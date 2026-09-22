#include "FeatureBuilder.h"

#include "GeometryBuildUtils.h"
#include "BooleanBuilder.h"

#include "CadDocument.h"
#include "CadBody.h"
#include <GeometryDisplayManager.h>
#include <GeometryCleanup.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCone.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeTorus.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeRevol.hxx>
#include <BRepOffsetAPI_MakePipe.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRepOffsetAPI_ThruSections.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <BRep_Builder.hxx>

#include <STEPControl_Reader.hxx>
#include <IGESControl_Reader.hxx>
#include <BRepTools.hxx>
#include <Precision.hxx>
#include <gp_Circ.hxx>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>

#include <QFile>
#include <QList>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <QPointF>
#include <QObject>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

using namespace GeometryBuildUtils;


namespace
{
struct LoftSection
{
    TopoDS_Wire outer;
    std::vector<TopoDS_Wire> inner;
};

struct WireSortKey
{
    double area = 0.0;
    gp_Pnt center;
};

WireSortKey wireSortKey(const TopoDS_Wire& wire)
{
    WireSortKey key;
    try
    {
        BRepBuilderAPI_MakeFace faceMaker(wire);
        if (faceMaker.IsDone() && !faceMaker.Face().IsNull())
        {
            GProp_GProps properties;
            BRepGProp::SurfaceProperties(faceMaker.Face(), properties);
            key.area = properties.Mass();
            key.center = properties.CentreOfMass();
        }
    }
    catch (...)
    {
    }
    return key;
}

void sortInnerWires(std::vector<TopoDS_Wire>& wires)
{
    std::sort(wires.begin(), wires.end(), [](const TopoDS_Wire& lhs, const TopoDS_Wire& rhs)
    {
        const WireSortKey a = wireSortKey(lhs);
        const WireSortKey b = wireSortKey(rhs);
        const double areaScale = std::max({1.0, std::abs(a.area), std::abs(b.area)});
        if (std::abs(a.area - b.area) > areaScale * 1.0e-8)
            return a.area > b.area;
        if (std::abs(a.center.X() - b.center.X()) > 1.0e-8)
            return a.center.X() < b.center.X();
        if (std::abs(a.center.Y() - b.center.Y()) > 1.0e-8)
            return a.center.Y() < b.center.Y();
        return a.center.Z() < b.center.Z();
    });
}

int edgeCountOfShape(const TopoDS_Shape& shape)
{
    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    return edges.Extent();
}

bool singleWireFromPathShape(const TopoDS_Shape& shape, TopoDS_Wire& wire, QString* message)
{
    wire.Nullify();
    if (shape.IsNull())
        return false;

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    if (faces.Extent() > 0)
    {
        if (message)
            *message = QObject::tr("扫掠路径必须是独立曲线，不能直接选择面或实体。");
        return false;
    }

    TopTools_IndexedMapOfShape wires;
    TopExp::MapShapes(shape, TopAbs_WIRE, wires);
    if (wires.Extent() == 1)
    {
        wire = TopoDS::Wire(wires(1));
        if (wire.IsNull() || edgeCountOfShape(wire) != edgeCountOfShape(shape))
        {
            wire.Nullify();
            if (message)
                *message = QObject::tr("路径除主线框外还包含独立边，请只保留一条路径。");
            return false;
        }
        return true;
    }
    if (wires.Extent() > 1)
    {
        if (message)
            *message = QObject::tr("路径中包含多条线框，请只保留一条连续路径。");
        return false;
    }

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    if (edges.Extent() <= 0)
    {
        if (message)
            *message = QObject::tr("路径对象中没有边。");
        return false;
    }

    // 分叉路径不具备唯一扫掠方向，必须明确拒绝，不能任意拼接。
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    std::vector<int> degrees(static_cast<size_t>(vertices.Extent() + 1), 0);
    BRepBuilderAPI_MakeWire maker;
    for (int index = 1; index <= edges.Extent(); ++index)
    {
        const TopoDS_Edge edge = TopoDS::Edge(edges(index));
        maker.Add(edge);
        TopoDS_Vertex first;
        TopoDS_Vertex last;
        TopExp::Vertices(edge, first, last, Standard_True);
        const int firstIndex = vertices.FindIndex(first);
        const int lastIndex = vertices.FindIndex(last);
        if (firstIndex > 0)
            ++degrees[static_cast<size_t>(firstIndex)];
        if (lastIndex > 0)
            ++degrees[static_cast<size_t>(lastIndex)];
    }
    for (int degree : degrees)
    {
        if (degree > 2)
        {
            if (message)
                *message = QObject::tr("路径存在分叉，扫掠只支持单条连续路径。");
            return false;
        }
    }

    try
    {
        maker.Build();
        if (!maker.IsDone() || maker.Wire().IsNull())
        {
            if (message)
                *message = QObject::tr("路径边无法连接成一条线框。");
            return false;
        }
        wire = maker.Wire();
        if (edgeCountOfShape(wire) != edges.Extent())
        {
            wire.Nullify();
            if (message)
                *message = QObject::tr("路径包含不连续边，请拆分后重新选择。");
            return false;
        }
        return true;
    }
    catch (...)
    {
        if (message)
            *message = QObject::tr("路径连接失败。");
        return false;
    }
}

bool singleSweepProfile(const TopoDS_Shape& shape, TopoDS_Shape& profile, QString* message)
{
    profile.Nullify();
    if (shape.IsNull())
        return false;

    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    if (solids.Extent() > 0)
    {
        if (message)
            *message = QObject::tr("扫掠截面不能是实体。");
        return false;
    }

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    if (faces.Extent() == 1)
    {
        profile = faces(1);
        if (edgeCountOfShape(profile) != edgeCountOfShape(shape))
        {
            profile.Nullify();
            if (message)
                *message = QObject::tr("截面除主面外还包含独立边，请先整理为单一截面。");
            return false;
        }
        return true;
    }
    if (faces.Extent() > 1)
    {
        if (message)
            *message = QObject::tr("截面包含多个独立面，请一次扫掠一个截面。");
        return false;
    }

    TopTools_IndexedMapOfShape wires;
    TopExp::MapShapes(shape, TopAbs_WIRE, wires);
    if (wires.Extent() == 1)
    {
        profile = wires(1);
        if (edgeCountOfShape(profile) != edgeCountOfShape(shape))
        {
            profile.Nullify();
            if (message)
                *message = QObject::tr("截面除主线框外还包含独立边，请先整理为单一截面。");
            return false;
        }
        return true;
    }
    if (wires.Extent() > 1)
    {
        if (message)
            *message = QObject::tr("截面包含多条线框，请先整理为一个面或一条线框。");
        return false;
    }

    TopTools_IndexedMapOfShape edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    if (edges.Extent() == 1)
    {
        profile = edges(1);
        return true;
    }

    if (message)
        *message = QObject::tr("截面中没有唯一可扫掠的面、线框或边。");
    return false;
}

bool collectLoftSection(const TopoDS_Shape& shape, LoftSection& section, QString* message)
{
    section = LoftSection{};
    if (shape.IsNull())
        return false;

    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(shape, TopAbs_SOLID, solids);
    if (solids.Extent() > 0)
    {
        if (message)
            *message = QObject::tr("放样截面不能是实体。");
        return false;
    }

    TopTools_IndexedMapOfShape faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    if (faces.Extent() > 1)
    {
        if (message)
            *message = QObject::tr("一个放样截面中存在多个独立面，请分别放样。");
        return false;
    }
    if (faces.Extent() == 1)
    {
        const TopoDS_Face face = TopoDS::Face(faces(1));
        section.outer = BRepTools::OuterWire(face);
        if (section.outer.IsNull())
        {
            if (message)
                *message = QObject::tr("截面的外轮廓无效。");
            return false;
        }
        if (edgeCountOfShape(face) != edgeCountOfShape(shape))
        {
            if (message)
                *message = QObject::tr("放样截面除主面外还包含独立边，请先整理轮廓。");
            return false;
        }
        for (TopExp_Explorer exp(face, TopAbs_WIRE); exp.More(); exp.Next())
        {
            const TopoDS_Wire wire = TopoDS::Wire(exp.Current());
            if (!wire.IsNull() && !wire.IsSame(section.outer))
                section.inner.push_back(wire);
        }
        // OCC 的 Wire 遍历顺序不保证稳定。按面积和空间位置排序，
        // 让各截面的同一内孔能够可靠对应。
        sortInnerWires(section.inner);
        return true;
    }

    TopTools_IndexedMapOfShape wires;
    TopExp::MapShapes(shape, TopAbs_WIRE, wires);
    if (wires.Extent() != 1)
    {
        if (message)
            *message = wires.Extent() > 1
                ? QObject::tr("一个放样截面中存在多条独立线框，请先生成面或分别放样。")
                : QObject::tr("截面中没有线框。");
        return false;
    }
    section.outer = TopoDS::Wire(wires(1));
    if (section.outer.IsNull() || edgeCountOfShape(section.outer) != edgeCountOfShape(shape))
    {
        section.outer.Nullify();
        if (message)
            *message = QObject::tr("放样截面除主线框外还包含独立边，请先整理轮廓。");
        return false;
    }
    return true;
}

bool buildLoftForWires(const std::vector<TopoDS_Wire>& wires,
                       bool makeSolid,
                       bool ruled,
                       TopoDS_Shape& result)
{
    result.Nullify();
    if (wires.size() < 2)
        return false;
    try
    {
        BRepOffsetAPI_ThruSections loft(makeSolid ? Standard_True : Standard_False,
                                        ruled ? Standard_True : Standard_False,
                                        1.0e-6);
        loft.CheckCompatibility(Standard_True);
        for (const TopoDS_Wire& wire : wires)
            loft.AddWire(wire);
        loft.Build();
        if (!loft.IsDone() || loft.Shape().IsNull())
            return false;
        result = loft.Shape();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

TopoDS_Shape compoundFromShapes(const std::vector<TopoDS_Shape>& shapes)
{
    if (shapes.empty())
        return TopoDS_Shape();
    if (shapes.size() == 1)
        return shapes.front();

    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    for (const TopoDS_Shape& shape : shapes)
    {
        if (!shape.IsNull())
            builder.Add(compound, shape);
    }
    return compound;
}

ExtrudeParameters legacyExtrudeParameters(double height, int direction)
{
    ExtrudeParameters parameters;
    parameters.distance = std::max(std::abs(height), kEps);
    parameters.extentMode = ExtrudeExtentMode::OneSide;
    parameters.resultMode = ExtrudeResultMode::Auto;
    parameters.reverse = height < 0.0;

    switch (direction)
    {
    case 0:
        parameters.directionMode = ExtrudeDirectionMode::XAxis;
        break;
    case 1:
        parameters.directionMode = ExtrudeDirectionMode::XAxis;
        parameters.reverse = !parameters.reverse;
        break;
    case 2:
        parameters.directionMode = ExtrudeDirectionMode::YAxis;
        break;
    case 3:
        parameters.directionMode = ExtrudeDirectionMode::YAxis;
        parameters.reverse = !parameters.reverse;
        break;
    case 5:
        parameters.directionMode = ExtrudeDirectionMode::ZAxis;
        parameters.reverse = !parameters.reverse;
        break;
    case -1:
        parameters.directionMode = ExtrudeDirectionMode::ProfileNormal;
        break;
    case 4:
    default:
        parameters.directionMode = ExtrudeDirectionMode::ZAxis;
        break;
    }
    return parameters;
}

bool extrusionDirection(const std::vector<TopoDS_Face>& faces,
                        const ExtrudeParameters& parameters,
                        const gp_Dir& detectedNormal,
                        gp_Dir& direction,
                        QString* message)
{
    switch (parameters.directionMode)
    {
    case ExtrudeDirectionMode::ProfileNormal:
    {
        direction = detectedNormal;
        // “沿轮廓法向”必须有唯一含义。多个截面不平行时，不能用第一个
        // 截面的法向悄悄替代其他截面。
        for (const TopoDS_Face& face : faces)
        {
            gp_Dir normal;
            if (!faceNormalDirection(face, normal) || std::abs(normal.Dot(direction)) < 1.0 - 1.0e-8)
            {
                if (message)
                    *message = QObject::tr("所选闭合区域不在平行平面上，无法统一沿轮廓法向拉伸。请改用 X/Y/Z 轴或自定义矢量。");
                return false;
            }
        }
        break;
    }
    case ExtrudeDirectionMode::XAxis:
        direction = gp_Dir(1, 0, 0);
        break;
    case ExtrudeDirectionMode::YAxis:
        direction = gp_Dir(0, 1, 0);
        break;
    case ExtrudeDirectionMode::ZAxis:
        direction = gp_Dir(0, 0, 1);
        break;
    case ExtrudeDirectionMode::Custom:
    {
        const QVector3D vector = parameters.customDirection;
        if (vector.lengthSquared() <= 1.0e-16f)
        {
            if (message)
                *message = QObject::tr("自定义方向矢量不能为零。");
            return false;
        }
        direction = gp_Dir(vector.x(), vector.y(), vector.z());
        break;
    }
    default:
        if (message)
            *message = QObject::tr("拉伸方向参数无效，请重新选择方向。");
        return false;
    }

    if (parameters.reverse)
        direction.Reverse();

    // 拉伸方向必须具有离开截面平面的分量；完全位于截面平面内时只能
    // 得到退化结果，应在调用 OCC 前给出明确提示。
    for (const TopoDS_Face& face : faces)
    {
        gp_Dir normal;
        if (!faceNormalDirection(face, normal))
        {
            // Curved faces do not have one global normal.  They are valid sweep
            // sources for fixed/custom vectors; OCC performs the final geometric
            // validity check when the prism is built.
            if (parameters.directionMode == ExtrudeDirectionMode::ProfileNormal)
            {
                if (message)
                    *message = QObject::tr("曲面没有唯一的拉伸法向，请改用 X/Y/Z 轴或自定义方向。");
                return false;
            }
            continue;
        }
        if (std::abs(normal.Dot(direction)) <= 1.0e-10)
        {
            if (message)
                *message = QObject::tr("拉伸方向不能与截面平面平行。请更换轴向或自定义矢量。");
            return false;
        }
    }
    return true;
}

bool extrusionExtents(const ExtrudeParameters& parameters,
                      double& forwardDistance,
                      double& backwardDistance,
                      QString* message)
{
    forwardDistance = 0.0;
    backwardDistance = 0.0;
    switch (parameters.extentMode)
    {
    case ExtrudeExtentMode::OneSide:
        forwardDistance = parameters.distance;
        break;
    case ExtrudeExtentMode::TwoSides:
        forwardDistance = parameters.distance;
        backwardDistance = parameters.secondDistance;
        break;
    case ExtrudeExtentMode::Symmetric:
        forwardDistance = parameters.distance * 0.5;
        backwardDistance = parameters.distance * 0.5;
        break;
    default:
        if (message)
            *message = QObject::tr("拉伸方式参数无效，请重新选择拉伸方式。");
        return false;
    }

    if (forwardDistance <= kEps ||
        (parameters.extentMode == ExtrudeExtentMode::TwoSides && backwardDistance <= kEps))
    {
        if (message)
        {
            *message = parameters.extentMode == ExtrudeExtentMode::TwoSides
                ? QObject::tr("两侧拉伸的两个距离都必须大于 0。")
                : QObject::tr("拉伸距离必须大于 0。");
        }
        return false;
    }
    return true;
}

}

bool FeatureBuilder::analyzeExtrudeProfile(const QString& profileUuid,
                                            int* faceCount,
                                            int* suggestedDirection,
                                            QString* message,
                                            int* openEdgeCount)
{
    GeomShapeObject* profileObj = validShapeObject(profileUuid);
    if (!profileObj)
    {
        if (faceCount)
            *faceCount = 0;
        if (openEdgeCount)
            *openEdgeCount = 0;
        if (suggestedDirection)
            *suggestedDirection = 4;
        if (message)
            *message = QObject::tr("来源草图或轮廓不存在。");
        return false;
    }

    const TopoDS_Shape& source = *(profileObj->getShape());
    std::vector<TopoDS_Face> faces;
    std::vector<TopoDS_Shape> openCurves;
    int faceDirection = 4;
    int curveDirection = 4;
    int ignoredOpenEdgeCount = 0;
    QString faceMessage;
    QString curveMessage;
    gp_Dir faceNormal(0, 0, 1);
    gp_Dir curveNormal(0, 0, 1);

    collectExtrudableFaces(source, faces, &faceDirection, &faceMessage,
                           &ignoredOpenEdgeCount, &faceNormal);
    collectExtrudableCurves(source, openCurves, &curveDirection, &curveMessage,
                            &curveNormal);

    const int closedCount = static_cast<int>(faces.size());
    const int openCount = static_cast<int>(openCurves.size());
    if (faceCount)
        *faceCount = closedCount;
    if (openEdgeCount)
        *openEdgeCount = openCount;
    if (suggestedDirection)
        *suggestedDirection = closedCount > 0 ? faceDirection : curveDirection;

    if (closedCount <= 0 && openCount <= 0)
    {
        if (message)
        {
            *message = !faceMessage.isEmpty() && faceMessage != QObject::tr("没有闭合平面轮廓。")
                ? faceMessage
                : (curveMessage.isEmpty() ? QObject::tr("没有可拉伸的轮廓。") : curveMessage);
        }
        return false;
    }

    if (message)
    {
        if (closedCount > 0 && openCount > 0)
        {
            *message = QObject::tr("检测到 %1 个闭合区域和 %2 组开放轮廓；将同时生成实体和曲面。")
                           .arg(closedCount).arg(openCount);
        }
        else if (closedCount > 0)
        {
            *message = QObject::tr("检测到 %1 个闭合区域，将生成实体。")
                           .arg(closedCount);
        }
        else
        {
            *message = QObject::tr("检测到 %1 组开放轮廓，将生成曲面。")
                           .arg(openCount);
        }
    }
    return true;
}

bool FeatureBuilder::buildExtrudeShape(const QString& profileUuid,
                                       const QVector<CadFaceRef>& faceRefs,
                                       const ExtrudeParameters& parameters,
                                       TopoDS_Shape& result,
                                       int* faceCount,
                                       QString* message)
{
    result.Nullify();

    double forwardDistance = 0.0;
    double backwardDistance = 0.0;
    if (!extrusionExtents(parameters, forwardDistance, backwardDistance, message))
    {
        if (faceCount)
            *faceCount = 0;
        return false;
    }

    std::vector<TopoDS_Face> faces;
    std::vector<TopoDS_Shape> openCurves;
    gp_Dir detectedNormal(0, 0, 1);
    bool haveDetectedNormal = false;

    if (!profileUuid.isEmpty())
    {
        GeomShapeObject* profileObj = validShapeObject(profileUuid);
        if (!profileObj)
        {
            if (faceCount)
                *faceCount = 0;
            if (message)
                *message = QObject::tr("来源草图或轮廓不存在。");
            return false;
        }

        const TopoDS_Shape& source = *(profileObj->getShape());
        int faceDirection = 4;
        int curveDirection = 4;
        int ignoredOpenEdgeCount = 0;
        gp_Dir faceNormal(0, 0, 1);
        gp_Dir curveNormal(0, 0, 1);
        QString faceMessage;
        QString curveMessage;
        collectExtrudableFaces(source, faces, &faceDirection, &faceMessage,
                               &ignoredOpenEdgeCount, &faceNormal);
        collectExtrudableCurves(source, openCurves, &curveDirection, &curveMessage,
                                &curveNormal);
        if (!faces.empty())
        {
            detectedNormal = faceNormal;
            haveDetectedNormal = true;
        }
        else if (!openCurves.empty())
        {
            detectedNormal = curveNormal;
            haveDetectedNormal = true;
        }
    }

    QSet<QString> uniqueFaceRefs;
    for (const CadFaceRef& ref : faceRefs)
    {
        if (ref.uuid.isEmpty() || ref.faceIndex <= 0)
            continue;
        const QString key = ref.uuid + QLatin1Char('#') + QString::number(ref.faceIndex);
        if (uniqueFaceRefs.contains(key))
            continue;
        uniqueFaceRefs.insert(key);

        GeomShapeObject* object = validShapeObject(ref.uuid);
        if (!object)
        {
            if (message)
                *message = QObject::tr("所选面的来源对象不存在。");
            return false;
        }

        TopTools_IndexedMapOfShape indexedFaces;
        TopExp::MapShapes(*(object->getShape()), TopAbs_FACE, indexedFaces);
        if (ref.faceIndex > indexedFaces.Extent())
        {
            if (message)
                *message = QObject::tr("所选面已经失效，请重新选择。");
            return false;
        }

        const TopoDS_Face face = TopoDS::Face(indexedFaces(ref.faceIndex));
        if (face.IsNull())
            continue;

        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane)
        {
            const gp_Dir normal = surface.Plane().Axis().Direction();
            if (!haveDetectedNormal)
            {
                detectedNormal = normal;
                haveDetectedNormal = true;
            }
            else if (parameters.directionMode == ExtrudeDirectionMode::ProfileNormal &&
                     std::abs(detectedNormal.Dot(normal)) < 1.0 - 1.0e-6)
            {
                if (message)
                    *message = QObject::tr("多个面使用“沿截面法向”时必须互相平行；也可以改用固定轴或自定义方向。");
                return false;
            }
        }
        else if (parameters.directionMode == ExtrudeDirectionMode::ProfileNormal)
        {
            if (message)
                *message = QObject::tr("曲面没有唯一的拉伸法向，请改用 X/Y/Z 轴或自定义方向。");
            return false;
        }
        faces.push_back(face);
    }

    const int solidProfileCount = static_cast<int>(faces.size());
    const int surfaceProfileCount = static_cast<int>(openCurves.size());
    if (faceCount)
        *faceCount = solidProfileCount;

    if (solidProfileCount <= 0 && surfaceProfileCount <= 0)
    {
        if (message)
            *message = QObject::tr("请选择草图、轮廓或一个/多个平面面。");
        return false;
    }

    gp_Dir direction(0, 0, 1);
    if (!extrusionDirection(faces, parameters, detectedNormal, direction, message))
        return false;

    // A prism of a planar face degenerates when the sweep direction lies in that
    // face plane. Validate every selected face, not just the first one.
    for (const TopoDS_Face& face : faces)
    {
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Plane &&
            std::abs(surface.Plane().Axis().Direction().Dot(direction)) <= 1.0e-10)
        {
            if (message)
                *message = QObject::tr("拉伸方向不能与所选面平行。");
            return false;
        }
    }
    if (faces.empty() && haveDetectedNormal &&
        std::abs(detectedNormal.Dot(direction)) <= 1.0e-10)
    {
        if (message)
            *message = QObject::tr("拉伸方向不能与轮廓平面平行。");
        return false;
    }

    std::vector<TopoDS_Shape> sourceProfiles;
    sourceProfiles.reserve(faces.size() + openCurves.size());
    for (const TopoDS_Face& face : faces)
        sourceProfiles.push_back(face);
    sourceProfiles.insert(sourceProfiles.end(), openCurves.begin(), openCurves.end());

    const double totalDistance = forwardDistance + backwardDistance;
    const gp_Vec prismVector(direction.X() * totalDistance,
                            direction.Y() * totalDistance,
                            direction.Z() * totalDistance);
    const gp_Vec backwardShift(-direction.X() * backwardDistance,
                               -direction.Y() * backwardDistance,
                               -direction.Z() * backwardDistance);

    std::vector<TopoDS_Shape> prisms;
    prisms.reserve(sourceProfiles.size());
    int failedCount = 0;
    for (const TopoDS_Shape& profile : sourceProfiles)
    {
        try
        {
            TopoDS_Shape startProfile = profile;
            if (backwardDistance > kEps)
            {
                gp_Trsf translation;
                translation.SetTranslation(backwardShift);
                BRepBuilderAPI_Transform transform(profile, translation, Standard_True);
                transform.Build();
                if (!transform.IsDone() || transform.Shape().IsNull())
                {
                    ++failedCount;
                    continue;
                }
                startProfile = transform.Shape();
            }

            BRepPrimAPI_MakePrism prism(startProfile, prismVector);
            prism.Build();
            if (!prism.IsDone() || prism.Shape().IsNull())
            {
                ++failedCount;
                continue;
            }
            prisms.push_back(prism.Shape());
        }
        catch (...)
        {
            ++failedCount;
        }
    }

    if (failedCount > 0 || prisms.size() != sourceProfiles.size())
    {
        if (message)
        {
            *message = QObject::tr("共有 %1 个截面，其中 %2 个拉伸失败；为避免生成不完整结果，本次未应用。")
                           .arg(static_cast<int>(sourceProfiles.size())).arg(failedCount);
        }
        return false;
    }

    if (prisms.empty())
    {
        if (message)
            *message = QObject::tr("所选截面无法拉伸。");
        return false;
    }

    result = compoundFromShapes(prisms);
    if (result.IsNull())
    {
        if (message)
            *message = QObject::tr("拉伸结果为空。");
        return false;
    }

    if (message)
    {
        QString extentText;
        switch (parameters.extentMode)
        {
        case ExtrudeExtentMode::TwoSides: extentText = QObject::tr("两侧"); break;
        case ExtrudeExtentMode::Symmetric: extentText = QObject::tr("对称"); break;
        case ExtrudeExtentMode::OneSide:
        default: extentText = QObject::tr("单向"); break;
        }
        *message = QObject::tr("已按%1方式拉伸 %2 个截面。")
                       .arg(extentText).arg(static_cast<int>(sourceProfiles.size()));
    }
    return true;
}

bool FeatureBuilder::buildExtrudeShape(const QString& profileUuid,
                                       const ExtrudeParameters& parameters,
                                       TopoDS_Shape& result,
                                       int* faceCount,
                                       QString* message)
{
    return buildExtrudeShape(profileUuid, {}, parameters, result, faceCount, message);
}

bool FeatureBuilder::recreateExtrude(const QString& uuid, const QString& name,
                                     const QString& profileUuid,
                                     const QVector<CadFaceRef>& faceRefs,
                                     const ExtrudeParameters& parameters)
{
    TopoDS_Shape result;
    QString message;
    if (!buildExtrudeShape(profileUuid, faceRefs, parameters, result, nullptr, &message))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    // Whole-profile features consume their source. Selecting a sub-face of an
    // existing solid must not hide the entire source body.
    if (!profileUuid.isEmpty())
        hideSourceGeometry(profileUuid);
    return true;
}

bool FeatureBuilder::recreateExtrude(const QString& uuid, const QString& name,
                                     const QString& profileUuid,
                                     const ExtrudeParameters& parameters)
{
    return recreateExtrude(uuid, name, profileUuid, {}, parameters);
}

bool FeatureBuilder::buildExtrudeShape(const QString& profileUuid,
                                       double height,
                                       int direction,
                                       TopoDS_Shape& result,
                                       int* faceCount,
                                       QString* message)
{
    return buildExtrudeShape(profileUuid,
                             legacyExtrudeParameters(height, direction),
                             result, faceCount, message);
}

bool FeatureBuilder::recreateExtrude(const QString& uuid, const QString& name,
                                     const QString& profileUuid,
                                     double height, int direction)
{
    return recreateExtrude(uuid, name, profileUuid,
                           legacyExtrudeParameters(height, direction));
}

bool FeatureBuilder::buildRevolveShape(const QString& profileUuid,
                                       const RevolveParameters& parameters,
                                       TopoDS_Shape& result,
                                       QString* message)
{
    result.Nullify();
    if (std::abs(parameters.angleDeg) <= kEps)
    {
        if (message)
            *message = QObject::tr("旋转角度不能为 0。");
        return false;
    }

    if (parameters.axisDirection.lengthSquared() <= 1.0e-16f)
    {
        if (message)
            *message = QObject::tr("旋转轴方向不能为零。");
        return false;
    }

    const QString sourceUuid = !parameters.sourceFace.uuid.isEmpty()
                                   ? parameters.sourceFace.uuid
                                   : profileUuid;
    GeomShapeObject* profileObj = validShapeObject(sourceUuid);
    if (!profileObj)
    {
        if (message)
            *message = QObject::tr("截面不存在。");
        return false;
    }

    std::vector<TopoDS_Face> faces;
    if (parameters.sourceFace.faceIndex > 0)
    {
        TopTools_IndexedMapOfShape indexedFaces;
        TopExp::MapShapes(*(profileObj->getShape()), TopAbs_FACE, indexedFaces);
        if (parameters.sourceFace.faceIndex > indexedFaces.Extent())
        {
            if (message)
                *message = QObject::tr("所选旋转截面已经失效，请重新选择几何面。");
            return false;
        }

        const TopoDS_Face face = TopoDS::Face(indexedFaces(parameters.sourceFace.faceIndex));
        if (face.IsNull())
        {
            if (message)
                *message = QObject::tr("所选旋转截面无效。");
            return false;
        }
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
        {
            if (message)
                *message = QObject::tr("请选择平面几何面作为旋转截面。");
            return false;
        }
        faces.push_back(face);
    }
    else
    {
        QString collectMessage;
        if (!collectExtrudableFaces(*(profileObj->getShape()), faces, nullptr, &collectMessage) || faces.empty())
        {
            if (message)
                *message = collectMessage.isEmpty() ? QObject::tr("没有可旋转的闭合截面。") : collectMessage;
            return false;
        }
    }

    const QVector3D axisVector = parameters.axisDirection.normalized();
    const gp_Dir axisDirection(axisVector.x(), axisVector.y(), axisVector.z());
    const gp_Pnt axisOrigin(parameters.axisOrigin.x(),
                            parameters.axisOrigin.y(),
                            parameters.axisOrigin.z());
    const gp_Ax1 axis(axisOrigin, axisDirection);
    const double angleRad = parameters.angleDeg * 3.14159265358979323846 / 180.0;

    std::vector<TopoDS_Shape> revolvedShapes;
    revolvedShapes.reserve(faces.size());
    int failedCount = 0;
    for (const TopoDS_Face& face : faces)
    {
        try
        {
            BRepPrimAPI_MakeRevol revol(face, axis, angleRad, Standard_True);
            revol.Build();
            if (!revol.IsDone())
            {
                ++failedCount;
                continue;
            }

            TopoDS_Shape shape = revol.Shape();
            if (shape.IsNull())
            {
                ++failedCount;
                continue;
            }

            BRepCheck_Analyzer analyzer(shape);
            if (!analyzer.IsValid())
            {
                ++failedCount;
                continue;
            }
            revolvedShapes.push_back(shape);
        }
        catch (...)
        {
            ++failedCount;
        }
    }

    if (failedCount > 0 || revolvedShapes.size() != faces.size())
    {
        if (message)
            *message = QObject::tr("部分截面无法绕当前旋转轴生成有效结果，请调整旋转轴或角度。");
        return false;
    }

    result = compoundFromShapes(revolvedShapes);
    if (result.IsNull())
    {
        if (message)
            *message = QObject::tr("旋转结果为空，请检查旋转轴与截面位置。");
        return false;
    }

    if (message)
        message->clear();
    return true;
}

bool FeatureBuilder::recreateRevolve(const QString& uuid, const QString& name,
                                     const QString& profileUuid,
                                     const RevolveParameters& parameters)
{
    TopoDS_Shape result;
    if (!buildRevolveShape(profileUuid, parameters, result, nullptr))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(!parameters.sourceFace.uuid.isEmpty()
                           ? parameters.sourceFace.uuid
                           : profileUuid);
    return true;
}

bool FeatureBuilder::buildRevolveShape(const QString& profileUuid,
                                       double angleDeg, int axisType,
                                       double ox, double oy, double oz,
                                       TopoDS_Shape& result,
                                       QString* message)
{
    RevolveParameters parameters;
    parameters.angleDeg = angleDeg;
    parameters.axisOrigin = QVector3D(float(ox), float(oy), float(oz));
    switch (axisType)
    {
    case 0: parameters.axisDirection = QVector3D(1.0f, 0.0f, 0.0f); break;
    case 1: parameters.axisDirection = QVector3D(0.0f, 1.0f, 0.0f); break;
    case 2:
    default: parameters.axisDirection = QVector3D(0.0f, 0.0f, 1.0f); break;
    }
    return buildRevolveShape(profileUuid, parameters, result, message);
}

bool FeatureBuilder::recreateRevolve(const QString& uuid, const QString& name,
                                     const QString& profileUuid, double angleDeg,
                                     int axisType, double ox, double oy, double oz)
{
    RevolveParameters parameters;
    parameters.angleDeg = angleDeg;
    parameters.axisOrigin = QVector3D(float(ox), float(oy), float(oz));
    switch (axisType)
    {
    case 0: parameters.axisDirection = QVector3D(1.0f, 0.0f, 0.0f); break;
    case 1: parameters.axisDirection = QVector3D(0.0f, 1.0f, 0.0f); break;
    case 2:
    default: parameters.axisDirection = QVector3D(0.0f, 0.0f, 1.0f); break;
    }
    return recreateRevolve(uuid, name, profileUuid, parameters);
}

bool FeatureBuilder::analyzeSweepProfile(const QString& profileUuid, QString* message)
{
    GeomShapeObject* object = validShapeObject(profileUuid);
    if (!object)
    {
        if (message)
            *message = QObject::tr("扫掠截面不存在。");
        return false;
    }
    TopoDS_Shape profile;
    return singleSweepProfile(*(object->getShape()), profile, message);
}

bool FeatureBuilder::analyzeSweepPath(const QString& pathUuid, QString* message)
{
    GeomShapeObject* object = validShapeObject(pathUuid);
    if (!object)
    {
        if (message)
            *message = QObject::tr("扫掠路径不存在。");
        return false;
    }
    TopoDS_Wire path;
    return singleWireFromPathShape(*(object->getShape()), path, message);
}

bool FeatureBuilder::analyzeLoftSection(const QString& profileUuid, QString* message)
{
    GeomShapeObject* object = validShapeObject(profileUuid);
    if (!object)
    {
        if (message)
            *message = QObject::tr("放样截面不存在。");
        return false;
    }
    LoftSection section;
    return collectLoftSection(*(object->getShape()), section, message);
}

bool FeatureBuilder::buildSweepShape(const QString& profileUuid,
                                     const QString& pathUuid,
                                     TopoDS_Shape& result,
                                     QString* message)
{
    result.Nullify();
    if (profileUuid.isEmpty() || pathUuid.isEmpty() || profileUuid == pathUuid)
    {
        if (message)
            *message = QObject::tr("截面和路径不能相同。");
        return false;
    }

    GeomShapeObject* profileObj = validShapeObject(profileUuid);
    GeomShapeObject* pathObj = validShapeObject(pathUuid);
    if (!profileObj || !pathObj)
    {
        if (message)
            *message = QObject::tr("扫掠截面或路径不存在。");
        return false;
    }

    TopoDS_Shape profile;
    if (!singleSweepProfile(*(profileObj->getShape()), profile, message))
        return false;

    TopoDS_Wire spine;
    if (!singleWireFromPathShape(*(pathObj->getShape()), spine, message))
        return false;

    try
    {
        BRepOffsetAPI_MakePipe pipe(spine, profile);
        pipe.Build();
        if (!pipe.IsDone() || pipe.Shape().IsNull())
        {
            if (message)
                *message = QObject::tr("扫掠失败：截面应靠近路径起点，且路径不能自交或急剧折返。");
            return false;
        }

        result = pipe.Shape();
        BRepCheck_Analyzer analyzer(result);
        if (!analyzer.IsValid())
        {
            if (message)
                *message = QObject::tr("扫掠结果无效，请调整截面位置或路径转角。");
            result.Nullify();
            return false;
        }

        if (message)
            *message = QObject::tr("扫掠已生成。");
        return true;
    }
    catch (...)
    {
        if (message)
            *message = QObject::tr("扫掠计算失败，请检查截面和路径。");
        return false;
    }
}

bool FeatureBuilder::recreateSweep(const QString& uuid,
                                   const QString& name,
                                   const QString& profileUuid,
                                   const QString& pathUuid)
{
    TopoDS_Shape result;
    QString message;
    if (!buildSweepShape(profileUuid, pathUuid, result, &message))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(profileUuid);
    hideSourceGeometry(pathUuid);
    return true;
}

bool FeatureBuilder::buildLoftShape(const QStringList& profileUuids,
                                    bool makeSolid,
                                    bool ruled,
                                    TopoDS_Shape& result,
                                    QString* message)
{
    result.Nullify();
    if (profileUuids.size() < 2)
    {
        if (message)
            *message = QObject::tr("放样至少需要两个截面。");
        return false;
    }

    std::vector<LoftSection> sections;
    sections.reserve(static_cast<size_t>(profileUuids.size()));
    for (const QString& uuid : profileUuids)
    {
        GeomShapeObject* object = validShapeObject(uuid);
        if (!object)
        {
            if (message)
                *message = QObject::tr("放样截面不存在：%1").arg(uuid);
            return false;
        }
        LoftSection section;
        QString sectionMessage;
        if (!collectLoftSection(*(object->getShape()), section, &sectionMessage))
        {
            if (message)
                *message = sectionMessage;
            return false;
        }
        sections.push_back(section);
    }

    const size_t innerCount = sections.front().inner.size();
    if (innerCount > 0 && !makeSolid)
    {
        if (message)
            *message = QObject::tr("带内孔截面需要勾选“生成实体”，曲面放样暂不支持贯通内孔。");
        return false;
    }
    for (const LoftSection& section : sections)
    {
        if (section.inner.size() != innerCount)
        {
            if (message)
                *message = QObject::tr("各截面的内孔数量不一致，无法建立稳定对应关系。");
            return false;
        }
    }

    std::vector<TopoDS_Wire> outerWires;
    outerWires.reserve(sections.size());
    for (const LoftSection& section : sections)
        outerWires.push_back(section.outer);

    if (!buildLoftForWires(outerWires, makeSolid, ruled, result))
    {
        if (message)
            *message = QObject::tr("放样失败：请检查截面顺序和方向。");
        return false;
    }

    // 截面带内孔时，分别放样每组内轮廓，再从外轮廓放样结果中切除。
    // 不再像旧实现一样只取第一条 Wire，导致内孔静默丢失。
    for (size_t holeIndex = 0; holeIndex < innerCount; ++holeIndex)
    {
        std::vector<TopoDS_Wire> holeWires;
        holeWires.reserve(sections.size());
        for (const LoftSection& section : sections)
            holeWires.push_back(section.inner.at(holeIndex));

        TopoDS_Shape holeLoft;
        TopoDS_Shape cutResult;
        if (!buildLoftForWires(holeWires, makeSolid, ruled, holeLoft) ||
            !BooleanBuilder::buildCutShape(result, holeLoft, cutResult))
        {
            result.Nullify();
            if (message)
                *message = QObject::tr("内孔放样失败，请检查各截面内孔的数量、顺序和方向。");
            return false;
        }
        result = cutResult;
    }

    BRepCheck_Analyzer analyzer(result);
    if (!analyzer.IsValid())
    {
        if (message)
            *message = QObject::tr("放样结果无效，请调整截面顺序或关闭“生成实体”。");
        result.Nullify();
        return false;
    }

    if (message)
        *message = innerCount == 0
            ? QObject::tr("放样已生成，截面数量：%1。").arg(static_cast<int>(sections.size()))
            : QObject::tr("放样已生成，并保留 %1 个贯通内孔。").arg(static_cast<int>(innerCount));
    return true;
}

bool FeatureBuilder::recreateLoft(const QString& uuid,
                                  const QString& name,
                                  const QStringList& profileUuids,
                                  bool makeSolid,
                                  bool ruled)
{
    TopoDS_Shape result;
    QString message;
    if (!buildLoftShape(profileUuids, makeSolid, ruled, result, &message))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    for (const QString& inputUuid : profileUuids)
        hideSourceGeometry(inputUuid);
    return true;
}

bool FeatureBuilder::buildCutExtrudeShape(const QString& targetUuid,
                                          const QString& profileUuid,
                                          double depth,
                                          int direction,
                                          TopoDS_Shape& result,
                                          QString* message)
{
    result.Nullify();
    if (targetUuid.isEmpty() || profileUuid.isEmpty() || targetUuid == profileUuid)
    {
        if (message)
            *message = QObject::tr("目标和轮廓无效。");
        return false;
    }

    if (std::abs(depth) <= kEps)
    {
        if (message)
            *message = QObject::tr("切除深度不能为 0。");
        return false;
    }

    GeomShapeObject* targetObj = validShapeObject(targetUuid);
    if (!targetObj)
    {
        if (message)
            *message = QObject::tr("目标实体不存在。");
        return false;
    }

    TopoDS_Shape tool;
    QString buildMessage;
    if (!buildExtrudeShape(profileUuid, depth, direction, tool, nullptr, &buildMessage) || tool.IsNull())
    {
        if (message)
            *message = QObject::tr("切除工具体生成失败：%1").arg(buildMessage);
        return false;
    }

    if (!BooleanBuilder::buildCutShape(*(targetObj->getShape()), tool, result))
    {
        if (message)
            *message = QObject::tr("切除失败：请确认切除轮廓与目标实体相交，并检查输入几何。");
        return false;
    }

    if (message)
        *message = QObject::tr("切除已生成。");
    return true;
}

bool FeatureBuilder::recreateCutExtrude(const QString& uuid,
                                        const QString& name,
                                        const QString& targetUuid,
                                        const QString& profileUuid,
                                        double depth,
                                        int direction)
{
    TopoDS_Shape result;
    QString message;
    if (!buildCutExtrudeShape(targetUuid, profileUuid, depth, direction, result, &message))
        return false;

    registerShape(uuid, name, new TopoDS_Shape(result));
    hideSourceGeometry(targetUuid);
    hideSourceGeometry(profileUuid);
    return true;
}
