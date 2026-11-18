#include "OccCadDisplayMeshBuilder.h"

// ===== OCC =====
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GeomLib.hxx>
#include <Geom_Curve.hxx>
#include <Geom_Surface.hxx>
#include <IMeshTools_Parameters.hxx>
#include <Poly_Connect.hxx>
#include <Poly_Polygon3D.hxx>
#include <Poly_PolygonOnTriangulation.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Precision.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>
#include <TColStd_Array1OfInteger.hxx>
#include <TColgp_Array1OfDir.hxx>
#include <TColgp_Array1OfPnt.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <gp_XYZ.hxx>

#if OCC_VERSION_HEX < 0x070600
#include <Poly_Array1OfTriangle.hxx>
#endif

// ===== VTK =====
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkFloatArray.h>
#include <vtkFieldData.h>
#include <vtkIdTypeArray.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <exception>
#include <unordered_set>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kDefaultDeviation = 0.5;
constexpr double kDefaultAngularDeflectionDeg = 28.5;
constexpr const char* kSubShapeIdsArrayName = "SUBSHAPE_IDS";

struct TessellationQuality
{
    double linearDeflection = Precision::Confusion();
    double angularDeflection = kDefaultAngularDeflectionDeg * kPi / 180.0;
};

// 直接采用 FreeCAD Part::Tools::getDeflection + ViewProviderPartExt 的默认参数语义：
// deflection = (dx + dy + dz) / 300 * Deviation，其中默认 Deviation=0.5%。
// 不再使用“中位 bbox 尺寸 + 0.0006 + 12°”这套自定义超细策略。
TessellationQuality displayQuality(const TopoDS_Shape& shape,
                                   double deviation,
                                   double angularDeflectionDeg)
{
    deviation = deviation > 0.0 ? deviation : kDefaultDeviation;
    angularDeflectionDeg = angularDeflectionDeg > 0.0
                               ? angularDeflectionDeg
                               : kDefaultAngularDeflectionDeg;

    double linearDeflection = Precision::Confusion();
    try
    {
        Bnd_Box bounds;
        BRepBndLib::Add(shape, bounds);
        bounds.SetGap(0.0);
        if (!bounds.IsVoid())
        {
            Standard_Real xmin = 0.0;
            Standard_Real ymin = 0.0;
            Standard_Real zmin = 0.0;
            Standard_Real xmax = 0.0;
            Standard_Real ymax = 0.0;
            Standard_Real zmax = 0.0;
            bounds.Get(xmin, ymin, zmin, xmax, ymax, zmax);

            const double candidate =
                ((xmax - xmin) + (ymax - ymin) + (zmax - zmin)) / 300.0 * deviation;
            if (std::isfinite(candidate))
                linearDeflection = std::max(candidate, static_cast<double>(Precision::Confusion()));
        }
    }
    catch (...)
    {
        linearDeflection = Precision::Confusion();
    }

    TessellationQuality quality;
    quality.linearDeflection = linearDeflection;
    quality.angularDeflection =
        std::max(1.0, std::min(angularDeflectionDeg, 180.0)) * kPi / 180.0;
    return quality;
}

gp_Pnt localTriangulationPoint(const Handle(Poly_Triangulation)& triangulation,
                               Standard_Integer nodeIndex)
{
#if OCC_VERSION_HEX < 0x070600
    return triangulation->Nodes()(nodeIndex);
#else
    return triangulation->Node(nodeIndex);
#endif
}

gp_Pnt triangulationPoint(const Handle(Poly_Triangulation)& triangulation,
                          Standard_Integer nodeIndex,
                          const TopLoc_Location& location)
{
    gp_Pnt point = localTriangulationPoint(triangulation, nodeIndex);
    if (!location.IsIdentity())
        point.Transform(location.Transformation());
    return point;
}

// 对应 FreeCAD Part::Tools::polygonOfEdge：Clean 后 free edge 往往已没有 Polygon3D，
// 此时用明确参数范围重建临时 Edge 并生成显示 polygon，而不是让整个 Shape 回退。
Handle(Poly_Polygon3D) polygonOfFreeEdge(const TopoDS_Edge& edge, TopLoc_Location& location)
{
    BRepAdaptor_Curve adaptor(edge);
    double first = adaptor.FirstParameter();
    double last = adaptor.LastParameter();

    Handle(Poly_Polygon3D) polygon = BRep_Tool::Polygon3D(edge, location);
    if (!polygon.IsNull() && !Precision::IsInfinite(first) && !Precision::IsInfinite(last))
        return polygon;

    // 只有数学上无界的 Curve 才限制一个有限预览窗口。正常 DXF / 草图
    // 线段的参数区间是有限的，通常随实际长度增长（例如 [0, 500]）。
    // 旧代码无条件裁剪到 [-50, 50]，导致在普通几何/网格页面只显示
    // 长边开头的一小截，而草图编辑层按完整参数绘制，看起来不一致。
    const bool finiteFirst = std::isfinite(first) && !Precision::IsInfinite(first);
    const bool finiteLast = std::isfinite(last) && !Precision::IsInfinite(last);
    if (!finiteFirst && !finiteLast)
    {
        first = -50.0;
        last = 50.0;
    }
    else if (!finiteFirst)
    {
        first = last - 100.0;
    }
    else if (!finiteLast)
    {
        last = first + 100.0;
    }
    if (!std::isfinite(first) || !std::isfinite(last) || !(last > first))
        return Handle(Poly_Polygon3D)();

    double curveFirst = 0.0;
    double curveLast = 0.0;
    Handle(Geom_Curve) curve = BRep_Tool::Curve(edge, curveFirst, curveLast);
    if (curve.IsNull())
        return Handle(Poly_Polygon3D)();

    BRepBuilderAPI_MakeEdge edgeBuilder(curve, first, last);
    if (!edgeBuilder.IsDone())
        return Handle(Poly_Polygon3D)();

    TopoDS_Shape temporary = edgeBuilder.Shape();
    const TopLoc_Location inverse = location.Inverted();
    temporary.Location(inverse);

    // 与 FreeCAD 当前 polygonOfEdge 保持一致的独立 Edge 显示离散参数。
    BRepMesh_IncrementalMesh(temporary, 0.005, Standard_False, 0.1, Standard_True);
    TopLoc_Location unused;
    return BRep_Tool::Polygon3D(TopoDS::Edge(temporary), unused);
}

// FreeCAD 的默认路径优先从真实 OCC Surface + UV 节点估算法线；只有无法得到
// surface normal 时才退回相邻三角形平均法线。这样圆柱/圆角不会只呈现 faceted normals。
bool computeFaceNormals(const TopoDS_Face& face,
                        const Handle(Poly_Triangulation)& triangulation,
                        const TopLoc_Location& location,
                        std::vector<gp_Dir>& normals)
{
    const int nbNodes = triangulation.IsNull() ? 0 : triangulation->NbNodes();
    if (nbNodes <= 0)
        return false;

    normals.assign(static_cast<size_t>(nbNodes) + 1u, gp::DZ());

#if OCC_VERSION_HEX >= 0x070600
    if (triangulation->HasNormals())
    {
        for (Standard_Integer nodeIndex = 1; nodeIndex <= nbNodes; ++nodeIndex)
            normals[static_cast<size_t>(nodeIndex)] = triangulation->Normal(nodeIndex);
    }
    else
    {
        Poly_Connect connect(triangulation);
        const TopoDS_Face zeroLocationFace =
            TopoDS::Face(face.Located(TopLoc_Location()));
        const Handle(Geom_Surface) surface = BRep_Tool::Surface(zeroLocationFace);
        const Standard_Real tolerance = Precision::Confusion();
        const bool hasUvNodes = triangulation->HasUVNodes() && !surface.IsNull();

        for (Standard_Integer nodeIndex = 1; nodeIndex <= nbNodes; ++nodeIndex)
        {
            gp_Dir normal = gp::DZ();
            const bool surfaceNormalFailed =
                !hasUvNodes ||
                GeomLib::NormEstim(surface,
                                   triangulation->UVNode(nodeIndex),
                                   tolerance,
                                   normal) > 1;

            if (surfaceNormalFailed)
            {
                gp_XYZ summed(0.0, 0.0, 0.0);
                for (connect.Initialize(nodeIndex); connect.More(); connect.Next())
                {
                    Standard_Integer n1 = 0;
                    Standard_Integer n2 = 0;
                    Standard_Integer n3 = 0;
                    triangulation->Triangle(connect.Value()).Get(n1, n2, n3);

                    const gp_XYZ v1 = localTriangulationPoint(triangulation, n2).XYZ() -
                                      localTriangulationPoint(triangulation, n1).XYZ();
                    const gp_XYZ v2 = localTriangulationPoint(triangulation, n3).XYZ() -
                                      localTriangulationPoint(triangulation, n2).XYZ();
                    const gp_XYZ cross = v1 ^ v2;
                    const Standard_Real modulus = cross.Modulus();
                    if (modulus >= tolerance)
                        summed += cross / modulus;
                }

                const Standard_Real modulus = summed.Modulus();
                normal = modulus > tolerance ? gp_Dir(summed) : gp::DZ();
            }

            normals[static_cast<size_t>(nodeIndex)] = normal;
        }
    }
#else
    // 旧 OCC 保留稳定兼容：直接由相邻三角形平均得到 point normal。
    Poly_Connect connect(triangulation);
    const Standard_Real tolerance = Precision::Confusion();
    const Poly_Array1OfTriangle& triangles = triangulation->Triangles();
    const TColgp_Array1OfPnt& nodes = triangulation->Nodes();
    for (Standard_Integer nodeIndex = nodes.Lower(); nodeIndex <= nodes.Upper(); ++nodeIndex)
    {
        gp_XYZ summed(0.0, 0.0, 0.0);
        for (connect.Initialize(nodeIndex); connect.More(); connect.Next())
        {
            Standard_Integer n1 = 0;
            Standard_Integer n2 = 0;
            Standard_Integer n3 = 0;
            triangles(connect.Value()).Get(n1, n2, n3);
            const gp_XYZ v1 = nodes(n2).XYZ() - nodes(n1).XYZ();
            const gp_XYZ v2 = nodes(n3).XYZ() - nodes(n2).XYZ();
            const gp_XYZ cross = v1 ^ v2;
            const Standard_Real modulus = cross.Modulus();
            if (modulus >= tolerance)
                summed += cross / modulus;
        }
        const Standard_Real modulus = summed.Modulus();
        normals[static_cast<size_t>(nodeIndex)] =
            modulus > tolerance ? gp_Dir(summed) : gp::DZ();
    }
#endif

    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    const bool transformed = !location.IsIdentity();
    const gp_Trsf transform = transformed ? location.Transformation() : gp_Trsf();

    for (Standard_Integer nodeIndex = 1; nodeIndex <= nbNodes; ++nodeIndex)
    {
        gp_Vec vector(normals[static_cast<size_t>(nodeIndex)]);
        if (reversed)
            vector.Reverse();
        if (transformed)
            vector.Transform(transform);
        if (vector.SquareMagnitude() <= gp::Resolution() * gp::Resolution())
            vector = gp_Vec(0.0, 0.0, 1.0);
        vector.Normalize();
        normals[static_cast<size_t>(nodeIndex)] = gp_Dir(vector);
    }

    return true;
}


} // namespace

namespace OCC
{
namespace Detail
{

CadDisplayMeshResult BuildCadDisplayMesh(const TopoDS_Shape& shape,
                                          double deviationCoeff,
                                          double featureAngleDeg,
                                          bool smoothShading,
                                          bool buildEdges)
{
    CadDisplayMeshResult result;
    result.surface = vtkSmartPointer<vtkPolyData>::New();
    result.edges = vtkSmartPointer<vtkPolyData>::New();

    if (shape.IsNull())
        return result;

    try
    {
        const TessellationQuality quality =
            displayQuality(shape, deviationCoeff, featureAngleDeg);

        // TopoDS_Shape 是共享底层 TShape 的轻量句柄。这里不能无条件 Clean：
        // 那会清掉同一 B-Rep 被其他显示/预览复用的 triangulation，造成重复剖分。
        // IncrementalMesh 会在缺少网格或现有网格不满足当前质量时按需更新。
        TopoDS_Shape displayShape = shape;

        TopTools_IndexedMapOfShape faceMap;
        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(displayShape, TopAbs_FACE, faceMap);
        TopExp::MapShapes(displayShape, TopAbs_EDGE, edgeMap);

        if (faceMap.Extent() > 0)
        {
            IMeshTools_Parameters meshParams;
            meshParams.Deflection = quality.linearDeflection;
            meshParams.Relative = Standard_False;
            meshParams.Angle = quality.angularDeflection;
            meshParams.InParallel = Standard_True;
            meshParams.AllowQualityDecrease = Standard_True;
            BRepMesh_IncrementalMesh mesher(displayShape, meshParams);
            (void)mesher;
        }

        // Surface 与 Edge 共享完全同一个 vtkPoints。Face-owned edge 读取
        // PolygonOnTriangulation 后直接引用这些 point id，不再复制/二次采样。
        vtkNew<vtkPoints> sharedPoints;
        sharedPoints->SetDataTypeToDouble();
        vtkNew<vtkCellArray> triangles;
        vtkNew<vtkCellArray> lines;

        vtkNew<vtkFloatArray> pointNormals;
        pointNormals->SetName("Normals");
        pointNormals->SetNumberOfComponents(3);

        vtkNew<vtkIdTypeArray> faceSubShapeIds;
        faceSubShapeIds->SetName(kSubShapeIdsArrayName);
        faceSubShapeIds->SetNumberOfComponents(1);
        vtkNew<vtkIdTypeArray> edgeSubShapeIds;
        edgeSubShapeIds->SetName(kSubShapeIdsArrayName);
        edgeSubShapeIds->SetNumberOfComponents(1);

        vtkNew<vtkIdTypeArray> faceUiIds;
        faceUiIds->SetName("OCC_FaceId");
        faceUiIds->SetNumberOfComponents(1);
        vtkNew<vtkIdTypeArray> edgeUiIds;
        edgeUiIds->SetName("OCC_EdgeId");
        edgeUiIds->SetNumberOfComponents(1);

        std::vector<unsigned char> emittedEdges(static_cast<size_t>(edgeMap.Extent()) + 1u, 0u);
        std::vector<unsigned char> faceOwnedEdges(static_cast<size_t>(edgeMap.Extent()) + 1u, 0u);

        vtkIdType triangleCount = 0;

        // ---------- Faces + exact Face-owned edges ----------
        for (int faceIndex = 1; faceIndex <= faceMap.Extent(); ++faceIndex)
        {
            const TopoDS_Face face = TopoDS::Face(faceMap(faceIndex));
            TopLoc_Location location;
            const Handle(Poly_Triangulation) triangulation =
                BRep_Tool::Triangulation(face, location);

            // 有 Face 却仍没有 triangulation，说明 OCC mesher 本身失败；这是少数真正需要
            // 调用方 fallback 的情况，不能输出半个实体。
            if (triangulation.IsNull() || triangulation->NbNodes() <= 0 ||
                triangulation->NbTriangles() <= 0)
            {
                return CadDisplayMeshResult{};
            }

            const int nbNodes = triangulation->NbNodes();
            std::vector<vtkIdType> nodeMap(static_cast<size_t>(nbNodes) + 1u, -1);
            std::vector<gp_Dir> occNormals;
            if (smoothShading && !computeFaceNormals(face, triangulation, location, occNormals))
                occNormals.assign(static_cast<size_t>(nbNodes) + 1u, gp::DZ());

            for (Standard_Integer nodeIndex = 1; nodeIndex <= nbNodes; ++nodeIndex)
            {
                const gp_Pnt point = triangulationPoint(triangulation, nodeIndex, location);
                nodeMap[static_cast<size_t>(nodeIndex)] =
                    sharedPoints->InsertNextPoint(point.X(), point.Y(), point.Z());

                if (smoothShading && nodeIndex < static_cast<Standard_Integer>(occNormals.size()))
                {
                    const gp_Dir& normal = occNormals[static_cast<size_t>(nodeIndex)];
                    pointNormals->InsertNextTuple3(static_cast<float>(normal.X()),
                                                   static_cast<float>(normal.Y()),
                                                   static_cast<float>(normal.Z()));
                }
                else
                {
                    pointNormals->InsertNextTuple3(0.0f, 0.0f, 1.0f);
                }
            }

            const vtkIdType faceCompatId = static_cast<vtkIdType>(faceIndex);
            const TopAbs_Orientation orientation = face.Orientation();

            for (Standard_Integer triangleIndex = 1;
                 triangleIndex <= triangulation->NbTriangles();
                 ++triangleIndex)
            {
                Standard_Integer n1 = 0;
                Standard_Integer n2 = 0;
                Standard_Integer n3 = 0;
#if OCC_VERSION_HEX < 0x070600
                triangulation->Triangles()(triangleIndex).Get(n1, n2, n3);
#else
                triangulation->Triangle(triangleIndex).Get(n1, n2, n3);
#endif
                if (orientation == TopAbs_REVERSED)
                    std::swap(n1, n2);

                if (n1 <= 0 || n1 > nbNodes || n2 <= 0 || n2 > nbNodes ||
                    n3 <= 0 || n3 > nbNodes)
                {
                    return CadDisplayMeshResult{};
                }

                vtkIdType ids[3] = {
                    nodeMap[static_cast<size_t>(n1)],
                    nodeMap[static_cast<size_t>(n2)],
                    nodeMap[static_cast<size_t>(n3)]};
                if (ids[0] < 0 || ids[1] < 0 || ids[2] < 0)
                    return CadDisplayMeshResult{};

                triangles->InsertNextCell(3, ids);
                faceSubShapeIds->InsertNextValue(faceCompatId);
                faceUiIds->InsertNextValue(static_cast<vtkIdType>(faceIndex));
                ++triangleCount;
            }

            if (!buildEdges)
                continue;

            // 一条共享 Edge 可能属于两个 Face；只在第一次拿到有效 polygon 时输出。
            // 如果当前 Face 没有 PolygonOnTriangulation，就让相邻 Face 再尝试；
            // 和 FreeCAD 一样，不为 Face-owned Edge 再生成第二套独立近似曲线。
            for (TopExp_Explorer edgeExp(face, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
            {
                const TopoDS_Edge edge = TopoDS::Edge(edgeExp.Current());
                const int edgeIndex = edgeMap.FindIndex(edge);
                if (edgeIndex <= 0 || edgeIndex > edgeMap.Extent())
                    continue;

                faceOwnedEdges[static_cast<size_t>(edgeIndex)] = 1u;
                if (emittedEdges[static_cast<size_t>(edgeIndex)] != 0u ||
                    BRep_Tool::Degenerated(edge))
                {
                    continue;
                }

                const Handle(Poly_PolygonOnTriangulation) polygon =
                    BRep_Tool::PolygonOnTriangulation(edge, triangulation, location);
                if (polygon.IsNull() || polygon->NbNodes() < 2)
                    continue;

                const TColStd_Array1OfInteger& polygonNodes = polygon->Nodes();
                std::vector<vtkIdType> lineIds;
                lineIds.reserve(static_cast<size_t>(polygonNodes.Length()));
                bool valid = true;

                for (Standard_Integer i = polygonNodes.Lower(); i <= polygonNodes.Upper(); ++i)
                {
                    const Standard_Integer nodeIndex = polygonNodes(i);
                    if (nodeIndex <= 0 || nodeIndex > nbNodes)
                    {
                        valid = false;
                        break;
                    }
                    const vtkIdType pointId = nodeMap[static_cast<size_t>(nodeIndex)];
                    if (pointId < 0)
                    {
                        valid = false;
                        break;
                    }
                    if (lineIds.empty() || lineIds.back() != pointId)
                        lineIds.push_back(pointId);
                }

                if (!valid || lineIds.size() < 2)
                    continue;

                lines->InsertNextCell(static_cast<vtkIdType>(lineIds.size()), lineIds.data());
                edgeSubShapeIds->InsertNextValue(static_cast<vtkIdType>(edgeIndex));
                edgeUiIds->InsertNextValue(static_cast<vtkIdType>(edgeIndex));
                emittedEdges[static_cast<size_t>(edgeIndex)] = 1u;
            }
        }

        // ---------- Free edges / pure wires ----------
        if (buildEdges)
        {
            for (int edgeIndex = 1; edgeIndex <= edgeMap.Extent(); ++edgeIndex)
            {
                if (emittedEdges[static_cast<size_t>(edgeIndex)] != 0u)
                    continue;

                const TopoDS_Edge edge = TopoDS::Edge(edgeMap(edgeIndex));
                if (edge.IsNull() || BRep_Tool::Degenerated(edge))
                    continue;

                // 对齐 FreeCAD setupCoinGeometry：属于 Face 的边只接受
                // PolygonOnTriangulation，不再另外创造第二套曲线近似。
                if (faceOwnedEdges[static_cast<size_t>(edgeIndex)] != 0u)
                    continue;

                TopLoc_Location location;
                const Handle(Poly_Polygon3D) polygon = polygonOfFreeEdge(edge, location);
                if (polygon.IsNull() || polygon->NbNodes() < 2)
                    continue;

                const TColgp_Array1OfPnt& nodes = polygon->Nodes();
                const bool transformed = !location.IsIdentity();
                const gp_Trsf transform = transformed ? location.Transformation() : gp_Trsf();

                std::vector<vtkIdType> lineIds;
                lineIds.reserve(static_cast<size_t>(polygon->NbNodes()));
                for (Standard_Integer i = nodes.Lower(); i <= nodes.Upper(); ++i)
                {
                    gp_Pnt point = nodes(i);
                    if (transformed)
                        point.Transform(transform);
                    lineIds.push_back(sharedPoints->InsertNextPoint(point.X(), point.Y(), point.Z()));
                    pointNormals->InsertNextTuple3(0.0f, 0.0f, 1.0f);
                }

                if (lineIds.size() < 2)
                    continue;

                lines->InsertNextCell(static_cast<vtkIdType>(lineIds.size()), lineIds.data());
                edgeSubShapeIds->InsertNextValue(static_cast<vtkIdType>(edgeIndex));
                edgeUiIds->InsertNextValue(static_cast<vtkIdType>(edgeIndex));
                emittedEdges[static_cast<size_t>(edgeIndex)] = 1u;
            }
        }

        vtkNew<vtkPolyData> surfaceInput;
        surfaceInput->SetPoints(sharedPoints);
        surfaceInput->SetPolys(triangles);
        surfaceInput->GetCellData()->AddArray(faceSubShapeIds);
        surfaceInput->GetCellData()->AddArray(faceUiIds);
        if (smoothShading && pointNormals->GetNumberOfTuples() == sharedPoints->GetNumberOfPoints())
            surfaceInput->GetPointData()->SetNormals(pointNormals);

        vtkNew<vtkPolyData> edgeOutput;
        edgeOutput->SetPoints(sharedPoints);
        edgeOutput->SetLines(lines);
        edgeOutput->GetCellData()->AddArray(edgeSubShapeIds);
        edgeOutput->GetCellData()->AddArray(edgeUiIds);

        result.surface->ShallowCopy(surfaceInput);
        result.edges->ShallowCopy(edgeOutput);

        const bool surfaceOk = faceMap.Extent() == 0 ||
                               (triangleCount > 0 &&
                                faceSubShapeIds->GetNumberOfTuples() == result.surface->GetNumberOfPolys());
        const bool edgesOk = !buildEdges ||
                             edgeSubShapeIds->GetNumberOfTuples() == result.edges->GetNumberOfLines();
        const bool hasAnything = result.surface->GetNumberOfPolys() > 0 ||
                                 result.edges->GetNumberOfLines() > 0;

        if (!surfaceOk || !edgesOk || !hasAnything)
            return CadDisplayMeshResult{};

        result.success = true;
        return result;
    }
    catch (const Standard_Failure&)
    {
        return CadDisplayMeshResult{};
    }
    catch (const std::exception&)
    {
        return CadDisplayMeshResult{};
    }
    catch (...)
    {
        return CadDisplayMeshResult{};
    }
}

} // namespace Detail
} // namespace OCC
