#include <GeometryDisplayManager.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkCellArray.h>
#include "GeometryEntityTools.h"
#include <QJsonArray>
#include "GeometrySelectionVisuals.h"

#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <GeometryDisplayStyle.h>
#include <OccToVtkConverter.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <BRepGProp.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pln.hxx>
#include <gp_Lin.hxx>
#include <gp_Circ.hxx>
#include <gp_Elips.hxx>
#include <gp_Vec.hxx>
#include <GeomLProp_SLProps.hxx>
#include <Geom_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Precision.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ListOfShape.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <Poly_Triangulation.hxx>
#include <Poly_Triangle.hxx>
#include <TopLoc_Location.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BOPAlgo_Splitter.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>

#include <QSet>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSaveFile>
#include <QTextStream>
#include <QVector>
#include <QVariantList>
#include <cmath>
#include <algorithm>
#include <limits>
#include <map>
#include <tuple>
#include <set>
#include <vector>

#include <vtkActor.h>
#include <vtkAppendPolyData.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkProp.h>
#include <vtkSmartPointer.h>

namespace
{
const QString kEntityHighlightPropId = QStringLiteral("preproc.entity.highlight");
const QString kEntityHoverHighlightPropId = QStringLiteral("preproc.entity.highlight.hover");

TopAbs_ShapeEnum shapeTypeForDimension(int dim)
{
    if (dim == 2)
        return TopAbs_FACE;
    if (dim == 3)
        return TopAbs_SOLID;
    return TopAbs_EDGE;
}

bool buildIndexedShapesFromGeometry(const QStringList& uuids, int dim, TopTools_IndexedMapOfShape& indexed)
{
    indexed.Clear();
    if (dim < 1 || dim > 3)
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || uuids.isEmpty())
        return false;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);

    bool hasShape = false;
    for (const QString& uuid : uuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;

        builder.Add(compound, *obj->getShape());
        hasShape = true;
    }
    if (!hasShape)
        return false;

    TopExp::MapShapes(compound, shapeTypeForDimension(dim), indexed);
    return indexed.Extent() > 0;
}

bool readIndexedShape(const QStringList& uuids, int dim, int id, TopoDS_Shape& shape)
{
    if (id <= 0)
        return false;

    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromGeometry(uuids, dim, indexed) || id > indexed.Extent())
        return false;

    shape = indexed(id);
    return !shape.IsNull();
}

bool addMassProperties(const TopoDS_Shape& shape, int dim, GProp_GProps& props)
{
    if (dim == 1)
        BRepGProp::LinearProperties(shape, props);
    else if (dim == 2)
        BRepGProp::SurfaceProperties(shape, props);
    else if (dim == 3)
        BRepGProp::VolumeProperties(shape, props);
    else
        return false;
    return true;
}


GeometryPickMode pickModeForDimension(int dim)
{
    if (dim == 2)
        return GeometryPickMode::Face;
    if (dim == 3)
        return GeometryPickMode::Solid;
    return GeometryPickMode::Edge;
}

// Mesh/pre-processing pages expose one global UI id space across all active
// geometry objects, while the visible CAD actors store OCC_FaceId/OCC_EdgeId
// local to each object. Resolve the global compound id back to the exact
// {uuid, local OCC index} used by GeometrySelectionVisuals so geometry and mesh
// selection share one rendering/identity path.
QVector<GeometryPickItem> resolveGlobalPickItems(const QStringList& uuids,
                                                 int dim,
                                                 const QList<int>& ids)
{
    QVector<GeometryPickItem> items;
    if (uuids.isEmpty() || ids.isEmpty() || dim < 1 || dim > 3)
        return items;

    TopTools_IndexedMapOfShape globalIndexed;
    if (!buildIndexedShapesFromGeometry(uuids, dim, globalIndexed))
        return items;

    QSet<int> wanted;
    for (int id : ids)
    {
        if (id > 0 && id <= globalIndexed.Extent())
            wanted.insert(id);
    }
    if (wanted.isEmpty())
        return items;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return items;

    const TopAbs_ShapeEnum type = shapeTypeForDimension(dim);
    const GeometryPickMode mode = pickModeForDimension(dim);

    // Resolve by OCC topology identity, not by center/area or display position.
    // IsSame() deliberately ignores orientation: OCC_FaceId/OCC_EdgeId in the
    // presentation are based on TopExp::MapShapes() of the owning object and the
    // same underlying TShape is what matters for selection.
    for (const QString& uuid : uuids)
    {
        if (wanted.isEmpty())
            break;

        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;

        TopTools_IndexedMapOfShape localIndexed;
        TopExp::MapShapes(*obj->getShape(), type, localIndexed);
        if (localIndexed.Extent() <= 0)
            continue;

        const QList<int> remaining = wanted.values();
        for (int globalId : remaining)
        {
            const TopoDS_Shape& target = globalIndexed(globalId);
            const int localId = localIndexed.FindIndex(target);
            if (localId <= 0)
                continue;
            items.push_back(GeometryPickItem{uuid, mode, localId});
            wanted.remove(globalId);
        }
    }

    return items;
}

struct HexSolidTopologyCheck
{
    bool sweepable = false;
    int faceCount = 0;
    int nonQuadFaceCount = 0;
    QString reason;
};

HexSolidTopologyCheck checkHexSweepTopology(const TopoDS_Shape& solid)
{
    HexSolidTopologyCheck out;
    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(solid, TopAbs_FACE, faces);
    out.faceCount = faces.Extent();
    if (out.faceCount != 6)
    {
        out.reason = QStringLiteral("边界面=%1（纯 Hex Sweep 需要 6 面）").arg(out.faceCount);
        return out;
    }

    TopTools_IndexedMapOfShape solidVertices;
    TopExp::MapShapes(solid, TopAbs_VERTEX, solidVertices);

    struct FaceTopo
    {
        std::set<int> vertices;
        int edgeCount = 0;
    };
    std::vector<FaceTopo> topo;
    topo.reserve(6);

    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const TopoDS_Shape& face = faces(i);
        TopTools_IndexedMapOfShape edgeMap;
        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(face, TopAbs_EDGE, edgeMap);
        TopExp::MapShapes(face, TopAbs_VERTEX, vertexMap);

        FaceTopo ft;
        ft.edgeCount = edgeMap.Extent();
        for (int v = 1; v <= vertexMap.Extent(); ++v)
        {
            const int globalId = solidVertices.FindIndex(vertexMap(v));
            if (globalId > 0) ft.vertices.insert(globalId);
        }
        if (ft.edgeCount != 4 || ft.vertices.size() != 4)
            ++out.nonQuadFaceCount;
        topo.push_back(std::move(ft));
    }

    if (out.nonQuadFaceCount > 0)
    {
        out.reason = QStringLiteral("存在 %1 个非四边块面").arg(out.nonQuadFaceCount);
        return out;
    }

    // 平移 Sweep 几何分析的核心拓扑要求：寻找一对
    // 不共享角点的四边端面，并要求另外四个侧面各含 2 个 Source + 2 个 Target 角点。
    for (int i = 0; i < 6; ++i)
    {
        for (int j = i + 1; j < 6; ++j)
        {
            bool disjoint = true;
            for (int v : topo[i].vertices)
            {
                if (topo[j].vertices.count(v))
                {
                    disjoint = false;
                    break;
                }
            }
            if (!disjoint) continue;

            bool sidesOk = true;
            for (int k = 0; k < 6; ++k)
            {
                if (k == i || k == j) continue;
                int srcHit = 0, dstHit = 0;
                for (int v : topo[k].vertices)
                {
                    if (topo[i].vertices.count(v)) ++srcHit;
                    if (topo[j].vertices.count(v)) ++dstHit;
                }
                if (srcHit != 2 || dstHit != 2)
                {
                    sidesOk = false;
                    break;
                }
            }
            if (sidesOk)
            {
                out.sweepable = true;
                out.reason = QStringLiteral("6 面四边 Sweep 拓扑");
                return out;
            }
        }
    }

    out.reason = QStringLiteral("6 个面均为四边面，但没有找到 Source/Target 一一对应拓扑");
    return out;
}

bool buildActiveSolidArguments(const QStringList& uuids,
                               TopTools_ListOfShape& arguments,
                               TopoDS_Compound& compound,
                               int& solidCount)
{
    arguments.Clear();
    solidCount = 0;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || uuids.isEmpty()) return false;

    BRep_Builder builder;
    builder.MakeCompound(compound);
    TopTools_IndexedMapOfShape uniqueSolids;
    for (const QString& uuid : uuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull()) continue;
        TopTools_IndexedMapOfShape local;
        TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, local);
        for (int i = 1; i <= local.Extent(); ++i)
            uniqueSolids.Add(local(i));
    }

    for (int i = 1; i <= uniqueSolids.Extent(); ++i)
    {
        const TopoDS_Shape& solid = uniqueSolids(i);
        arguments.Append(solid);
        builder.Add(compound, solid);
        ++solidCount;
    }
    return solidCount > 0;
}
} // namespace

namespace GeometryEntityTools
{
QJsonObject meshingShapeIndexMap(const QStringList& uuids, const TopoDS_Shape& exported)
{
    QJsonObject result{{"format",1}};
    for (int dim=1;dim<=3;++dim) {
        TopTools_IndexedMapOfShape ui, native;
        buildIndexedShapesFromGeometry(uuids,dim,ui);
        TopExp::MapShapes(exported,shapeTypeForDimension(dim),native);
        QJsonArray indices;
        for (int i=1;i<=ui.Extent();++i) indices.append(native.FindIndex(ui(i)));
        result[QString::number(dim)] = indices;
        result[QStringLiteral("count%1").arg(dim)] = native.Extent();
    }
    return result;
}

QStringList listEntityIds(const QStringList& geometryUuids, int dim)
{
    QStringList out;
    TopTools_IndexedMapOfShape indexed;
    if (!buildIndexedShapesFromGeometry(geometryUuids, dim, indexed))
        return out;

    for (int i = 1; i <= indexed.Extent(); ++i)
        out << QString::number(i);
    return out;
}

bool entityCenter(const QStringList& geometryUuids, int dim, int id, QVector3D& center)
{
    center = QVector3D();

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, dim, id, target))
        return false;

    GProp_GProps props;
    if (!addMassProperties(target, dim, props))
        return false;

    const gp_Pnt c = props.CentreOfMass();
    center = QVector3D(c.X(), c.Y(), c.Z());
    return true;
}

bool entityNormal(const QStringList& geometryUuids, int dim, int id, QVector3D& normal)
{
    normal = QVector3D();
    if (dim != 2)
        return false;

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, dim, id, target) || target.ShapeType() != TopAbs_FACE)
        return false;

    const TopoDS_Face face = TopoDS::Face(target);
    Handle(Geom_Surface) surface = BRep_Tool::Surface(face);
    if (surface.IsNull())
        return false;

    double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0;
    BRepTools::UVBounds(face, uMin, uMax, vMin, vMax);
    if (!std::isfinite(uMin) || !std::isfinite(uMax) || !std::isfinite(vMin) || !std::isfinite(vMax))
        return false;

    const double u = 0.5 * (uMin + uMax);
    const double v = 0.5 * (vMin + vMax);
    GeomLProp_SLProps props(surface, u, v, 1, Precision::Confusion());
    if (!props.IsNormalDefined())
        return false;

    gp_Dir n = props.Normal();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    normal = QVector3D{static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z())};
    if (normal.lengthSquared() <= 0.0f)
        return false;
    normal.normalize();
    return true;
}

bool entityLine(const QStringList& geometryUuids, int dim, int id, QVector3D& origin, QVector3D& direction)
{
    origin = QVector3D();
    direction = QVector3D();
    if (dim != 1)
        return false;

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, dim, id, target) || target.ShapeType() != TopAbs_EDGE)
        return false;

    const TopoDS_Edge edge = TopoDS::Edge(target);
    BRepAdaptor_Curve adaptor(edge);
    if (adaptor.GetType() != GeomAbs_Line)
        return false;

    GProp_GProps props;
    if (!addMassProperties(target, dim, props))
        return false;
    const gp_Pnt c = props.CentreOfMass();
    origin = QVector3D{static_cast<float>(c.X()), static_cast<float>(c.Y()), static_cast<float>(c.Z())};

    gp_Dir d = adaptor.Line().Direction();
    direction = QVector3D{static_cast<float>(d.X()), static_cast<float>(d.Y()), static_cast<float>(d.Z())};
    if (direction.lengthSquared() <= 1e-16f)
        return false;
    direction.normalize();

    // Canonicalize the sign so the same geometric line direction is stable even
    // when OCC flips topological edge orientation.
    const float ax = std::abs(direction.x());
    const float ay = std::abs(direction.y());
    const float az = std::abs(direction.z());
    float dominant = direction.x();
    if (ay > ax && ay >= az) dominant = direction.y();
    else if (az > ax && az > ay) dominant = direction.z();
    if (dominant < 0.0f) direction = -direction;
    return true;
}


QVariantMap edgeBlockingInfo(const QStringList& geometryUuids, int id)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("transitionPlanarCurved"), false);
    out.insert(QStringLiteral("transitionPlanarPlanar"), false);

    QVector3D origin;
    QVector3D direction;
    if (!entityLine(geometryUuids, 1, id, origin, direction))
        return out;

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, 1, id, target) || target.ShapeType() != TopAbs_EDGE)
        return out;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || geometryUuids.isEmpty())
        return out;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    bool hasShape = false;
    for (const QString& uuid : geometryUuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;
        builder.Add(compound, *obj->getShape());
        hasShape = true;
    }
    if (!hasShape)
        return out;

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(compound, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);

    TopTools_IndexedMapOfShape faceMap;
    TopExp::MapShapes(compound, TopAbs_FACE, faceMap);

    QVariantList adjacentFaceIds;
    QVariantList planarNormals;
    QVariantList surfaceTypes;
    int planarCount = 0;
    int curvedCount = 0;

    if (edgeToFaces.Contains(target))
    {
        const TopTools_ListOfShape& faces = edgeToFaces.FindFromKey(target);
        for (TopTools_ListIteratorOfListOfShape it(faces); it.More(); it.Next())
        {
            const TopoDS_Face face = TopoDS::Face(it.Value());
            const int faceId = faceMap.FindIndex(face);
            if (faceId > 0)
                adjacentFaceIds.push_back(faceId);

            BRepAdaptor_Surface adaptor(face, true);
            const GeomAbs_SurfaceType type = adaptor.GetType();
            surfaceTypes.push_back(static_cast<int>(type));
            if (type == GeomAbs_Plane)
            {
                ++planarCount;
                gp_Dir n = adaptor.Plane().Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED)
                    n.Reverse();
                QVector3D normal{
                    static_cast<float>(n.X()),
                    static_cast<float>(n.Y()),
                    static_cast<float>(n.Z())};
                if (normal.lengthSquared() > 1e-16f)
                {
                    normal.normalize();
                    planarNormals.push_back(QVariant::fromValue(normal));
                }
            }
            else
            {
                ++curvedCount;
            }
        }
    }

    double length = 0.0;
    entityMeasure(geometryUuids, 1, id, length);

    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("origin"), QVariant::fromValue(origin));
    out.insert(QStringLiteral("direction"), QVariant::fromValue(direction));
    out.insert(QStringLiteral("length"), length);
    out.insert(QStringLiteral("adjacentFaceIds"), adjacentFaceIds);
    out.insert(QStringLiteral("surfaceTypes"), surfaceTypes);
    out.insert(QStringLiteral("planarNormals"), planarNormals);
    out.insert(QStringLiteral("adjacentFaceCount"), adjacentFaceIds.size());
    bool transitionPlanarPlanar = false;
    if (curvedCount == 0 && planarNormals.size() >= 2)
    {
        for (int i = 0; i < int(planarNormals.size()) && !transitionPlanarPlanar; ++i)
        {
            QVector3D a = planarNormals[i].value<QVector3D>();
            if (a.lengthSquared() <= 1e-16f) continue;
            a.normalize();
            for (int j = i + 1; j < int(planarNormals.size()); ++j)
            {
                QVector3D b = planarNormals[j].value<QVector3D>();
                if (b.lengthSquared() <= 1e-16f) continue;
                b.normalize();
                // A real planar crease/step: ignore nearly coplanar seams.  A broad
                // threshold is intentional; OCC topology precheck later decides whether
                // a paired crease actually improves Sweep regions.
                if (std::abs(double(QVector3D::dotProduct(a, b))) < 0.985)
                {
                    transitionPlanarPlanar = true;
                    break;
                }
            }
        }
    }

    out.insert(QStringLiteral("planarFaceCount"), planarCount);
    out.insert(QStringLiteral("curvedFaceCount"), curvedCount);
    out.insert(QStringLiteral("transitionPlanarCurved"), planarCount > 0 && curvedCount > 0);
    out.insert(QStringLiteral("transitionPlanarPlanar"), transitionPlanarPlanar);
    return out;
}




QVariantMap edgeSweepFeaturePlane(const QStringList& geometryUuids, int id)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("edgeId"), id);

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, 1, id, target) || target.ShapeType() != TopAbs_EDGE)
        return out;

    const TopoDS_Edge edge = TopoDS::Edge(target);
    BRepAdaptor_Curve curve(edge);
    if (curve.GetType() == GeomAbs_Line)
        return out;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || geometryUuids.isEmpty())
        return out;

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    bool hasShape = false;
    for (const QString& uuid : geometryUuids)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;
        builder.Add(compound, *obj->getShape());
        hasShape = true;
    }
    if (!hasShape)
        return out;

    TopTools_IndexedDataMapOfShapeListOfShape edgeToFaces;
    TopExp::MapShapesAndAncestors(compound, TopAbs_EDGE, TopAbs_FACE, edgeToFaces);
    if (!edgeToFaces.Contains(target))
        return out;

    int adjacentCount = 0;
    int planarCount = 0;
    int curvedCount = 0;
    std::set<int> surfaceTypes;
    const TopTools_ListOfShape& faces = edgeToFaces.FindFromKey(target);
    for (TopTools_ListIteratorOfListOfShape it(faces); it.More(); it.Next())
    {
        ++adjacentCount;
        const TopoDS_Face face = TopoDS::Face(it.Value());
        BRepAdaptor_Surface surf(face, true);
        const int t = static_cast<int>(surf.GetType());
        surfaceTypes.insert(t);
        if (surf.GetType() == GeomAbs_Plane) ++planarCount;
        else ++curvedCount;
    }
    if (adjacentCount < 2)
        return out;

    gp_Pnt originP;
    gp_Dir normalD(0.0, 0.0, 1.0);
    bool havePlane = false;
    QString sourceKind;

    try
    {
        if (curve.GetType() == GeomAbs_Circle)
        {
            const gp_Circ c = curve.Circle();
            originP = c.Location();
            normalD = c.Axis().Direction();
            havePlane = true;
            sourceKind = QStringLiteral("Circle");
        }
        else if (curve.GetType() == GeomAbs_Ellipse)
        {
            const gp_Elips e = curve.Ellipse();
            originP = e.Location();
            normalD = e.Axis().Direction();
            havePlane = true;
            sourceKind = QStringLiteral("Ellipse");
        }
        else
        {
            const double u0 = curve.FirstParameter();
            const double u1 = curve.LastParameter();
            if (std::isfinite(u0) && std::isfinite(u1) && u1 > u0)
            {
                std::vector<gp_Pnt> pts;
                pts.reserve(9);
                for (int i = 0; i < 9; ++i)
                {
                    const double t = double(i) / 8.0;
                    pts.push_back(curve.Value(u0 * (1.0 - t) + u1 * t));
                }

                int ia = -1, ib = -1, ic = -1;
                double bestCross = 0.0;
                for (int a = 0; a < int(pts.size()); ++a)
                    for (int b = a + 1; b < int(pts.size()); ++b)
                        for (int c = b + 1; c < int(pts.size()); ++c)
                        {
                            const gp_Vec ab(pts[std::size_t(a)], pts[std::size_t(b)]);
                            const gp_Vec ac(pts[std::size_t(a)], pts[std::size_t(c)]);
                            const double cross = ab.Crossed(ac).Magnitude();
                            if (cross > bestCross)
                            {
                                bestCross = cross;
                                ia = a; ib = b; ic = c;
                            }
                        }

                double length = 0.0;
                entityMeasure(geometryUuids, 1, id, length);
                const double tol = std::max({Precision::Confusion() * 50.0,
                                             std::max(1.0, length) * 2.0e-5,
                                             1.0e-8});
                if (ia >= 0 && bestCross > tol * tol)
                {
                    const gp_Vec ab(pts[std::size_t(ia)], pts[std::size_t(ib)]);
                    const gp_Vec ac(pts[std::size_t(ia)], pts[std::size_t(ic)]);
                    gp_Vec n = ab.Crossed(ac);
                    if (n.Magnitude() > tol)
                    {
                        n.Normalize();
                        double sx = 0.0, sy = 0.0, sz = 0.0;
                        for (const gp_Pnt& p : pts)
                        {
                            sx += p.X();
                            sy += p.Y();
                            sz += p.Z();
                        }
                        const double inv = 1.0 / double(pts.size());
                        originP = gp_Pnt(sx * inv, sy * inv, sz * inv);

                        double maxDist = 0.0;
                        for (const gp_Pnt& p : pts)
                        {
                            const gp_Vec op(originP, p);
                            maxDist = std::max(maxDist, std::abs(op.Dot(n)));
                        }
                        if (maxDist <= tol)
                        {
                            normalD = gp_Dir(n);
                            havePlane = true;
                            sourceKind = QStringLiteral("PlanarCurve");
                        }
                    }
                }
            }
        }
    }
    catch (...)
    {
        havePlane = false;
    }

    if (!havePlane)
        return out;

    const bool typeTransition = surfaceTypes.size() >= 2 || (planarCount > 0 && curvedCount > 0);
    if (!typeTransition)
        return out;

    QVector3D normal{
        static_cast<float>(normalD.X()),
        static_cast<float>(normalD.Y()),
        static_cast<float>(normalD.Z())};
    if (normal.lengthSquared() <= 1e-16f)
        return out;
    normal.normalize();

    const float ax = std::abs(normal.x());
    const float ay = std::abs(normal.y());
    const float az = std::abs(normal.z());
    float dominant = normal.x();
    if (ay > ax && ay >= az) dominant = normal.y();
    else if (az > ax && az > ay) dominant = normal.z();
    if (dominant < 0.0f) normal = -normal;

    const QVector3D origin{
        static_cast<float>(originP.X()),
        static_cast<float>(originP.Y()),
        static_cast<float>(originP.Z())};

    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("origin"), QVariant::fromValue(origin));
    out.insert(QStringLiteral("normal"), QVariant::fromValue(normal));
    out.insert(QStringLiteral("sourceKind"), sourceKind);
    out.insert(QStringLiteral("adjacentFaceCount"), adjacentCount);
    out.insert(QStringLiteral("planarFaceCount"), planarCount);
    out.insert(QStringLiteral("curvedFaceCount"), curvedCount);
    out.insert(QStringLiteral("surfaceTypeCount"), int(surfaceTypes.size()));
    return out;
}


QVariantMap detectTranslationalPaveSweep(const QStringList& geometryUuids)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("sweepable"), false);
    out.insert(QStringLiteral("sourceFaceId"), -1);
    out.insert(QStringLiteral("targetFaceId"), -1);
    out.insert(QStringLiteral("profileVertexCount"), 0);

    TopTools_ListOfShape arguments;
    TopoDS_Compound sourceCompound;
    int solidCount = 0;
    if (!buildActiveSolidArguments(geometryUuids, arguments, sourceCompound, solidCount))
    {
        out.insert(QStringLiteral("message"), QStringLiteral("当前没有可用于 Sweep 分析的 OCC 实体"));
        return out;
    }

    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("solidCount"), solidCount);
    if (solidCount != 1)
    {
        out.insert(QStringLiteral("message"),
            QStringLiteral("整体 Pave-Sweep 只对单一 OCC Solid 做高置信度识别；当前实体数=%1").arg(solidCount));
        return out;
    }

    TopoDS_Shape solid;
    for (TopExp_Explorer it(sourceCompound, TopAbs_SOLID); it.More(); it.Next())
    {
        solid = it.Current();
        break;
    }
    if (solid.IsNull())
    {
        out.insert(QStringLiteral("valid"), false);
        out.insert(QStringLiteral("message"), QStringLiteral("无法读取 OCC Solid"));
        return out;
    }

    Bnd_Box box;
    BRepBndLib::Add(solid, box);
    double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = std::max(0.0, xmax - xmin);
    const double dy = std::max(0.0, ymax - ymin);
    const double dz = std::max(0.0, zmax - zmin);
    const double diag = std::max(1.0e-12, std::sqrt(dx * dx + dy * dy + dz * dz));
    const double tol = std::max({Precision::Confusion() * 20.0, diag * 1.0e-6, 1.0e-9});
    const double tol2 = tol * tol;
    constexpr double kParallel = 0.9995;

    TopTools_IndexedMapOfShape globalFaces;
    TopExp::MapShapes(sourceCompound, TopAbs_FACE, globalFaces);
    TopTools_IndexedMapOfShape solidFaces;
    TopExp::MapShapes(solid, TopAbs_FACE, solidFaces);
    TopTools_IndexedMapOfShape solidVertices;
    TopExp::MapShapes(solid, TopAbs_VERTEX, solidVertices);

    auto globalFaceId = [&](const TopoDS_Face& face) -> int
    {
        int id = globalFaces.FindIndex(face);
        if (id > 0) return id;
        for (int i = 1; i <= globalFaces.Extent(); ++i)
            if (globalFaces(i).IsSame(face)) return i;
        return -1;
    };

    auto vertexPoints = [](const TopoDS_Shape& shape)
    {
        std::vector<gp_Pnt> pts;
        TopTools_IndexedMapOfShape vertices;
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
        pts.reserve(std::size_t(std::max(0, vertices.Extent())));
        for (int i = 1; i <= vertices.Extent(); ++i)
            pts.push_back(BRep_Tool::Pnt(TopoDS::Vertex(vertices(i))));
        return pts;
    };

    auto squaredDistancePnt = [](const gp_Pnt& a, const gp_Pnt& b)
    {
        const double x = a.X() - b.X();
        const double y = a.Y() - b.Y();
        const double z = a.Z() - b.Z();
        return x * x + y * y + z * z;
    };

    struct FaceInfo
    {
        TopoDS_Face face;
        int globalId = -1;
        gp_Pnt center;
        gp_Dir normal;
        double area = 0.0;
        std::vector<gp_Pnt> vertices;
    };

    std::vector<FaceInfo> planarFaces;
    planarFaces.reserve(std::size_t(std::max(0, solidFaces.Extent())));
    for (int i = 1; i <= solidFaces.Extent(); ++i)
    {
        const TopoDS_Face face = TopoDS::Face(solidFaces(i));
        BRepAdaptor_Surface adaptor(face, true);
        if (adaptor.GetType() != GeomAbs_Plane)
            continue;

        GProp_GProps props;
        BRepGProp::SurfaceProperties(face, props);
        const double area = std::max(0.0, props.Mass());
        if (!(area > Precision::Confusion()))
            continue;

        gp_Dir n = adaptor.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            n.Reverse();

        FaceInfo info;
        info.face = face;
        info.globalId = globalFaceId(face);
        info.center = props.CentreOfMass();
        info.normal = n;
        info.area = area;
        info.vertices = vertexPoints(face);
        if (info.globalId > 0 && info.vertices.size() >= 3)
            planarFaces.push_back(std::move(info));
    }

    const auto allVertices = vertexPoints(solid);
    if (planarFaces.size() < 2 || allVertices.size() < 4)
    {
        out.insert(QStringLiteral("message"), QStringLiteral("没有找到足够的平面端面用于整体平移 Sweep 识别"));
        return out;
    }

    double bestScore = -1.0;
    int bestA = -1, bestB = -1;
    gp_Vec bestTranslation;

    for (int ia = 0; ia < int(planarFaces.size()); ++ia)
    {
        for (int ib = ia + 1; ib < int(planarFaces.size()); ++ib)
        {
            const FaceInfo& a = planarFaces[std::size_t(ia)];
            const FaceInfo& b = planarFaces[std::size_t(ib)];
            if (a.vertices.size() != b.vertices.size())
                continue;

            const double parallel = std::abs(a.normal.Dot(b.normal));
            if (parallel < kParallel)
                continue;

            const gp_Vec translation(a.center, b.center);
            const double length = translation.Magnitude();
            if (!(length > tol))
                continue;
            const gp_Dir td(translation);
            if (std::abs(a.normal.Dot(td)) < kParallel)
                continue;

            const double areaScale = std::max({a.area, b.area, tol * tol});
            if (std::abs(a.area - b.area) > areaScale * 2.0e-3)
                continue;

            // Source/Target 边界必须是一一平移对应；曲线本身不需要离散，
            // 顶点集合匹配即可验证拓扑层。真正的曲线/曲面仍由 OCC 保留。
            std::vector<bool> used(b.vertices.size(), false);
            bool translatedMatch = true;
            for (const gp_Pnt& pa : a.vertices)
            {
                gp_Pnt expected = pa.Translated(translation);
                int best = -1;
                double bestD2 = std::numeric_limits<double>::max();
                for (int j = 0; j < int(b.vertices.size()); ++j)
                {
                    if (used[std::size_t(j)]) continue;
                    const double d2 = squaredDistancePnt(expected, b.vertices[std::size_t(j)]);
                    if (d2 < bestD2)
                    {
                        bestD2 = d2;
                        best = j;
                    }
                }
                if (best < 0 || bestD2 > tol2)
                {
                    translatedMatch = false;
                    break;
                }
                used[std::size_t(best)] = true;
            }
            if (!translatedMatch)
                continue;

            // 一个严格平移 Sweep 的所有拓扑顶点只能落在 Source/Target 两层。
            const gp_Dir n = a.normal;
            auto planeCoord = [&](const gp_Pnt& pnt)
            {
                return n.X() * pnt.X() + n.Y() * pnt.Y() + n.Z() * pnt.Z();
            };
            const double da = planeCoord(a.center);
            const double db = planeCoord(b.center);
            bool twoLayers = true;
            for (const gp_Pnt& pnt : allVertices)
            {
                const double d = planeCoord(pnt);
                if (std::min(std::abs(d - da), std::abs(d - db)) > tol * 2.0)
                {
                    twoLayers = false;
                    break;
                }
            }
            if (!twoLayers)
                continue;

            // 除端面外，每个侧面都必须同时连接 Source 和 Target 两层。
            bool allSidesBridge = true;
            for (int fi = 1; fi <= solidFaces.Extent(); ++fi)
            {
                const TopoDS_Face side = TopoDS::Face(solidFaces(fi));
                if (side.IsSame(a.face) || side.IsSame(b.face))
                    continue;
                bool onA = false, onB = false;
                const auto pts = vertexPoints(side);
                for (const gp_Pnt& pnt : pts)
                {
                    const double d = planeCoord(pnt);
                    onA = onA || std::abs(d - da) <= tol * 2.0;
                    onB = onB || std::abs(d - db) <= tol * 2.0;
                }
                if (!onA || !onB)
                {
                    allSidesBridge = false;
                    break;
                }
            }
            if (!allSidesBridge)
                continue;

            const double score = std::min(a.area, b.area) * length *
                (1.0 + 0.02 * double(a.vertices.size()));
            if (score > bestScore)
            {
                bestScore = score;
                bestA = ia;
                bestB = ib;
                bestTranslation = translation;
            }
        }
    }

    if (bestA < 0 || bestB < 0)
    {
        out.insert(QStringLiteral("message"),
            QStringLiteral("原 OCC 实体不是严格平移 Sweep；继续进入 OCC Sweep Region 自动分解"));
        return out;
    }

    const FaceInfo& a = planarFaces[std::size_t(bestA)];
    const FaceInfo& b = planarFaces[std::size_t(bestB)];
    const QVector3D translation{
        static_cast<float>(bestTranslation.X()),
        static_cast<float>(bestTranslation.Y()),
        static_cast<float>(bestTranslation.Z())};

    out.insert(QStringLiteral("sweepable"), true);
    out.insert(QStringLiteral("sourceFaceId"), a.globalId);
    out.insert(QStringLiteral("targetFaceId"), b.globalId);
    out.insert(QStringLiteral("translation"), QVariant::fromValue(translation));
    out.insert(QStringLiteral("profileVertexCount"), int(a.vertices.size()));
    out.insert(QStringLiteral("sourceArea"), a.area);
    out.insert(QStringLiteral("targetArea"), b.area);
    out.insert(QStringLiteral("confidence"), 1.0);
    out.insert(QStringLiteral("message"),
        QStringLiteral("OCC 检测到严格平移 Sweep：Source Face #%1 → Target Face #%2，截面顶点=%3，平移=(%4,%5,%6)")
            .arg(a.globalId).arg(b.globalId).arg(a.vertices.size())
            .arg(translation.x(), 0, 'g', 8)
            .arg(translation.y(), 0, 'g', 8)
            .arg(translation.z(), 0, 'g', 8));
    return out;
}


QVariantMap analyzeTranslationalSweepShape(const TopoDS_Shape& inputSolid)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("sweepable"), false);
    out.insert(QStringLiteral("candidateCount"), 0);
    out.insert(QStringLiteral("ambiguous"), false);
    out.insert(QStringLiteral("candidates"), QVariantList{});

    if (inputSolid.IsNull())
    {
        out.insert(QStringLiteral("message"), QStringLiteral("实体为空，无法进行 Sweep 自动分析。"));
        return out;
    }

    TopoDS_Shape solid = inputSolid;
    if (solid.ShapeType() != TopAbs_SOLID)
    {
        for (TopExp_Explorer ex(inputSolid, TopAbs_SOLID); ex.More(); ex.Next())
        {
            solid = ex.Current();
            break;
        }
    }
    if (solid.IsNull() || solid.ShapeType() != TopAbs_SOLID)
    {
        out.insert(QStringLiteral("message"), QStringLiteral("所选对象不是可分析的 OCC Solid。"));
        return out;
    }

    Bnd_Box box;
    BRepBndLib::Add(solid, box);
    if (box.IsVoid())
    {
        out.insert(QStringLiteral("message"), QStringLiteral("无法取得实体包围盒。"));
        return out;
    }
    Standard_Real xmin=0.0, ymin=0.0, zmin=0.0, xmax=0.0, ymax=0.0, zmax=0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = std::max(0.0, double(xmax - xmin));
    const double dy = std::max(0.0, double(ymax - ymin));
    const double dz = std::max(0.0, double(zmax - zmin));
    const double diag = std::max(1.0e-12, std::sqrt(dx*dx + dy*dy + dz*dz));
    const double tol = std::max({double(Precision::Confusion()) * 25.0, diag * 2.0e-6, 1.0e-9});
    const double tol2 = tol * tol;
    constexpr double kParallel = 0.9995;

    // V8.5: Source/Target 只相互平移匹配仍然不够。对于带孔栅格/薄板，
    // 某些侧面也可能成对平移对应，但用它们做 Source/Target 会把整个体
    // 沿错误方向重新拉伸。严格平移 Sweep 必须同时满足：
    //     Volume ~= SourceArea * SweepDistance
    // 这是全局体积守恒检查，不需要分析 Linking Faces，而且代价非常低。
    GProp_GProps solidVolumeProps;
    BRepGProp::VolumeProperties(solid, solidVolumeProps);
    const double solidVolume = std::abs(solidVolumeProps.Mass());

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(solid, TopAbs_FACE, faces);
    if (faces.Extent() < 3)
    {
        out.insert(QStringLiteral("valid"), true);
        out.insert(QStringLiteral("message"), QStringLiteral("边界面少于 3 个，不能形成普通三维平移 Sweep。"));
        return out;
    }

    auto edgeLength = [](const TopoDS_Edge& edge)
    {
        GProp_GProps props;
        BRepGProp::LinearProperties(edge, props);
        return std::max(0.0, props.Mass());
    };

    auto sampleEdge = [](const TopoDS_Edge& edge, int count)
    {
        std::vector<gp_Pnt> pts;
        try
        {
            BRepAdaptor_Curve curve(edge);
            const double first = curve.FirstParameter();
            const double last = curve.LastParameter();
            if (!std::isfinite(first) || !std::isfinite(last) || !(last > first))
                return pts;
            const int n = std::max(4, count);
            pts.reserve(std::size_t(n));
            for (int i = 0; i < n; ++i)
            {
                // 不取最后一个参数，避免闭合圆边首尾重复导致匹配退化。
                const double u = first + (last - first) * (double(i) / double(n));
                pts.push_back(curve.Value(u));
            }
        }
        catch (...)
        {
            pts.clear();
        }
        return pts;
    };

    auto sqDist = [](const gp_Pnt& a, const gp_Pnt& b)
    {
        const double x = a.X() - b.X();
        const double y = a.Y() - b.Y();
        const double z = a.Z() - b.Z();
        return x*x + y*y + z*z;
    };

    auto boundaryEdges = [](const TopoDS_Face& face)
    {
        std::vector<TopoDS_Edge> edges;
        TopTools_IndexedMapOfShape map;
        TopExp::MapShapes(face, TopAbs_EDGE, map);
        edges.reserve(std::size_t(std::max(0, map.Extent())));
        for (int i = 1; i <= map.Extent(); ++i)
            edges.push_back(TopoDS::Edge(map(i)));
        return edges;
    };

    auto translatedEdgeMatches = [&](const TopoDS_Edge& source,
                                      const TopoDS_Edge& target,
                                      const gp_Vec& tr) -> bool
    {
        const double la = edgeLength(source);
        const double lb = edgeLength(target);
        const double lscale = std::max({la, lb, tol});
        if (!(la > 0.0) || !(lb > 0.0) || std::abs(la - lb) > lscale * 2.0e-3)
            return false;

        try
        {
            BRepAdaptor_Curve ca(source), cb(target);
            if (ca.GetType() == GeomAbs_Line && cb.GetType() == GeomAbs_Line)
            {
                const gp_Pnt a0 = ca.Value(ca.FirstParameter()).Translated(tr);
                const gp_Pnt a1 = ca.Value(ca.LastParameter()).Translated(tr);
                const gp_Pnt b0 = cb.Value(cb.FirstParameter());
                const gp_Pnt b1 = cb.Value(cb.LastParameter());
                const double endpointTol = tol * 4.0;
                return (a0.Distance(b0) <= endpointTol && a1.Distance(b1) <= endpointTol) ||
                       (a0.Distance(b1) <= endpointTol && a1.Distance(b0) <= endpointTol);
            }
            if (ca.GetType() == GeomAbs_Circle && cb.GetType() == GeomAbs_Circle)
            {
                const gp_Circ aa = ca.Circle();
                const gp_Circ bb = cb.Circle();
                const gp_Pnt expected = aa.Location().Translated(tr);
                return expected.Distance(bb.Location()) <= tol * 4.0 &&
                       std::abs(aa.Radius() - bb.Radius()) <= std::max(tol * 4.0, aa.Radius() * 2.0e-6);
            }
            if (ca.GetType() == GeomAbs_Ellipse && cb.GetType() == GeomAbs_Ellipse)
            {
                const gp_Elips aa = ca.Ellipse();
                const gp_Elips bb = cb.Ellipse();
                const gp_Pnt expected = aa.Location().Translated(tr);
                return expected.Distance(bb.Location()) <= tol * 4.0 &&
                       std::abs(aa.MajorRadius() - bb.MajorRadius()) <= std::max(tol * 4.0, aa.MajorRadius() * 2.0e-6) &&
                       std::abs(aa.MinorRadius() - bb.MinorRadius()) <= std::max(tol * 4.0, aa.MinorRadius() * 2.0e-6);
            }
            // 其它修剪曲线继续用实际几何采样，不依赖 STEP 暴露出的曲线类型名称。
        }
        catch (...) {}

        const auto aPts = sampleEdge(source, 24);
        const auto bPts = sampleEdge(target, 24);
        if (aPts.empty() || bPts.empty())
            return false;

        // 对闭合圆/椭圆允许不同 seam 参数起点：用点集最近距离，而不是要求参数一一对应。
        for (const gp_Pnt& p : aPts)
        {
            const gp_Pnt expected = p.Translated(tr);
            double best = std::numeric_limits<double>::max();
            for (const gp_Pnt& q : bPts)
                best = std::min(best, sqDist(expected, q));
            if (best > tol2 * 16.0)
                return false;
        }
        for (const gp_Pnt& q : bPts)
        {
            double best = std::numeric_limits<double>::max();
            for (const gp_Pnt& p : aPts)
                best = std::min(best, sqDist(p.Translated(tr), q));
            if (best > tol2 * 16.0)
                return false;
        }
        return true;
    };

    auto translatedBoundaryMatches = [&](const TopoDS_Face& source,
                                          const TopoDS_Face& target,
                                          const gp_Vec& tr) -> bool
    {
        const auto aEdges = boundaryEdges(source);
        const auto bEdges = boundaryEdges(target);
        if (aEdges.empty() || bEdges.empty())
            return false;

        // 快速路径：规则拉伸体（圆柱、长方体、棱柱等）通常两端边界 Edge
        // 可以直接按真实曲线几何匹配。这里完全不做 BRepExtrema 点到 Shape 距离，
        // 对上百个重复零件会比旧逻辑快几个数量级。
        // 若 STEP seam/BooleanFragments 把一条几何边拆成多段，快速路径失败后
        // 再进入下面的稳健 fallback，因此不会牺牲 V8.2 对坏拓扑的兼容。
        struct EdgeQuickInfo
        {
            TopoDS_Edge edge;
            gp_Pnt center;
            double length = 0.0;
            bool valid = false;
        };
        auto makeEdgeQuickInfo = [&](const TopoDS_Edge& e) -> EdgeQuickInfo
        {
            EdgeQuickInfo info;
            info.edge = e;
            try
            {
                GProp_GProps props;
                BRepGProp::LinearProperties(e, props);
                info.length = std::max(0.0, props.Mass());
                info.center = props.CentreOfMass();
                info.valid = info.length > 0.0;
            }
            catch (...) { info.valid = false; }
            return info;
        };

        // V8.6：批量栅格/多孔板的端面可能包含数百条 Edge。旧代码会对
        // Source 的每条 Edge 与 Target 的所有 Edge 都执行 BRepAdaptor/GProp，
        // 复杂截面会退化成很重的 O(E^2) 几何调用。现在先用“边质心平移后位置
        // + 边长”做极便宜的候选过滤，只对极少数近邻 Edge 做精确曲线匹配；
        // 若 STEP 把边拆碎导致快速路径失败，后面的整体边界 fallback 仍然保留。
        auto everyEdgeHasGeometricMate = [&](const std::vector<TopoDS_Edge>& from,
                                              const std::vector<TopoDS_Edge>& to,
                                              const gp_Vec& move) -> bool
        {
            std::vector<EdgeQuickInfo> toInfo;
            toInfo.reserve(to.size());
            for (const TopoDS_Edge& e : to) toInfo.push_back(makeEdgeQuickInfo(e));

            const double centerTol = tol * 8.0;
            const double centerTol2 = centerTol * centerTol;
            for (const TopoDS_Edge& e : from)
            {
                const EdgeQuickInfo src = makeEdgeQuickInfo(e);
                if (!src.valid) return false;
                const gp_Pnt expectedCenter = src.center.Translated(move);
                bool matched = false;
                for (const EdgeQuickInfo& candidate : toInfo)
                {
                    if (!candidate.valid) continue;
                    const double lscale = std::max({src.length, candidate.length, tol});
                    if (std::abs(src.length - candidate.length) > lscale * 2.0e-3) continue;
                    if (sqDist(expectedCenter, candidate.center) > centerTol2) continue;
                    if (translatedEdgeMatches(e, candidate.edge, move))
                    {
                        matched = true;
                        break;
                    }
                }
                if (!matched) return false;
            }
            return true;
        };
        const gp_Vec reverseMove(-tr.X(), -tr.Y(), -tr.Z());
        if (everyEdgeHasGeometricMate(aEdges, bEdges, tr) &&
            everyEdgeHasGeometricMate(bEdges, aEdges, reverseMove))
            return true;

        // STEP/OCC BooleanFragments 经常会把同一条几何边拆成多条拓扑 Edge。
        // 因此这里不再要求 Source/Target 的 Edge 数量完全相同，也不分析 Linking Faces。
        // 只验证“Source 边界整体平移后是否落在 Target 的真实 OCC 边界上”，反向再验证一次。
        BRep_Builder builder;
        TopoDS_Compound aBoundary, bBoundary;
        builder.MakeCompound(aBoundary);
        builder.MakeCompound(bBoundary);
        for (const TopoDS_Edge& e : aEdges) builder.Add(aBoundary, e);
        for (const TopoDS_Edge& e : bEdges) builder.Add(bBoundary, e);

        auto pointDistanceToShape = [](const gp_Pnt& p, const TopoDS_Shape& shape) -> double
        {
            try
            {
                const TopoDS_Vertex v = BRepBuilderAPI_MakeVertex(p).Vertex();
                BRepExtrema_DistShapeShape dist(v, shape);
                dist.Perform();
                if (dist.IsDone() && dist.NbSolution() > 0)
                    return dist.Value();
            }
            catch (...) {}
            return std::numeric_limits<double>::max();
        };

        const double matchTol = tol * 12.0;
        for (const TopoDS_Edge& e : aEdges)
        {
            const auto pts = sampleEdge(e, 20);
            if (pts.empty()) return false;
            for (const gp_Pnt& p : pts)
            {
                if (pointDistanceToShape(p.Translated(tr), bBoundary) > matchTol)
                    return false;
            }
        }

        const gp_Vec back(-tr.X(), -tr.Y(), -tr.Z());
        for (const TopoDS_Edge& e : bEdges)
        {
            const auto pts = sampleEdge(e, 20);
            if (pts.empty()) return false;
            for (const gp_Pnt& p : pts)
            {
                if (pointDistanceToShape(p.Translated(back), aBoundary) > matchTol)
                    return false;
            }
        }
        return true;
    };

    struct PlaneFace
    {
        int localId = -1;
        TopoDS_Face face;
        gp_Pnt center;
        gp_Dir normal;
        double area = 0.0;
        std::vector<TopoDS_Edge> edges;
    };

    std::vector<PlaneFace> planar;
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const TopoDS_Face face = TopoDS::Face(faces(i));
        try
        {
            BRepAdaptor_Surface adaptor(face, true);
            if (adaptor.GetType() != GeomAbs_Plane)
                continue;
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            const double area = std::max(0.0, props.Mass());
            if (!(area > Precision::Confusion()))
                continue;
            PlaneFace f;
            f.localId = i;
            f.face = face;
            f.center = props.CentreOfMass();
            f.normal = adaptor.Plane().Axis().Direction();
            f.area = area;
            f.edges = boundaryEdges(face);
            if (!f.edges.empty())
                planar.push_back(std::move(f));
        }
        catch (...) {}
    }

    out.insert(QStringLiteral("valid"), true);
    if (planar.size() < 2)
    {
        out.insert(QStringLiteral("message"), QStringLiteral("没有找到两张可作为 Source/Target 的平面端面。"));
        return out;
    }

    QVariantList candidates;

    // V8.4 批量优化：Source/Target 必须位于同一法向族的两个最外层平面。
    // 旧代码对所有平面 Face 做 O(F^2) 两两比较，并在很多候选上进入昂贵的
    // BRepExtrema fallback；栅格/多孔板拥有大量平面侧壁时会非常慢。
    // 对一个严格直线 Sweep Solid，真正的两个端盖一定是该扫掠法向上的
    // min/max 外层面，因此先按法向分桶，只比较两端极值带中的 Face。
    struct NormalBucketKey
    {
        int x = 0, y = 0, z = 0;
        bool operator<(const NormalBucketKey& o) const
        {
            if (x != o.x) return x < o.x;
            if (y != o.y) return y < o.y;
            return z < o.z;
        }
    };
    struct BucketFace
    {
        int planarIndex = -1;
        double projection = 0.0;
    };

    auto canonicalNormal = [](const gp_Dir& in) -> gp_Dir
    {
        double x = in.X(), y = in.Y(), z = in.Z();
        const double a[3] = {std::abs(x), std::abs(y), std::abs(z)};
        int d = 0;
        if (a[1] > a[d]) d = 1;
        if (a[2] > a[d]) d = 2;
        const double dominant = d == 0 ? x : (d == 1 ? y : z);
        if (dominant < 0.0) { x = -x; y = -y; z = -z; }
        return gp_Dir(x, y, z);
    };
    auto normalKey = [&](const gp_Dir& n) -> NormalBucketKey
    {
        const gp_Dir c = canonicalNormal(n);
        constexpr double q = 1000000.0; // ~1e-6 normal quantization
        return { int(std::llround(c.X() * q)), int(std::llround(c.Y() * q)), int(std::llround(c.Z() * q)) };
    };

    std::map<NormalBucketKey, std::vector<BucketFace>> buckets;
    for (int i = 0; i < int(planar.size()); ++i)
    {
        const gp_Dir n = canonicalNormal(planar[std::size_t(i)].normal);
        const gp_Pnt& c = planar[std::size_t(i)].center;
        const double projection = c.X()*n.X() + c.Y()*n.Y() + c.Z()*n.Z();
        buckets[normalKey(n)].push_back({i, projection});
    }

    for (auto& kv : buckets)
    {
        auto& bf = kv.second;
        if (bf.size() < 2) continue;
        std::sort(bf.begin(), bf.end(), [](const BucketFace& a, const BucketFace& b)
        {
            return a.projection < b.projection;
        });
        const double pMin = bf.front().projection;
        const double pMax = bf.back().projection;
        const double span = pMax - pMin;
        if (!(span > tol)) continue;
        const double extremeTol = std::max(tol * 20.0, std::abs(span) * 1.0e-8);

        std::vector<int> low, high;
        for (const BucketFace& f : bf)
        {
            if (std::abs(f.projection - pMin) <= extremeTol) low.push_back(f.planarIndex);
            if (std::abs(f.projection - pMax) <= extremeTol) high.push_back(f.planarIndex);
        }

        for (int ia : low)
        {
            for (int ib : high)
            {
                if (ia == ib) continue;
                PlaneFace a = planar[std::size_t(ia)];
                PlaneFace b = planar[std::size_t(ib)];

                gp_Vec tr(a.center, b.center);
                double distance = tr.Magnitude();
                if (!(distance > tol)) continue;
                gp_Dir dir(tr);
                if (std::abs(a.normal.Dot(dir)) < kParallel) continue;

                const double areaScale = std::max({a.area, b.area, tol * tol});
                if (std::abs(a.area - b.area) > areaScale * 2.0e-3) continue;
                if (!translatedBoundaryMatches(a.face, b.face, tr)) continue;

                // V8.5 全局 Sweep 一致性：一个严格平移拉伸体的体积必须等于
                // 端面净面积（含孔洞扣除）乘扫掠距离。这样可直接排除“侧壁恰好
                // 也有一对相同平面，但并不是整个实体端盖”的假候选。
                const double meanCapArea = 0.5 * (a.area + b.area);
                const double predictedVolume = meanCapArea * distance;
                const double volumeScale = std::max({solidVolume, predictedVolume, tol * tol * tol});
                const double sweepVolumeRelError = std::abs(predictedVolume - solidVolume) / volumeScale;
                constexpr double kSweepVolumeRelTol = 5.0e-3; // 0.5%，兼容 STEP/OCC 数值误差
                if (solidVolume > tol * tol * tol && sweepVolumeRelError > kSweepVolumeRelTol)
                    continue;

                // 保证方向稳定：优先 +X/+Y/+Z，避免同一几何每次 Source/Target 翻转。
                const double comps[3] = {tr.X(), tr.Y(), tr.Z()};
                int dominant = 0;
                if (std::abs(comps[1]) > std::abs(comps[dominant])) dominant = 1;
                if (std::abs(comps[2]) > std::abs(comps[dominant])) dominant = 2;
                if (comps[dominant] < 0.0)
                {
                    std::swap(a, b);
                    tr.Reverse();
                    distance = tr.Magnitude();
                    dir = gp_Dir(tr);
                }

                QVariantMap c;
                c.insert(QStringLiteral("sourceLocalFaceIndex"), a.localId);
                c.insert(QStringLiteral("targetLocalFaceIndex"), b.localId);
                const QVector3D translation(float(tr.X()), float(tr.Y()), float(tr.Z()));
                const QVector3D direction(float(dir.X()), float(dir.Y()), float(dir.Z()));
                c.insert(QStringLiteral("translation"), QVariant::fromValue(translation));
                c.insert(QStringLiteral("direction"), QVariant::fromValue(direction));
                c.insert(QStringLiteral("distance"), distance);
                c.insert(QStringLiteral("sourceArea"), a.area);
                c.insert(QStringLiteral("targetArea"), b.area);
                c.insert(QStringLiteral("solidVolume"), solidVolume);
                c.insert(QStringLiteral("predictedSweepVolume"), predictedVolume);
                c.insert(QStringLiteral("sweepVolumeRelativeError"), sweepVolumeRelError);
                // score 越大越优：先看整体验证，再把扫掠长度作为同分候选的次要偏好。
                c.insert(QStringLiteral("score"), 1.0 / (1.0 + sweepVolumeRelError));
                candidates.push_back(c);
            }
        }
    }

    // 长方体可能有 3 组完全合法方向。排序先按“SourceArea*Distance 与真实 Volume”
    // 的一致性，再用距离作为完全等价候选的次级偏好。UI 仍可让用户改选。
    std::sort(candidates.begin(), candidates.end(), [](const QVariant& va, const QVariant& vb)
    {
        const QVariantMap a = va.toMap();
        const QVariantMap b = vb.toMap();
        const double ea = a.value(QStringLiteral("sweepVolumeRelativeError")).toDouble();
        const double eb = b.value(QStringLiteral("sweepVolumeRelativeError")).toDouble();
        if (std::abs(ea - eb) > 1.0e-12) return ea < eb;
        const double da = a.value(QStringLiteral("distance")).toDouble();
        const double db = b.value(QStringLiteral("distance")).toDouble();
        if (std::abs(da - db) > 1.0e-12) return da > db;
        return a.value(QStringLiteral("sourceLocalFaceIndex")).toInt() <
               b.value(QStringLiteral("sourceLocalFaceIndex")).toInt();
    });

    out.insert(QStringLiteral("candidates"), candidates);
    out.insert(QStringLiteral("candidateCount"), candidates.size());
    out.insert(QStringLiteral("sweepable"), !candidates.isEmpty());
    out.insert(QStringLiteral("ambiguous"), candidates.size() > 1);
    if (candidates.isEmpty())
        out.insert(QStringLiteral("message"), QStringLiteral("没有找到可靠的平移 Source/Target 端面候选。可切换旋转 Sweep，或使用手动 Source/Target 兜底。"));
    else if (candidates.size() == 1)
        out.insert(QStringLiteral("message"), QStringLiteral("检测到 1 组平移匹配的 Source/Target 端面候选，方向和距离可自动确定。"));
    else
        out.insert(QStringLiteral("message"), QStringLiteral("检测到 %1 组合法平移 Sweep 方向，请从候选中选择，不自动猜。") .arg(candidates.size()));
    return out;
}

QVariantMap analyzeTranslationalSweepVolume(const QStringList& geometryUuids, int volumeUiId)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("sweepable"), false);
    out.insert(QStringLiteral("candidateCount"), 0);
    if (volumeUiId <= 0)
    {
        out.insert(QStringLiteral("message"), QStringLiteral("没有选择 CAD Volume。"));
        return out;
    }

    TopoDS_Shape solid;
    if (!entityShape(geometryUuids, 3, volumeUiId, solid) || solid.IsNull())
    {
        out.insert(QStringLiteral("message"), QStringLiteral("无法读取 CAD Volume #%1。") .arg(volumeUiId));
        return out;
    }

    QVariantMap local = analyzeTranslationalSweepShape(solid);
    if (!local.value(QStringLiteral("valid")).toBool())
        return local;

    TopTools_IndexedMapOfShape localFaces;
    TopExp::MapShapes(solid, TopAbs_FACE, localFaces);
    TopTools_IndexedMapOfShape globalFaces;
    buildIndexedShapesFromGeometry(geometryUuids, 2, globalFaces);
    auto globalFaceId = [&](const TopoDS_Shape& face) -> int
    {
        const int direct = globalFaces.FindIndex(face);
        if (direct > 0) return direct;
        for (int i = 1; i <= globalFaces.Extent(); ++i)
            if (globalFaces(i).IsSame(face)) return i;
        return -1;
    };
    QVariantList mapped;
    const QVariantList candidates = local.value(QStringLiteral("candidates")).toList();
    for (const QVariant& v : candidates)
    {
        QVariantMap c = v.toMap();
        const int sourceLocal = c.value(QStringLiteral("sourceLocalFaceIndex")).toInt();
        const int targetLocal = c.value(QStringLiteral("targetLocalFaceIndex")).toInt();
        if (sourceLocal <= 0 || targetLocal <= 0 ||
            sourceLocal > localFaces.Extent() || targetLocal > localFaces.Extent())
            continue;

        const int sourceGlobal = globalFaceId(localFaces(sourceLocal));
        const int targetGlobal = globalFaceId(localFaces(targetLocal));
        if (sourceGlobal <= 0 || targetGlobal <= 0)
            continue;

        c.insert(QStringLiteral("source"), sourceGlobal);
        c.insert(QStringLiteral("target"), targetGlobal);
        const QVector3D dir = c.value(QStringLiteral("direction")).value<QVector3D>();
        const double volErrPct = 100.0 * c.value(QStringLiteral("sweepVolumeRelativeError")).toDouble();
        c.insert(QStringLiteral("label"),
            QStringLiteral("F%1 → F%2；方向=(%3,%4,%5)，距离=%6，体积一致性误差=%7%")
                .arg(sourceGlobal).arg(targetGlobal)
                .arg(dir.x(), 0, 'g', 4).arg(dir.y(), 0, 'g', 4).arg(dir.z(), 0, 'g', 4)
                .arg(c.value(QStringLiteral("distance")).toDouble(), 0, 'g', 8)
                .arg(volErrPct, 0, 'g', 4));
        mapped.push_back(c);
    }

    out = local;
    out.insert(QStringLiteral("valid"), true);
    out.insert(QStringLiteral("volumeUiId"), volumeUiId);
    out.insert(QStringLiteral("candidates"), mapped);
    out.insert(QStringLiteral("candidateCount"), mapped.size());
    out.insert(QStringLiteral("sweepable"), !mapped.isEmpty());
    out.insert(QStringLiteral("ambiguous"), mapped.size() > 1);
    if (mapped.isEmpty())
        out.insert(QStringLiteral("message"), QStringLiteral("未找到可映射到当前 CAD UI Face 的平移 Source/Target 候选。"));
    else if (mapped.size() == 1)
        out.insert(QStringLiteral("message"), QStringLiteral("Volume %1：已自动确定 Source、Target、扫掠方向和距离。")
            .arg(volumeUiId));
    else
        out.insert(QStringLiteral("message"), QStringLiteral("Volume %1：检测到 %2 组合法 Sweep 方向，请选择候选。")
            .arg(volumeUiId).arg(mapped.size()));
    return out;
}


QVariantList analyzeTranslationalSweepVolumes(const QStringList& geometryUuids,
                                               const QList<int>& volumeUiIds)
{
    QVariantList out;
    if (geometryUuids.isEmpty() || volumeUiIds.isEmpty())
        return out;

    // 批量分析的关键优化：全局 Solid/Face 索引只建立一次。
    // 旧实现每分析一个 Volume 都重新把整个 STEP 装配拼成 Compound 再 MapShapes，
    // 150 个零件会退化成近似 O(N^2) 的重复 OCC 拓扑扫描。
    TopTools_IndexedMapOfShape globalSolids;
    TopTools_IndexedMapOfShape globalFaces;
    if (!buildIndexedShapesFromGeometry(geometryUuids, 3, globalSolids) ||
        !buildIndexedShapesFromGeometry(geometryUuids, 2, globalFaces))
        return out;

    auto globalFaceId = [&](const TopoDS_Shape& face) -> int
    {
        const int direct = globalFaces.FindIndex(face);
        if (direct > 0) return direct;
        // 极少数 STEP/OCC Location/Orientation 差异下保留 IsSame fallback。
        for (int i = 1; i <= globalFaces.Extent(); ++i)
            if (globalFaces(i).IsSame(face)) return i;
        return -1;
    };

    out.reserve(volumeUiIds.size());
    for (int volumeUiId : volumeUiIds)
    {
        QVariantMap result;
        result.insert(QStringLiteral("valid"), false);
        result.insert(QStringLiteral("sweepable"), false);
        result.insert(QStringLiteral("candidateCount"), 0);
        result.insert(QStringLiteral("volumeUiId"), volumeUiId);

        if (volumeUiId <= 0 || volumeUiId > globalSolids.Extent())
        {
            result.insert(QStringLiteral("message"),
                          QStringLiteral("无法读取 CAD Volume #%1。").arg(volumeUiId));
            out.push_back(result);
            continue;
        }

        const TopoDS_Shape& solid = globalSolids(volumeUiId);
        QVariantMap local = analyzeTranslationalSweepShape(solid);
        if (!local.value(QStringLiteral("valid")).toBool())
        {
            local.insert(QStringLiteral("volumeUiId"), volumeUiId);
            out.push_back(local);
            continue;
        }

        TopTools_IndexedMapOfShape localFaces;
        TopExp::MapShapes(solid, TopAbs_FACE, localFaces);
        QVariantList mapped;
        const QVariantList candidates = local.value(QStringLiteral("candidates")).toList();
        mapped.reserve(candidates.size());
        for (const QVariant& v : candidates)
        {
            QVariantMap c = v.toMap();
            const int sourceLocal = c.value(QStringLiteral("sourceLocalFaceIndex")).toInt();
            const int targetLocal = c.value(QStringLiteral("targetLocalFaceIndex")).toInt();
            if (sourceLocal <= 0 || targetLocal <= 0 ||
                sourceLocal > localFaces.Extent() || targetLocal > localFaces.Extent())
                continue;

            const int sourceGlobal = globalFaceId(localFaces(sourceLocal));
            const int targetGlobal = globalFaceId(localFaces(targetLocal));
            if (sourceGlobal <= 0 || targetGlobal <= 0)
                continue;

            c.insert(QStringLiteral("source"), sourceGlobal);
            c.insert(QStringLiteral("target"), targetGlobal);
            const QVector3D dir = c.value(QStringLiteral("direction")).value<QVector3D>();
            const double volErrPct = 100.0 * c.value(QStringLiteral("sweepVolumeRelativeError")).toDouble();
            c.insert(QStringLiteral("label"),
                QStringLiteral("F%1 → F%2；方向=(%3,%4,%5)，距离=%6，体积一致性误差=%7%")
                    .arg(sourceGlobal).arg(targetGlobal)
                    .arg(dir.x(), 0, 'g', 4).arg(dir.y(), 0, 'g', 4).arg(dir.z(), 0, 'g', 4)
                    .arg(c.value(QStringLiteral("distance")).toDouble(), 0, 'g', 8)
                    .arg(volErrPct, 0, 'g', 4));
            mapped.push_back(c);
        }

        result = local;
        result.insert(QStringLiteral("valid"), true);
        result.insert(QStringLiteral("volumeUiId"), volumeUiId);
        result.insert(QStringLiteral("candidates"), mapped);
        result.insert(QStringLiteral("candidateCount"), mapped.size());
        result.insert(QStringLiteral("sweepable"), !mapped.isEmpty());
        result.insert(QStringLiteral("ambiguous"), mapped.size() > 1);
        if (mapped.isEmpty())
            result.insert(QStringLiteral("message"),
                          QStringLiteral("未找到可映射到当前 CAD UI Face 的平移 Source/Target 候选。"));
        else if (mapped.size() == 1)
            result.insert(QStringLiteral("message"),
                          QStringLiteral("Volume %1：已自动确定 Source、Target、扫掠方向和距离。").arg(volumeUiId));
        else
            result.insert(QStringLiteral("message"),
                          QStringLiteral("Volume %1：检测到 %2 组合法 Sweep 方向。").arg(volumeUiId).arg(mapped.size()));
        out.push_back(result);
    }
    return out;
}

QVariantMap exportPolycubeSurfaceObj(const QStringList& geometryUuids,
                                     const QString& objPath,
                                     const QString& faceMapPath)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);

    if (geometryUuids.isEmpty())
    {
        out.insert(QStringLiteral("message"), QStringLiteral("当前没有可导出的 OCC 几何。"));
        return out;
    }
    if (objPath.trimmed().isEmpty() || faceMapPath.trimmed().isEmpty())
    {
        out.insert(QStringLiteral("message"), QStringLiteral("Polycube OBJ/FaceMap 输出路径为空。"));
        return out;
    }

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        out.insert(QStringLiteral("message"), QStringLiteral("Geometry manager 不可用。"));
        return out;
    }

    try
    {
        BRep_Builder builder;
        TopoDS_Compound compound;
        builder.MakeCompound(compound);
        bool hasShape = false;
        for (const QString& uuid : geometryUuids)
        {
            GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;
            builder.Add(compound, *obj->getShape());
            hasShape = true;
        }
        if (!hasShape)
        {
            out.insert(QStringLiteral("message"), QStringLiteral("当前活动几何没有有效 OCC Shape。"));
            return out;
        }

        Bnd_Box box;
        BRepBndLib::Add(compound, box);
        if (box.IsVoid())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("无法取得 OCC 几何包围盒。"));
            return out;
        }
        Standard_Real xmin=0.0, ymin=0.0, zmin=0.0, xmax=0.0, ymax=0.0, zmax=0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double dx = double(xmax - xmin);
        const double dy = double(ymax - ymin);
        const double dz = double(zmax - zmin);
        const double diag = std::sqrt(dx*dx + dy*dy + dz*dz);
        if (!std::isfinite(diag) || diag <= Precision::Confusion())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("OCC 几何尺寸无效，无法生成 Polycube 表面。"));
            return out;
        }

        // Polycube only needs a faithful analysis surface, not the render tessellation.
        // Keep enough curvature resolution while avoiding an unnecessarily huge OBJ.
        const double linearDeflection = std::max(diag * 0.0020, double(Precision::Confusion()) * 20.0);
        const double angularDeflection = 0.20; // radians, ~11.5 degrees
        // Constructor performs the incremental tessellation in the same way as the
        // existing CAD presentation path. We validate every Face triangulation below
        // instead of relying on version-specific mesher status APIs.
        BRepMesh_IncrementalMesh mesher(compound, linearDeflection, Standard_False,
                                        angularDeflection, Standard_True);
        (void)mesher;

        TopTools_IndexedMapOfShape globalFaces;
        TopExp::MapShapes(compound, TopAbs_FACE, globalFaces);
        if (globalFaces.Extent() <= 0)
        {
            out.insert(QStringLiteral("message"), QStringLiteral("OCC 几何没有可导出的 Face。"));
            return out;
        }

        struct P3 { double x=0.0, y=0.0, z=0.0; };
        struct Tri { int a=0, b=0, c=0, faceId=0; };
        std::vector<P3> vertices;
        std::vector<Tri> triangles;

        // Different OCC Faces usually own separate Poly_Triangulation nodes even on a
        // shared CAD Edge. Native Polycube requires a closed 2-manifold by OBJ index,
        // so weld coincident world-space nodes before writing faces.
        const double weldTol = std::max(double(Precision::Confusion()) * 5.0, diag * 1.0e-9);
        const double invWeld = 1.0 / weldTol;
        using WeldKey = std::tuple<long long,long long,long long>;
        std::map<WeldKey, std::vector<int>> buckets;

        auto vertexId = [&](const gp_Pnt& pnt) -> int
        {
            const double x = pnt.X(), y = pnt.Y(), z = pnt.Z();
            const long long qx = (long long)std::llround(x * invWeld);
            const long long qy = (long long)std::llround(y * invWeld);
            const long long qz = (long long)std::llround(z * invWeld);

            // Probe neighboring quantization buckets as well: a point lying just on
            // opposite sides of a rounding boundary must still weld to the same CAD node.
            for (int ix=-1; ix<=1; ++ix)
            for (int iy=-1; iy<=1; ++iy)
            for (int iz=-1; iz<=1; ++iz)
            {
                const WeldKey key{qx+ix, qy+iy, qz+iz};
                auto it = buckets.find(key);
                if (it == buckets.end()) continue;
                for (int id : it->second)
                {
                    const P3& v = vertices[size_t(id-1)];
                    const double ex=x-v.x, ey=y-v.y, ez=z-v.z;
                    if (ex*ex + ey*ey + ez*ez <= weldTol*weldTol)
                        return id;
                }
            }

            const int id = int(vertices.size()) + 1; // OBJ is 1-based
            vertices.push_back(P3{x,y,z});
            buckets[WeldKey{qx,qy,qz}].push_back(id);
            return id;
        };

        for (int faceId = 1; faceId <= globalFaces.Extent(); ++faceId)
        {
            const TopoDS_Face face = TopoDS::Face(globalFaces(faceId));
            TopLoc_Location loc;
            const Handle(Poly_Triangulation) tri = BRep_Tool::Triangulation(face, loc);
            if (tri.IsNull() || tri->NbTriangles() <= 0)
                continue;

            const gp_Trsf trsf = loc.Transformation();
            const bool reversed = face.Orientation() == TopAbs_REVERSED;
            for (int ti = 1; ti <= tri->NbTriangles(); ++ti)
            {
                Standard_Integer n1=0, n2=0, n3=0;
                tri->Triangle(ti).Get(n1,n2,n3);
                if (reversed) std::swap(n2,n3);

                gp_Pnt p1 = tri->Node(n1); p1.Transform(trsf);
                gp_Pnt p2 = tri->Node(n2); p2.Transform(trsf);
                gp_Pnt p3 = tri->Node(n3); p3.Transform(trsf);

                const gp_Vec e1(p1,p2), e2(p1,p3);
                if (e1.Crossed(e2).SquareMagnitude() <= weldTol*weldTol*weldTol*weldTol)
                    continue;

                const int a=vertexId(p1), b=vertexId(p2), c=vertexId(p3);
                if (a==b || b==c || c==a) continue;
                triangles.push_back(Tri{a,b,c,faceId});
            }
        }

        if (vertices.empty() || triangles.empty())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("OCC 三角化没有生成有效三角面。"));
            return out;
        }

        // Validate the exact topology Native Polycube will see after OBJ welding.
        using EdgeKey = std::pair<int,int>;
        std::map<EdgeKey,int> edgeUse;
        for (const Tri& t : triangles)
        {
            const int ids[3]={t.a,t.b,t.c};
            for (int e=0;e<3;++e)
            {
                int a=ids[e], b=ids[(e+1)%3];
                if (a>b) std::swap(a,b);
                ++edgeUse[EdgeKey{a,b}];
            }
        }
        int openEdges=0, nonManifoldEdges=0;
        for (const auto& kv : edgeUse)
        {
            if (kv.second == 1) ++openEdges;
            else if (kv.second != 2) ++nonManifoldEdges;
        }
        if (openEdges > 0 || nonManifoldEdges > 0)
        {
            out.insert(QStringLiteral("openEdgeCount"), openEdges);
            out.insert(QStringLiteral("nonManifoldEdgeCount"), nonManifoldEdges);
            out.insert(QStringLiteral("message"),
                       QStringLiteral("导出的 CAD 表面不是闭合 2-manifold：openEdge=%1，nonManifoldEdge=%2。Polycube Blocking 需要闭合实体边界。")
                           .arg(openEdges).arg(nonManifoldEdges));
            return out;
        }

        QFileInfo objInfo(objPath);
        if (!objInfo.absoluteDir().exists() && !objInfo.absoluteDir().mkpath(QStringLiteral(".")))
        {
            out.insert(QStringLiteral("message"), QStringLiteral("无法创建 Polycube OBJ 输出目录。"));
            return out;
        }
        QFileInfo mapInfo(faceMapPath);
        if (!mapInfo.absoluteDir().exists() && !mapInfo.absoluteDir().mkpath(QStringLiteral(".")))
        {
            out.insert(QStringLiteral("message"), QStringLiteral("无法创建 Polycube FaceMap 输出目录。"));
            return out;
        }

        QSaveFile objFile(objPath);
        if (!objFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            out.insert(QStringLiteral("message"), QStringLiteral("无法写入 Polycube OBJ：%1").arg(objPath));
            return out;
        }
        QTextStream os(&objFile);
        os.setRealNumberNotation(QTextStream::ScientificNotation);
        os.setRealNumberPrecision(17);
        os << "# POWERCHINA-GEOCAE Polycube analysis surface\n";
        os << "# vertices " << vertices.size() << " triangles " << triangles.size() << "\n";
        for (const P3& v : vertices)
            os << "v " << v.x << ' ' << v.y << ' ' << v.z << '\n';
        for (const Tri& t : triangles)
            os << "f " << t.a << ' ' << t.b << ' ' << t.c << '\n';
        os.flush();
        if (os.status() != QTextStream::Ok || !objFile.commit())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("提交 Polycube OBJ 文件失败。"));
            return out;
        }

        QSaveFile mapFile(faceMapPath);
        if (!mapFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            QFile::remove(objPath);
            out.insert(QStringLiteral("message"), QStringLiteral("无法写入 Polycube FaceMap：%1").arg(faceMapPath));
            return out;
        }
        QTextStream ms(&mapFile);
        ms << "# triangle_id global_occ_face_id\n";
        for (size_t i=0; i<triangles.size(); ++i)
            ms << (i+1) << ' ' << triangles[i].faceId << '\n';
        ms.flush();
        if (ms.status() != QTextStream::Ok || !mapFile.commit())
        {
            QFile::remove(objPath);
            out.insert(QStringLiteral("message"), QStringLiteral("提交 Polycube FaceMap 文件失败。"));
            return out;
        }

        out.insert(QStringLiteral("valid"), true);
        out.insert(QStringLiteral("vertexCount"), int(vertices.size()));
        out.insert(QStringLiteral("triangleCount"), int(triangles.size()));
        out.insert(QStringLiteral("faceCount"), globalFaces.Extent());
        out.insert(QStringLiteral("openEdgeCount"), 0);
        out.insert(QStringLiteral("nonManifoldEdgeCount"), 0);
        out.insert(QStringLiteral("linearDeflection"), linearDeflection);
        out.insert(QStringLiteral("message"),
                   QStringLiteral("OCC Polycube OBJ 导出完成：V=%1，Tri=%2，CAD Face=%3。")
                       .arg(vertices.size()).arg(triangles.size()).arg(globalFaces.Extent()));
        return out;
    }
    catch (const Standard_Failure& failure)
    {
        out.insert(QStringLiteral("message"),
                   QStringLiteral("OCC 导出 Polycube OBJ 异常：%1")
                       .arg(failure.GetMessageString() ? QString::fromUtf8(failure.GetMessageString())
                                                      : QStringLiteral("Standard_Failure")));
        return out;
    }
    catch (const std::exception& error)
    {
        out.insert(QStringLiteral("message"),
                   QStringLiteral("导出 Polycube OBJ 异常：%1").arg(QString::fromUtf8(error.what())));
        return out;
    }
}

bool entityPlane(const QStringList& geometryUuids, int dim, int id, QVector3D& origin, QVector3D& normal)
{
    origin = QVector3D();
    normal = QVector3D();
    if (dim != 2)
        return false;

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, dim, id, target) || target.ShapeType() != TopAbs_FACE)
        return false;

    const TopoDS_Face face = TopoDS::Face(target);
    BRepAdaptor_Surface adaptor(face, true);
    if (adaptor.GetType() != GeomAbs_Plane)
        return false;

    GProp_GProps props;
    if (!addMassProperties(target, dim, props))
        return false;
    const gp_Pnt c = props.CentreOfMass();
    origin = QVector3D{static_cast<float>(c.X()), static_cast<float>(c.Y()), static_cast<float>(c.Z())};

    gp_Dir n = adaptor.Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    normal = QVector3D{static_cast<float>(n.X()), static_cast<float>(n.Y()), static_cast<float>(n.Z())};
    if (normal.lengthSquared() <= 1e-16f)
        return false;
    normal.normalize();
    return true;
}

QVector<TopoDS_Shape> splitTemporaryPartitionRegions(const QStringList& geometryUuids,
                                              const QVariantList& planes,
                                              QString* message,
                                              QVector<int>* ownerVolumeIds)
{
    QVector<TopoDS_Shape> regions;
    QVector<int> owners;
    if (message) message->clear();
    if (ownerVolumeIds) ownerVolumeIds->clear();

    TopTools_ListOfShape arguments;
    TopoDS_Compound sourceCompound;
    int originalCount = 0;
    if (!buildActiveSolidArguments(geometryUuids, arguments, sourceCompound, originalCount))
    {
        if (message) *message = QStringLiteral("当前没有可用于分块的实体");
        return regions;
    }

    TopTools_IndexedMapOfShape originalSolids;
    TopExp::MapShapes(sourceCompound, TopAbs_SOLID, originalSolids);
    originalCount = originalSolids.Extent();
    if (originalCount <= 0)
    {
        if (message) *message = QStringLiteral("当前没有可用于分块的实体");
        return regions;
    }

    if (planes.isEmpty())
    {
        for (int i = 1; i <= originalSolids.Extent(); ++i)
        {
            regions.push_back(originalSolids(i));
            owners.push_back(i);
        }
        if (ownerVolumeIds) *ownerVolumeIds = owners;
        if (message) *message = QStringLiteral("当前未设置切面");
        return regions;
    }

    Bnd_Box box;
    BRepBndLib::Add(sourceCompound, box);
    if (box.IsVoid())
    {
        if (message) *message = QStringLiteral("无法取得模型包围盒");
        return {};
    }

    double xmin = 0.0, ymin = 0.0, zmin = 0.0;
    double xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    const double dx = std::max(0.0, xmax - xmin);
    const double dy = std::max(0.0, ymax - ymin);
    const double dz = std::max(0.0, zmax - zmin);
    const double diag = std::max(1e-6, std::sqrt(dx * dx + dy * dy + dz * dz));
    const double half = std::max(1.0, diag * 2.5);

    // V8.12: a manual cutter belongs to one CAD solid.  This keeps a cutter for
    // one sweep body from silently slicing every other body in the model.  Old
    // projects have no targetVolumeUiId; <=0 intentionally keeps the old
    // all-solids behavior for compatibility.
    for (const QVariant& planeVar : planes)
    {
        const int target = planeVar.toMap().value(QStringLiteral("targetVolumeUiId"), -1).toInt();
        if (target > originalCount)
        {
            if (message) *message = QStringLiteral("切面绑定的目标实体 %1 已不存在，请重新选择实体。")
                .arg(target);
            return {};
        }
    }

    bool anySplit = false;
    int splitBodyCount = 0;
    for (int solidId = 1; solidId <= originalSolids.Extent(); ++solidId)
    {
        const TopoDS_Shape solid = originalSolids(solidId);
        TopTools_ListOfShape tools;
        for (const QVariant& planeVar : planes)
        {
            const QVariantMap plane = planeVar.toMap();
            const int target = plane.value(QStringLiteral("targetVolumeUiId"), -1).toInt();
            if (target > 0 && target != solidId)
                continue;

            QVector3D origin = plane.value(QStringLiteral("origin")).value<QVector3D>();
            QVector3D normal = plane.value(QStringLiteral("normal")).value<QVector3D>();
            if (normal.lengthSquared() <= 1e-16f)
                continue;
            normal.normalize();

            const gp_Pln pln(
                gp_Pnt(origin.x(), origin.y(), origin.z()),
                gp_Dir(normal.x(), normal.y(), normal.z()));
            BRepBuilderAPI_MakeFace faceMaker(pln, -half, half, -half, half);
            if (faceMaker.IsDone())
                tools.Append(faceMaker.Face());
        }

        if (tools.IsEmpty())
        {
            regions.push_back(solid);
            owners.push_back(solidId);
            continue;
        }

        try
        {
            TopTools_ListOfShape oneArgument;
            oneArgument.Append(solid);
            BOPAlgo_Splitter splitter;
            splitter.SetArguments(oneArgument);
            splitter.SetTools(tools);
            splitter.SetRunParallel(false);
            splitter.SetFuzzyValue(1.0e-6);
            splitter.Perform();
            if (splitter.HasErrors())
            {
                if (message) *message = QStringLiteral("实体 %1 的 OCC 分块失败").arg(solidId);
                return {};
            }

            TopTools_IndexedMapOfShape children;
            TopExp::MapShapes(splitter.Shape(), TopAbs_SOLID, children);
            int validChildren = 0;
            for (int i = 1; i <= children.Extent(); ++i)
            {
                GProp_GProps props;
                try { BRepGProp::VolumeProperties(children(i), props); }
                catch (...) { continue; }
                if (!(props.Mass() > 1.0e-30))
                    continue;
                regions.push_back(children(i));
                owners.push_back(solidId);
                ++validChildren;
            }
            if (validChildren <= 0)
            {
                if (message) *message = QStringLiteral("实体 %1 分块后没有得到有效区域").arg(solidId);
                return {};
            }
            if (validChildren > 1)
            {
                anySplit = true;
                ++splitBodyCount;
            }
        }
        catch (...)
        {
            if (message) *message = QStringLiteral("实体 %1 的 OCC 分块发生异常").arg(solidId);
            return {};
        }
    }

    if (!anySplit)
    {
        regions.clear();
        if (message)
            *message = QStringLiteral("切面没有穿过所选实体内部。请调整坐标后再预览。");
        return regions;
    }

    if (ownerVolumeIds) *ownerVolumeIds = owners;
    if (message)
        *message = QStringLiteral("分块有效：%1 个原始实体中有 %2 个被切开，共得到 %3 个区域")
            .arg(originalCount).arg(splitBodyCount).arg(regions.size());
    return regions;
}

QVariantMap evaluateHexBlockingPlanes(const QStringList& geometryUuids, const QVariantList& planes)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("allSweepable"), false);
    out.insert(QStringLiteral("originalVolumeCount"), 0);
    out.insert(QStringLiteral("resultVolumeCount"), 0);
    out.insert(QStringLiteral("sweepableCount"), 0);
    out.insert(QStringLiteral("nonSweepableCount"), 0);
    out.insert(QStringLiteral("maxFaceCount"), 0);
    out.insert(QStringLiteral("topologyPenalty"), 1000000);

    TopTools_ListOfShape arguments;
    TopoDS_Compound sourceCompound;
    int originalCount = 0;
    if (!buildActiveSolidArguments(geometryUuids, arguments, sourceCompound, originalCount))
    {
        out.insert(QStringLiteral("message"), QStringLiteral("当前没有可用于分块预检的实体"));
        return out;
    }
    out.insert(QStringLiteral("originalVolumeCount"), originalCount);

    TopoDS_Shape result = sourceCompound;
    if (!planes.isEmpty())
    {
        Bnd_Box box;
        BRepBndLib::Add(sourceCompound, box);
        if (box.IsVoid())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("无法取得模型包围盒"));
            return out;
        }
        double xmin = 0.0, ymin = 0.0, zmin = 0.0, xmax = 0.0, ymax = 0.0, zmax = 0.0;
        box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
        const double dx = std::max(0.0, xmax - xmin);
        const double dy = std::max(0.0, ymax - ymin);
        const double dz = std::max(0.0, zmax - zmin);
        const double diag = std::max(1e-6, std::sqrt(dx * dx + dy * dy + dz * dz));
        const double half = std::max(1.0, diag * 2.5);

        TopTools_ListOfShape tools;
        for (const QVariant& planeVar : planes)
        {
            const QVariantMap plane = planeVar.toMap();
            QVector3D origin = plane.value(QStringLiteral("origin")).value<QVector3D>();
            QVector3D normal = plane.value(QStringLiteral("normal")).value<QVector3D>();
            if (normal.lengthSquared() <= 1e-16f) continue;
            normal.normalize();

            const gp_Pln pln(
                gp_Pnt(origin.x(), origin.y(), origin.z()),
                gp_Dir(normal.x(), normal.y(), normal.z()));
            BRepBuilderAPI_MakeFace faceMaker(pln, -half, half, -half, half);
            if (!faceMaker.IsDone()) continue;
            tools.Append(faceMaker.Face());
        }

        if (tools.IsEmpty())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("没有有效切割平面"));
            return out;
        }

        try
        {
            BOPAlgo_Splitter splitter;
            splitter.SetArguments(arguments);
            splitter.SetTools(tools);
            splitter.SetRunParallel(false);
            splitter.Perform();
            if (splitter.HasErrors())
            {
                out.insert(QStringLiteral("message"), QStringLiteral("OCC 分块预检失败"));
                return out;
            }
            result = splitter.Shape();
        }
        catch (...)
        {
            out.insert(QStringLiteral("message"), QStringLiteral("OCC 分块预检发生异常"));
            return out;
        }
    }

    QVariantList faceCounts;
    QVariantList blockChecks;
    int resultCount = 0;
    int sweepableCount = 0;
    int nonSweepableCount = 0;
    int maxFaceCount = 0;
    int penalty = 0;

    for (TopExp_Explorer it(result, TopAbs_SOLID); it.More(); it.Next())
    {
        ++resultCount;
        const HexSolidTopologyCheck check = checkHexSweepTopology(it.Current());
        faceCounts.push_back(check.faceCount);
        maxFaceCount = std::max(maxFaceCount, check.faceCount);
        if (check.sweepable) ++sweepableCount;
        else ++nonSweepableCount;

        QVariantMap one;
        one.insert(QStringLiteral("index"), resultCount);
        GProp_GProps regionProps;
        try
        {
            BRepGProp::VolumeProperties(it.Current(), regionProps);
            const gp_Pnt c = regionProps.CentreOfMass();
            one.insert(QStringLiteral("volume"), regionProps.Mass());
            one.insert(QStringLiteral("center"), QVariant::fromValue(QVector3D(
                static_cast<float>(c.X()), static_cast<float>(c.Y()), static_cast<float>(c.Z()))));
        }
        catch (...) {}
        one.insert(QStringLiteral("faceCount"), check.faceCount);
        one.insert(QStringLiteral("nonQuadFaceCount"), check.nonQuadFaceCount);
        one.insert(QStringLiteral("sweepable"), check.sweepable);
        one.insert(QStringLiteral("reason"), check.reason);
        blockChecks.push_back(one);

        // 用于半自动候选搜索的连续评分：优先减少 >6 面和非四边面，
        // 再减少仍不可 Sweep 的 Block 数量。0 表示所有块均通过预检。
        penalty += std::abs(check.faceCount - 6) * 12;
        penalty += check.nonQuadFaceCount * 8;
        if (!check.sweepable) penalty += 5;
    }

    if (resultCount <= originalCount && !planes.isEmpty())
        penalty += 500; // 没有真正切开实体，候选应被强烈淘汰。

    const bool allSweepable = resultCount > 0 && nonSweepableCount == 0;
    out.insert(QStringLiteral("valid"), resultCount > 0);
    out.insert(QStringLiteral("allSweepable"), allSweepable);
    out.insert(QStringLiteral("resultVolumeCount"), resultCount);
    out.insert(QStringLiteral("sweepableCount"), sweepableCount);
    out.insert(QStringLiteral("nonSweepableCount"), nonSweepableCount);
    out.insert(QStringLiteral("maxFaceCount"), maxFaceCount);
    out.insert(QStringLiteral("topologyPenalty"), penalty);
    out.insert(QStringLiteral("faceCounts"), faceCounts);
    out.insert(QStringLiteral("blocks"), blockChecks);

    QStringList counts;
    for (const QVariant& v : faceCounts) counts << QString::number(v.toInt());
    if (allSweepable)
        out.insert(QStringLiteral("message"), QStringLiteral("OCC 预检通过：Volume=%1，块面数=[%2]，全部为 6 面四边 Sweep 拓扑")
            .arg(resultCount).arg(counts.join(QStringLiteral(", "))));
    else
        out.insert(QStringLiteral("message"), QStringLiteral("OCC 预检：Volume=%1，块面数=[%2]，仍有 %3 个 Block 不可 Sweep")
            .arg(resultCount).arg(counts.join(QStringLiteral(", "))).arg(nonSweepableCount));
    return out;
}

QVariantMap evaluateHexBlockingSurfaces(const QStringList& geometryUuids, const QVariantList& surfaces)
{
    QVariantMap out;
    out.insert(QStringLiteral("valid"), false);
    out.insert(QStringLiteral("allSweepable"), false);
    out.insert(QStringLiteral("originalVolumeCount"), 0);
    out.insert(QStringLiteral("resultVolumeCount"), 0);
    out.insert(QStringLiteral("sweepableCount"), 0);
    out.insert(QStringLiteral("nonSweepableCount"), 0);
    out.insert(QStringLiteral("maxFaceCount"), 0);
    out.insert(QStringLiteral("topologyPenalty"), 1000000);
    out.insert(QStringLiteral("sweepableVolumeRatio"), 0.0);

    TopTools_ListOfShape arguments;
    TopoDS_Compound sourceCompound;
    int originalCount = 0;
    if (!buildActiveSolidArguments(geometryUuids, arguments, sourceCompound, originalCount))
    {
        out.insert(QStringLiteral("message"), QStringLiteral("当前没有可用于有限 Surface 分块预检的实体"));
        return out;
    }
    out.insert(QStringLiteral("originalVolumeCount"), originalCount);

    TopoDS_Shape result = sourceCompound;
    if (!surfaces.isEmpty())
    {
        TopTools_ListOfShape tools;
        int validSurfaceCount = 0;

        for (const QVariant& surfaceVar : surfaces)
        {
            const QVariantMap surface = surfaceVar.toMap();
            QVariantList loops = surface.value(QStringLiteral("loops")).toList();
            if (loops.isEmpty())
            {
                const QVariantList outer = surface.value(QStringLiteral("outer")).toList();
                if (!outer.isEmpty())
                {
                    loops.push_back(outer);
                    const QVariantList holes = surface.value(QStringLiteral("holes")).toList();
                    for (const QVariant& h : holes) loops.push_back(h);
                }
            }
            if (loops.isEmpty()) continue;

            QVector<TopoDS_Wire> wires;
            for (const QVariant& loopVar : loops)
            {
                const QVariantList pts = loopVar.toList();
                if (pts.size() < 3) continue;

                BRepBuilderAPI_MakePolygon poly;
                int added = 0;
                QVector3D previous;
                for (const QVariant& pointVar : pts)
                {
                    const QVector3D q = pointVar.value<QVector3D>();
                    if (added > 0 && (q - previous).lengthSquared() <= 1e-20f)
                        continue;
                    poly.Add(gp_Pnt(q.x(), q.y(), q.z()));
                    previous = q;
                    ++added;
                }
                if (added < 3) continue;
                poly.Close();
                if (!poly.IsDone()) continue;
                wires.push_back(poly.Wire());
            }
            if (wires.isEmpty()) continue;

            try
            {
                // OnlyPlane=true is intentional. Mesh reconstructs exactly the same
                // local planar cutter before intersecting it with the original OCC CAD.
                BRepBuilderAPI_MakeFace faceMaker(wires.front(), true);
                if (!faceMaker.IsDone()) continue;
                for (int wi = 1; wi < wires.size(); ++wi)
                    faceMaker.Add(wires[wi]);
                if (!faceMaker.IsDone()) continue;
                tools.Append(faceMaker.Face());
                ++validSurfaceCount;
            }
            catch (...)
            {
                continue;
            }
        }

        if (tools.IsEmpty())
        {
            out.insert(QStringLiteral("message"), QStringLiteral("没有可用于 OCC 预检的有效局部 Surface"));
            return out;
        }

        try
        {
            // BOPAlgo_Splitter keeps the original solid exterior and only partitions
            // where the finite face actually crosses the CAD. Surface portions outside
            // the solid cannot create a Region.
            BOPAlgo_Splitter splitter;
            splitter.SetArguments(arguments);
            splitter.SetTools(tools);
            splitter.SetRunParallel(false);
            splitter.Perform();
            if (splitter.HasErrors())
            {
                out.insert(QStringLiteral("message"), QStringLiteral("OCC 有限 Surface 分块预检失败"));
                return out;
            }
            result = splitter.Shape();
            out.insert(QStringLiteral("acceptedSurfaceCount"), validSurfaceCount);
        }
        catch (...)
        {
            out.insert(QStringLiteral("message"), QStringLiteral("OCC 有限 Surface 分块预检发生异常"));
            return out;
        }
    }

    QVariantList faceCounts;
    QVariantList blockChecks;
    int resultCount = 0;
    int sweepableCount = 0;
    int nonSweepableCount = 0;
    int maxFaceCount = 0;
    int penalty = 0;
    double totalVolume = 0.0;
    double sweepableVolume = 0.0;

    for (TopExp_Explorer it(result, TopAbs_SOLID); it.More(); it.Next())
    {
        ++resultCount;
        const TopoDS_Shape solid = it.Current();
        const HexSolidTopologyCheck check = checkHexSweepTopology(solid);
        faceCounts.push_back(check.faceCount);
        maxFaceCount = std::max(maxFaceCount, check.faceCount);

        GProp_GProps volumeProps;
        BRepGProp::VolumeProperties(solid, volumeProps);
        const double volume = std::max(0.0, volumeProps.Mass());
        totalVolume += volume;

        if (check.sweepable)
        {
            ++sweepableCount;
            sweepableVolume += volume;
        }
        else
        {
            ++nonSweepableCount;
        }

        QVariantMap one;
        one.insert(QStringLiteral("index"), resultCount);
        one.insert(QStringLiteral("faceCount"), check.faceCount);
        one.insert(QStringLiteral("nonQuadFaceCount"), check.nonQuadFaceCount);
        one.insert(QStringLiteral("sweepable"), check.sweepable);
        one.insert(QStringLiteral("volume"), volume);
        one.insert(QStringLiteral("reason"), check.reason);
        blockChecks.push_back(one);

        penalty += std::abs(check.faceCount - 6) * 12;
        penalty += check.nonQuadFaceCount * 8;
        if (!check.sweepable) penalty += 5;
    }

    if (resultCount <= originalCount && !surfaces.isEmpty())
        penalty += 500;

    const bool allSweepable = resultCount > 0 && nonSweepableCount == 0;
    const double sweepableVolumeRatio =
        totalVolume > Precision::Confusion() ? std::clamp(sweepableVolume / totalVolume, 0.0, 1.0) : 0.0;

    out.insert(QStringLiteral("valid"), resultCount > 0);
    out.insert(QStringLiteral("allSweepable"), allSweepable);
    out.insert(QStringLiteral("resultVolumeCount"), resultCount);
    out.insert(QStringLiteral("sweepableCount"), sweepableCount);
    out.insert(QStringLiteral("nonSweepableCount"), nonSweepableCount);
    out.insert(QStringLiteral("maxFaceCount"), maxFaceCount);
    out.insert(QStringLiteral("topologyPenalty"), penalty);
    out.insert(QStringLiteral("sweepableVolumeRatio"), sweepableVolumeRatio);
    out.insert(QStringLiteral("faceCounts"), faceCounts);
    out.insert(QStringLiteral("blocks"), blockChecks);

    QStringList counts;
    for (const QVariant& v : faceCounts) counts << QString::number(v.toInt());
    out.insert(QStringLiteral("message"),
        allSweepable
            ? QStringLiteral("OCC 有限 Surface 预检通过：Volume=%1，Sweep/Hex体积占比=%2%，块面数=[%3]")
                .arg(resultCount).arg(sweepableVolumeRatio * 100.0, 0, 'f', 1).arg(counts.join(QStringLiteral(", ")))
            : QStringLiteral("OCC 有限 Surface 预检：Volume=%1，Sweep/Hex体积占比=%2%，仍有 %3 个 Region 不可 Sweep，块面数=[%4]")
                .arg(resultCount).arg(sweepableVolumeRatio * 100.0, 0, 'f', 1).arg(nonSweepableCount)
                .arg(counts.join(QStringLiteral(", "))));
    return out;
}

bool entityMeasure(const QStringList& geometryUuids, int dim, int id, double& measure)
{
    measure = 0.0;

    TopoDS_Shape target;
    if (!readIndexedShape(geometryUuids, dim, id, target))
        return false;

    GProp_GProps props;
    if (!addMassProperties(target, dim, props))
        return false;

    measure = props.Mass();
    return measure > 0.0;
}


int globalEntityId(const QStringList& geometryUuids, const QString& sourceUuid, int dim, int localIndex)
{
    if (sourceUuid.trimmed().isEmpty() || localIndex <= 0 || dim < 1 || dim > 3)
        return 0;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(sourceUuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return 0;

    TopTools_IndexedMapOfShape localIndexed;
    TopExp::MapShapes(*obj->getShape(), shapeTypeForDimension(dim), localIndexed);
    if (localIndex > localIndexed.Extent())
        return 0;

    TopTools_IndexedMapOfShape globalIndexed;
    if (!buildIndexedShapesFromGeometry(geometryUuids, dim, globalIndexed))
        return 0;

    return globalIndexed.FindIndex(localIndexed(localIndex));
}

int globalSolidIdForFace(const QStringList& geometryUuids, const QString& sourceUuid, int localFaceIndex)
{
    if (sourceUuid.trimmed().isEmpty() || localFaceIndex <= 0)
        return 0;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(sourceUuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return 0;

    TopTools_IndexedMapOfShape localFaces;
    TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, localFaces);
    if (localFaceIndex > localFaces.Extent())
        return 0;

    const TopoDS_Shape& pickedFace = localFaces(localFaceIndex);

    TopTools_IndexedMapOfShape localSolids;
    TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, localSolids);
    for (int solidIndex = 1; solidIndex <= localSolids.Extent(); ++solidIndex)
    {
        TopTools_IndexedMapOfShape solidFaces;
        TopExp::MapShapes(localSolids(solidIndex), TopAbs_FACE, solidFaces);
        if (solidFaces.FindIndex(pickedFace) <= 0)
            continue;

        return globalEntityId(geometryUuids, sourceUuid, 3, solidIndex);
    }

    return 0;
}

QList<int> globalEntityIdsForGeometryUuids(const QStringList& allGeometryUuids,
                                           const QStringList& selectedGeometryUuids, int dim)
{
    QList<int> out;
    if (dim < 1 || dim > 3 || allGeometryUuids.isEmpty() || selectedGeometryUuids.isEmpty())
        return out;

    TopTools_IndexedMapOfShape globalIndexed;
    if (!buildIndexedShapesFromGeometry(allGeometryUuids, dim, globalIndexed))
        return out;

    QSet<int> seen;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    for (const QString& uuid : selectedGeometryUuids)
    {
        GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
        if (!obj || !obj->getShape() || obj->getShape()->IsNull())
            continue;

        TopTools_IndexedMapOfShape localIndexed;
        TopExp::MapShapes(*obj->getShape(), shapeTypeForDimension(dim), localIndexed);
        for (int i = 1; i <= localIndexed.Extent(); ++i)
        {
            const int globalId = globalIndexed.FindIndex(localIndexed(i));
            if (globalId > 0 && !seen.contains(globalId))
            {
                seen.insert(globalId);
                out.push_back(globalId);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool entityShape(const QStringList& geometryUuids, int dim, int id, TopoDS_Shape& shape)
{
    shape.Nullify();
    return readIndexedShape(geometryUuids, dim, id, shape);
}

int globalEntityIdForShape(const QStringList& geometryUuids, int dim, const TopoDS_Shape& shape)
{
    if (shape.IsNull() || dim < 1 || dim > 3)
        return 0;
    TopTools_IndexedMapOfShape globalIndexed;
    if (!buildIndexedShapesFromGeometry(geometryUuids, dim, globalIndexed))
        return 0;
    return globalIndexed.FindIndex(shape);
}


bool highlightEntity(const QStringList& geometryUuids, int dim, int id)
{
    return highlightEntities(geometryUuids, dim, QList<int>{id}, false);
}

bool highlightEntities(const QStringList& geometryUuids, int dim, const QList<int>& ids, bool hoverPreview)
{
    if (hoverPreview)
        clearEntityHoverHighlight();
    else
        clearEntityHighlight();

    const QVector<GeometryPickItem> items = resolveGlobalPickItems(geometryUuids, dim, ids);
    if (items.isEmpty())
        return false;

    // GeometrySelectionVisuals extracts Edge/Face cells directly from the
    // currently displayed CAD actor (OCC_EdgeId/OCC_FaceId), uses the exact same
    // tessellation and applies a dedicated coincident-topology selection overlay.
    // This avoids the old second OCC tessellation + translucent coincident actor,
    // whose z-fighting made a selected wall disappear at some camera angles.
    // Use the orange preview palette for pre-processing pages so existing Mesh UI
    // selection semantics stay unchanged; the overlay itself is opaque/stable.
    vtkSmartPointer<vtkProp> actor = GeometrySelectionVisuals::buildPickActor(items, true);
    if (!actor)
        return false;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis)
        return false;

    vis->addProp(hoverPreview ? kEntityHoverHighlightPropId : kEntityHighlightPropId,
                 actor, "Geometry", "entity.highlight", false, true);
    return true;
}

void clearEntityHoverHighlight()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->removeProp(kEntityHoverHighlightPropId, true);
}

void clearEntityHighlight()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(kEntityHighlightPropId, true);
        vis->removeProp(kEntityHoverHighlightPropId, true);
    }
}

struct MeshScopeActorState
{
    vtkWeakPointer<vtkActor> surface;
    vtkWeakPointer<vtkActor> edges;
    vtkSmartPointer<vtkPolyData> surfaceInput;
    vtkSmartPointer<vtkPolyData> edgeInput;
    int surfaceVisible = 0;
    int edgeVisible = 0;
};
QHash<QString, MeshScopeActorState>& meshScopeStates()
{
    static QHash<QString, MeshScopeActorState> value;
    return value;
}
vtkWeakPointer<vtkProp>& meshScopeEdgeBatchActor()
{
    static vtkWeakPointer<vtkProp> value;
    return value;
}
int& meshScopeEdgeBatchVisible()
{
    static int value = 0;
    return value;
}
vtkSmartPointer<vtkPolyData>& meshScopeEdgeBatchInput()
{
    static vtkSmartPointer<vtkPolyData> value;
    return value;
}

vtkSmartPointer<vtkPolyData> filterMeshScopeCells(
    vtkPolyData* source, const char* idArrayName,
    const QSet<int>& wanted, bool surface)
{
    if (!source || !source->GetPoints() || wanted.isEmpty()) return nullptr;
    vtkDataArray* indices = source->GetCellData()
        ? source->GetCellData()->GetArray(idArrayName) : nullptr;
    if (!indices || indices->GetNumberOfTuples() != source->GetNumberOfCells())
        return nullptr;
    vtkNew<vtkCellArray> cells;
    vtkNew<vtkIdList> pointIds;
    auto output = vtkSmartPointer<vtkPolyData>::New();
    output->SetPoints(source->GetPoints());
    output->GetPointData()->ShallowCopy(source->GetPointData());
    output->GetCellData()->CopyAllocate(source->GetCellData(), source->GetNumberOfCells());
    vtkIdType outputCell = 0;
    for (vtkIdType cell = 0; cell < source->GetNumberOfCells(); ++cell)
    {
        if (!wanted.contains(static_cast<int>(indices->GetTuple1(cell)))) continue;
        source->GetCellPoints(cell, pointIds);
        if (pointIds->GetNumberOfIds() < (surface ? 3 : 2)) continue;
        cells->InsertNextCell(pointIds);
        output->GetCellData()->CopyData(source->GetCellData(), cell, outputCell++);
    }
    if (outputCell == 0) return nullptr;
    if (surface) output->SetPolys(cells);
    else output->SetLines(cells);
    return output;
}

void clearMeshScopeSolids(bool render)
{
    auto& states = meshScopeStates();
    const bool changed = !states.isEmpty() || meshScopeEdgeBatchActor().GetPointer();
    for (auto it = states.cbegin(); it != states.cend(); ++it)
    {
        const MeshScopeActorState& state = it.value();
        if (auto* actor = state.surface.GetPointer())
        {
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                if (state.surfaceInput) mapper->SetInputData(state.surfaceInput);
            actor->SetVisibility(state.surfaceVisible);
        }
        if (auto* actor = state.edges.GetPointer())
        {
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                if (state.edgeInput) mapper->SetInputData(state.edgeInput);
            actor->SetVisibility(state.edgeVisible);
        }
    }
    states.clear();
    if (auto* batch = meshScopeEdgeBatchActor().GetPointer())
    {
        if (auto* actor = vtkActor::SafeDownCast(batch))
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                if (meshScopeEdgeBatchInput()) mapper->SetInputData(meshScopeEdgeBatchInput());
        batch->SetVisibility(meshScopeEdgeBatchVisible());
    }
    meshScopeEdgeBatchInput() = nullptr;
    meshScopeEdgeBatchActor() = nullptr;
    if (render && changed)
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
}

bool showMeshScopeSolids(const QStringList& geometryUuids,
                         const QList<int>& selectedGlobalSolidIds)
{
    clearMeshScopeSolids(false);
    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    if (!manager) return false;
    TopTools_IndexedMapOfShape globalSolids;
    if (!buildIndexedShapesFromGeometry(geometryUuids, 3, globalSolids)) return false;
    QSet<int> included;
    for (int id : selectedGlobalSolidIds)
    {
        if (id <= 0 || id > globalSolids.Extent()) return false;
        included.insert(id);
    }
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis) return false;
    // Large assembly edge batching is not an individual CAD actor. Suppress
    // its unfiltered outline only during this temporary view.
    meshScopeEdgeBatchActor() = vis->getProp(QStringLiteral("__geometry_cad_edge_batch"));
    const bool hasBatchedEdges = meshScopeEdgeBatchActor().GetPointer() != nullptr;
    if (auto* batch = meshScopeEdgeBatchActor().GetPointer())
    {
        meshScopeEdgeBatchVisible() = batch->GetVisibility();
        if (auto* actor = vtkActor::SafeDownCast(batch))
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                meshScopeEdgeBatchInput() = mapper->GetInput();
        batch->SetVisibility(false);
    }
    vtkNew<vtkAppendPolyData> selectedEdgeBatch;
    int selectedEdgeInputs = 0;
    auto includeInEdgeBatch = [&](vtkPolyData* poly) {
        if (hasBatchedEdges && poly && poly->GetNumberOfLines() > 0) {
            selectedEdgeBatch->AddInputData(poly);
            ++selectedEdgeInputs;
        }
    };
    for (const QString& uuid : geometryUuids)
    {
        auto* obj = manager->getGeomShapeObject(uuid);
        if (!obj || !obj->getShape() || obj->getShape()->IsNull()) continue;
        TopTools_IndexedMapOfShape localSolids, localFaces, localEdges;
        TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, localSolids);
        if (localSolids.Extent() == 0) continue;
        QSet<int> checkedLocal;
        for (int j = 1; j <= localSolids.Extent(); ++j)
        {
            const int globalId = globalSolids.FindIndex(localSolids(j));
            if (included.contains(globalId)) checkedLocal.insert(j);
        }
        if (checkedLocal.size() == localSolids.Extent()) {
            if (!GeometryDisplayManager::isVisible(uuid)) continue;
            vtkActor* edgeActor = obj->getEdgeActor().GetPointer();
            if (edgeActor)
                if (auto* mapper = vtkPolyDataMapper::SafeDownCast(edgeActor->GetMapper()))
                    includeInEdgeBatch(mapper->GetInput());
            continue;
        }

        MeshScopeActorState state;
        state.surface = obj->getSurfActor();
        state.edges = obj->getEdgeActor();
        if (auto* actor = state.surface.GetPointer())
        {
            state.surfaceVisible = actor->GetVisibility();
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                state.surfaceInput = mapper->GetInput();
        }
        if (auto* actor = state.edges.GetPointer())
        {
            state.edgeVisible = actor->GetVisibility();
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                state.edgeInput = mapper->GetInput();
        }
        meshScopeStates().insert(uuid, state);
        if (checkedLocal.isEmpty())
        {
            if (auto* actor = state.surface.GetPointer()) actor->SetVisibility(false);
            if (auto* actor = state.edges.GetPointer()) actor->SetVisibility(false);
            continue;
        }
        TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, localFaces);
        TopExp::MapShapes(*obj->getShape(), TopAbs_EDGE, localEdges);
        QSet<int> faceIds, edgeIds;
        for (int j : checkedLocal)
        {
            TopTools_IndexedMapOfShape faces, edges;
            TopExp::MapShapes(localSolids(j), TopAbs_FACE, faces);
            TopExp::MapShapes(localSolids(j), TopAbs_EDGE, edges);
            for (int k = 1; k <= faces.Extent(); ++k)
                if (int id = localFaces.FindIndex(faces(k)); id > 0) faceIds.insert(id);
            for (int k = 1; k <= edges.Extent(); ++k)
                if (int id = localEdges.FindIndex(edges(k)); id > 0) edgeIds.insert(id);
        }
        // Fail closed when a legacy CAD actor cannot distinguish sub-solids.
        // Never leave an unchecked solid visible just because its face map is absent.
        auto partialSurface = filterMeshScopeCells(state.surfaceInput, "OCC_FaceId", faceIds, true);
        if (!partialSurface)
        {
            clearMeshScopeSolids();
            return false;
        }
        if (auto* actor = state.surface.GetPointer())
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                mapper->SetInputData(partialSurface);
        if (auto* actor = state.edges.GetPointer())
        {
            const auto partialEdges = filterMeshScopeCells(state.edgeInput, "OCC_EdgeId", edgeIds, false);
            if (partialEdges)
            {
                if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper()))
                    mapper->SetInputData(partialEdges);
                includeInEdgeBatch(partialEdges);
            }
            else actor->SetVisibility(false);
        }
    }
    if (hasBatchedEdges && selectedEdgeInputs > 0)
    {
        selectedEdgeBatch->Update();
        if (auto* actor = vtkActor::SafeDownCast(meshScopeEdgeBatchActor().GetPointer()))
            if (auto* mapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper())) {
                mapper->SetInputData(selectedEdgeBatch->GetOutput());
                actor->SetVisibility(meshScopeEdgeBatchVisible());
            }
    }
    vis->render();
    return true;
}

} // namespace GeometryEntityTools
