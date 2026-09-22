#include "GeometryBuildUtils.h"

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
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_FindPlane.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <Geom_Circle.hxx>
#include <Geom_Ellipse.hxx>
#include <Geom_Plane.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <gp_Ax1.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_ListIteratorOfListOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
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

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QTextStream>
#include <QVector>
#include <QPointF>
#include <QObject>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <vector>

using namespace GeometryBuildUtils;


namespace
{
constexpr const char* kStableEdgeSelectionPrefix = "edge-v3:";
constexpr const char* kLegacyStableEdgeSelectionPrefix = "edge-v2:";

struct EdgeDescriptor
{
    int index = 0;
    int curveType = -1;
    double length = 0.0;
    gp_Pnt center;
    gp_Pnt first;
    gp_Pnt last;
    bool hasEnds = false;
    QVector<int> adjacentSurfaceTypes;
    QVector<double> adjacentFaceAreas;
    QVector<gp_Dir> adjacentPlanarNormals;
};

double shapeScale(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    try
    {
        BRepBndLib::Add(shape, box);
        if (!box.IsVoid())
        {
            double xmin = 0.0, ymin = 0.0, zmin = 0.0;
            double xmax = 0.0, ymax = 0.0, zmax = 0.0;
            box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
            const double dx = xmax - xmin;
            const double dy = ymax - ymin;
            const double dz = zmax - zmin;
            const double diagonal = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (diagonal > Precision::Confusion())
                return diagonal;
        }
    }
    catch (...)
    {
    }
    return 1.0;
}

bool describeEdge(const TopoDS_Edge& edge, int index, EdgeDescriptor& descriptor,
                  const TopTools_IndexedDataMapOfShapeListOfShape* edgeFaces = nullptr)
{
    if (edge.IsNull())
        return false;

    descriptor = EdgeDescriptor{};
    descriptor.index = index;
    try
    {
        BRepAdaptor_Curve curve(edge);
        descriptor.curveType = static_cast<int>(curve.GetType());
    }
    catch (...)
    {
        descriptor.curveType = -1;
    }

    try
    {
        GProp_GProps properties;
        BRepGProp::LinearProperties(edge, properties);
        descriptor.length = properties.Mass();
        descriptor.center = properties.CentreOfMass();
    }
    catch (...)
    {
        descriptor.length = 0.0;
    }

    try
    {
        TopoDS_Vertex firstVertex;
        TopoDS_Vertex lastVertex;
        TopExp::Vertices(edge, firstVertex, lastVertex, Standard_True);
        if (!firstVertex.IsNull() && !lastVertex.IsNull())
        {
            descriptor.first = BRep_Tool::Pnt(firstVertex);
            descriptor.last = BRep_Tool::Pnt(lastVertex);
            descriptor.hasEnds = true;
        }
    }
    catch (...)
    {
        descriptor.hasEnds = false;
    }

    if (edgeFaces)
    {
        const int mapIndex = edgeFaces->FindIndex(edge);
        if (mapIndex > 0)
        {
            struct FaceContext
            {
                int type = -1;
                double area = 0.0;
                gp_Dir normal = gp_Dir(0.0, 0.0, 1.0);
                bool planar = false;
            };
            QVector<FaceContext> contexts;
            const TopTools_ListOfShape& faces = edgeFaces->FindFromIndex(mapIndex);
            for (TopTools_ListIteratorOfListOfShape it(faces); it.More(); it.Next())
            {
                const TopoDS_Face face = TopoDS::Face(it.Value());
                if (face.IsNull())
                    continue;
                FaceContext context;
                try
                {
                    BRepAdaptor_Surface surface(face, Standard_True);
                    context.type = static_cast<int>(surface.GetType());
                    if (surface.GetType() == GeomAbs_Plane)
                    {
                        context.normal = surface.Plane().Axis().Direction();
                        context.planar = true;
                    }
                }
                catch (...)
                {
                }
                try
                {
                    GProp_GProps props;
                    BRepGProp::SurfaceProperties(face, props);
                    context.area = props.Mass();
                }
                catch (...)
                {
                }
                contexts.push_back(context);
            }
            std::sort(contexts.begin(), contexts.end(), [](const FaceContext& left, const FaceContext& right)
            {
                if (left.type != right.type)
                    return left.type < right.type;
                return left.area < right.area;
            });
            for (const FaceContext& context : contexts)
            {
                descriptor.adjacentSurfaceTypes.push_back(context.type);
                descriptor.adjacentFaceAreas.push_back(context.area);
                descriptor.adjacentPlanarNormals.push_back(
                    context.planar ? context.normal : gp_Dir(0.0, 0.0, 1.0));
            }
        }
    }
    return true;
}

QJsonArray pointJson(const gp_Pnt& point)
{
    return QJsonArray{point.X(), point.Y(), point.Z()};
}

gp_Pnt jsonPoint(const QJsonValue& value)
{
    const QJsonArray array = value.toArray();
    if (array.size() != 3)
        return gp_Pnt();
    return gp_Pnt(array.at(0).toDouble(), array.at(1).toDouble(), array.at(2).toDouble());
}

QJsonObject descriptorJson(const EdgeDescriptor& descriptor)
{
    QJsonObject object;
    object.insert(QStringLiteral("i"), descriptor.index);
    object.insert(QStringLiteral("t"), descriptor.curveType);
    object.insert(QStringLiteral("l"), descriptor.length);
    object.insert(QStringLiteral("c"), pointJson(descriptor.center));
    object.insert(QStringLiteral("e"), descriptor.hasEnds);
    if (descriptor.hasEnds)
    {
        object.insert(QStringLiteral("a"), pointJson(descriptor.first));
        object.insert(QStringLiteral("b"), pointJson(descriptor.last));
    }
    QJsonArray surfaceTypes;
    QJsonArray faceAreas;
    QJsonArray faceNormals;
    for (int i = 0; i < descriptor.adjacentSurfaceTypes.size(); ++i)
    {
        surfaceTypes.append(descriptor.adjacentSurfaceTypes.at(i));
        faceAreas.append(descriptor.adjacentFaceAreas.value(i));
        const gp_Dir normal = descriptor.adjacentPlanarNormals.value(i, gp_Dir(0.0, 0.0, 1.0));
        faceNormals.append(QJsonArray{normal.X(), normal.Y(), normal.Z()});
    }
    object.insert(QStringLiteral("ft"), surfaceTypes);
    object.insert(QStringLiteral("fa"), faceAreas);
    object.insert(QStringLiteral("fn"), faceNormals);
    return object;
}

bool descriptorFromJson(const QJsonObject& object, EdgeDescriptor& descriptor)
{
    if (object.isEmpty())
        return false;
    descriptor.index = object.value(QStringLiteral("i")).toInt();
    descriptor.curveType = object.value(QStringLiteral("t")).toInt(-1);
    descriptor.length = object.value(QStringLiteral("l")).toDouble();
    descriptor.center = jsonPoint(object.value(QStringLiteral("c")));
    descriptor.hasEnds = object.value(QStringLiteral("e")).toBool(false);
    if (descriptor.hasEnds)
    {
        descriptor.first = jsonPoint(object.value(QStringLiteral("a")));
        descriptor.last = jsonPoint(object.value(QStringLiteral("b")));
    }
    const QJsonArray surfaceTypes = object.value(QStringLiteral("ft")).toArray();
    const QJsonArray faceAreas = object.value(QStringLiteral("fa")).toArray();
    const QJsonArray faceNormals = object.value(QStringLiteral("fn")).toArray();
    for (int i = 0; i < surfaceTypes.size(); ++i)
    {
        descriptor.adjacentSurfaceTypes.push_back(surfaceTypes.at(i).toInt(-1));
        descriptor.adjacentFaceAreas.push_back(
            i < faceAreas.size() ? faceAreas.at(i).toDouble() : 0.0);
        const QJsonArray normal = i < faceNormals.size() ? faceNormals.at(i).toArray() : QJsonArray{};
        if (normal.size() == 3)
        {
            const gp_Vec vector(normal.at(0).toDouble(), normal.at(1).toDouble(), normal.at(2).toDouble());
            descriptor.adjacentPlanarNormals.push_back(
                vector.Magnitude() > Precision::Confusion() ? gp_Dir(vector) : gp_Dir(0.0, 0.0, 1.0));
        }
        else
        {
            descriptor.adjacentPlanarNormals.push_back(gp_Dir(0.0, 0.0, 1.0));
        }
    }
    return true;
}

double relativeDifference(double left, double right, double scale)
{
    const double denominator = std::max({std::abs(left), std::abs(right), scale * 1.0e-9});
    return std::abs(left - right) / denominator;
}

double endpointDistance(const EdgeDescriptor& expected,
                        const EdgeDescriptor& candidate,
                        double scale)
{
    if (!expected.hasEnds || !candidate.hasEnds)
        return 0.0;

    const double direct = expected.first.Distance(candidate.first) +
                          expected.last.Distance(candidate.last);
    const double reversed = expected.first.Distance(candidate.last) +
                            expected.last.Distance(candidate.first);
    return std::min(direct, reversed) / std::max(scale, Precision::Confusion());
}

double edgeMatchScore(const EdgeDescriptor& expected,
                      const EdgeDescriptor& candidate,
                      double scale)
{
    double score = 0.0;
    if (expected.curveType >= 0 && candidate.curveType >= 0 &&
        expected.curveType != candidate.curveType)
    {
        score += 100.0;
    }
    score += 4.0 * relativeDifference(expected.length, candidate.length, scale);
    score += 2.0 * expected.center.Distance(candidate.center) /
             std::max(scale, Precision::Confusion());
    score += endpointDistance(expected, candidate, scale);
    if (!expected.adjacentSurfaceTypes.isEmpty())
    {
        if (expected.adjacentSurfaceTypes.size() != candidate.adjacentSurfaceTypes.size())
            score += 12.0;
        const int count = std::min(expected.adjacentSurfaceTypes.size(),
                                   candidate.adjacentSurfaceTypes.size());
        for (int i = 0; i < count; ++i)
        {
            if (expected.adjacentSurfaceTypes.at(i) != candidate.adjacentSurfaceTypes.at(i))
                score += 16.0;
            score += 1.5 * relativeDifference(expected.adjacentFaceAreas.value(i),
                                               candidate.adjacentFaceAreas.value(i), scale * scale);
            if (expected.adjacentSurfaceTypes.at(i) == static_cast<int>(GeomAbs_Plane) &&
                candidate.adjacentSurfaceTypes.at(i) == static_cast<int>(GeomAbs_Plane))
            {
                const double dot = std::abs(expected.adjacentPlanarNormals.value(i).Dot(
                    candidate.adjacentPlanarNormals.value(i)));
                score += 3.0 * (1.0 - std::clamp(dot, 0.0, 1.0));
            }
        }
    }
    return score;
}
}

namespace GeometryBuildUtils
{

gp_Dir getPlaneNormal(int planeType)
{
    switch (planeType)
    {
    case 0: // XY
        return gp_Dir(0, 0, 1);
    case 1: // XZ
        return gp_Dir(0, 1, 0);
    case 2: // YZ
        return gp_Dir(1, 0, 0);
    }
    return gp_Dir(0, 0, 1);
}

gp_Dir axisDirFromType(int axisType)
{
    switch (axisType)
    {
    case 0: return gp_Dir(1, 0, 0);
    case 1: return gp_Dir(0, 1, 0);
    case 2:
    default: return gp_Dir(0, 0, 1);
    }
}

gp_Dir extrudeDirFromType(int direction)
{
    switch (direction)
    {
    case 0: return gp_Dir(1, 0, 0);
    case 1: return gp_Dir(-1, 0, 0);
    case 2: return gp_Dir(0, 1, 0);
    case 3: return gp_Dir(0, -1, 0);
    case 5: return gp_Dir(0, 0, -1);
    case 4:
    default: return gp_Dir(0, 0, 1);
    }
}

int directionTypeFromDir(const gp_Dir& dir)
{
    const double ax = std::abs(dir.X());
    const double ay = std::abs(dir.Y());
    const double az = std::abs(dir.Z());
    if (ax >= ay && ax >= az)
        return dir.X() >= 0.0 ? 0 : 1;
    if (ay >= ax && ay >= az)
        return dir.Y() >= 0.0 ? 2 : 3;
    return dir.Z() >= 0.0 ? 4 : 5;
}

QString directionNameForMessage(int direction)
{
    switch (direction)
    {
    case 0: return QObject::tr("+X");
    case 1: return QObject::tr("-X");
    case 2: return QObject::tr("+Y");
    case 3: return QObject::tr("-Y");
    case 5: return QObject::tr("-Z");
    case 4:
    default: return QObject::tr("+Z");
    }
}

bool faceNormalDirection(const TopoDS_Face& face, gp_Dir& normal)
{
    if (face.IsNull())
        return false;

    try
    {
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() != GeomAbs_Plane)
            return false;

        normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool isValidFaceForExtrude(const TopoDS_Face& face)
{
    if (face.IsNull())
        return false;
    try
    {
        BRepCheck_Analyzer analyzer(face);
        return analyzer.IsValid();
    }
    catch (...)
    {
        return true;
    }
}

bool makeFaceFromWireForExtrude(const TopoDS_Wire& wire, TopoDS_Face& face)
{
    if (wire.IsNull())
        return false;

    try
    {
        BRepBuilderAPI_MakeFace faceMaker(wire, Standard_True);
        faceMaker.Build();
        if (!faceMaker.IsDone() || faceMaker.Face().IsNull())
        {
            BRepBuilderAPI_MakeFace fallbackMaker(wire);
            fallbackMaker.Build();
            if (!fallbackMaker.IsDone() || fallbackMaker.Face().IsNull())
                return false;
            face = fallbackMaker.Face();
        }
        else
        {
            face = faceMaker.Face();
        }
        return isValidFaceForExtrude(face);
    }
    catch (...)
    {
        return false;
    }
}

bool collectExtrudableFaces(const TopoDS_Shape& source,
                            std::vector<TopoDS_Face>& faces,
                            int* suggestedDirection,
                            QString* message,
                            int* openEdgeCount,
                            gp_Dir* profileNormal)
{
    faces.clear();
    if (suggestedDirection)
        *suggestedDirection = 4;
    if (openEdgeCount)
        *openEdgeCount = 0;
    if (profileNormal)
        *profileNormal = gp_Dir(0, 0, 1);

    if (source.IsNull())
    {
        if (message)
            *message = QObject::tr("来源几何为空。");
        return false;
    }

    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(source, TopAbs_SOLID, solids);
    if (solids.Extent() > 0)
    {
        if (message)
            *message = QObject::tr("不是平面轮廓。");
        return false;
    }

    TopTools_IndexedMapOfShape sourceEdges;
    TopExp::MapShapes(source, TopAbs_EDGE, sourceEdges);
    QSet<int> consumedEdges;

    auto markFaceEdges = [&](const TopoDS_Face& face)
    {
        for (TopExp_Explorer edgeExp(face, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
        {
            const int edgeIndex = sourceEdges.FindIndex(edgeExp.Current());
            if (edgeIndex > 0)
                consumedEdges.insert(edgeIndex);
        }
    };

    for (TopExp_Explorer exp(source, TopAbs_FACE); exp.More(); exp.Next())
    {
        TopoDS_Face face = TopoDS::Face(exp.Current());
        gp_Dir planarNormal;
        if (!face.IsNull() && isValidFaceForExtrude(face) &&
            faceNormalDirection(face, planarNormal))
        {
            faces.push_back(face);
            markFaceEdges(face);
        }
    }

    // 除已有 Face 外，也处理 Compound 中独立存在的闭合 Wire。先跳过
    // 已被现有 Face 使用的 Wire，避免同一闭合区域被重复识别。
    for (TopExp_Explorer exp(source, TopAbs_WIRE); exp.More(); exp.Next())
    {
        TopoDS_Wire wire = TopoDS::Wire(exp.Current());
        bool touchesKnownFace = false;
        for (TopExp_Explorer edgeExp(wire, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
        {
            const int edgeIndex = sourceEdges.FindIndex(edgeExp.Current());
            if (edgeIndex > 0 && consumedEdges.contains(edgeIndex))
            {
                touchesKnownFace = true;
                break;
            }
        }
        if (touchesKnownFace)
            continue;

        TopoDS_Face face;
        if (makeFaceFromWireForExtrude(wire, face))
        {
            faces.push_back(face);
            for (TopExp_Explorer edgeExp(wire, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
            {
                const int edgeIndex = sourceEdges.FindIndex(edgeExp.Current());
                if (edgeIndex > 0)
                    consumedEdges.insert(edgeIndex);
            }
        }
    }

    const int unconsumedEdgeCount = std::max(0, sourceEdges.Extent() - int(consumedEdges.size()));
    if (openEdgeCount)
        *openEdgeCount = unconsumedEdgeCount;

    if (faces.empty())
    {
        if (message)
        {
            *message = unconsumedEdgeCount > 0
                ? QObject::tr("没有闭合区域；检测到 %1 条开放或独立曲线，可直接拉伸为曲面。")
                      .arg(unconsumedEdgeCount)
                : QObject::tr("没有闭合平面轮廓。");
        }
        return false;
    }

    // 完整报告闭合区域和剩余开放曲线。拉伸构建器会同时处理两类
    // 轮廓，不会因为存在闭合区域而丢弃开放部分。
    gp_Dir normal(0, 0, 1);
    if (faceNormalDirection(faces.front(), normal))
    {
        if (suggestedDirection)
            *suggestedDirection = directionTypeFromDir(normal);
        if (profileNormal)
            *profileNormal = normal;
    }

    if (message)
    {
        *message = unconsumedEdgeCount > 0
            ? QObject::tr("检测到 %1 个闭合区域和 %2 条开放或独立曲线。")
                  .arg(static_cast<int>(faces.size()))
                  .arg(unconsumedEdgeCount)
            : QObject::tr("检测到 %1 个闭合区域。").arg(static_cast<int>(faces.size()));
    }
    return true;
}

bool collectExtrudableCurves(const TopoDS_Shape& source,
                             std::vector<TopoDS_Shape>& curves,
                             int* suggestedDirection,
                             QString* message,
                             gp_Dir* profileNormal)
{
    curves.clear();
    if (suggestedDirection)
        *suggestedDirection = 4;
    if (profileNormal)
        *profileNormal = gp_Dir(0, 0, 1);

    if (source.IsNull())
    {
        if (message)
            *message = QObject::tr("来源几何为空。");
        return false;
    }

    TopTools_IndexedMapOfShape solids;
    TopExp::MapShapes(source, TopAbs_SOLID, solids);
    if (solids.Extent() > 0)
    {
        if (message)
            *message = QObject::tr("实体不能作为拉伸轮廓。");
        return false;
    }

    // 先找出能够形成平面区域的闭合边。它们由拉伸构建器作为 Face
    // 生成实体，这里只返回没有被这些区域消耗的开放/独立曲线，避免
    // 同一个闭合轮廓既生成实体又重复生成一层侧面。
    std::vector<TopoDS_Face> closedFaces;
    int ignoredDirection = 4;
    int ignoredOpenCount = 0;
    QString ignoredMessage;
    gp_Dir normal(0, 0, 1);
    collectExtrudableFaces(source, closedFaces, &ignoredDirection, &ignoredMessage,
                           &ignoredOpenCount, &normal);

    TopTools_IndexedMapOfShape allEdges;
    TopExp::MapShapes(source, TopAbs_EDGE, allEdges);
    QSet<int> faceEdges;
    for (const TopoDS_Face& face : closedFaces)
    {
        for (TopExp_Explorer edgeExp(face, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
        {
            const int edgeIndex = allEdges.FindIndex(edgeExp.Current());
            if (edgeIndex > 0)
                faceEdges.insert(edgeIndex);
        }
    }

    QSet<int> addedEdges;
    for (TopExp_Explorer exp(source, TopAbs_WIRE); exp.More(); exp.Next())
    {
        const TopoDS_Wire wire = TopoDS::Wire(exp.Current());
        if (wire.IsNull())
            continue;

        bool hasEdge = false;
        bool touchesClosedRegion = false;
        QVector<int> indices;
        for (TopExp_Explorer edgeExp(wire, TopAbs_EDGE); edgeExp.More(); edgeExp.Next())
        {
            const int edgeIndex = allEdges.FindIndex(edgeExp.Current());
            if (edgeIndex <= 0)
                continue;
            hasEdge = true;
            indices.push_back(edgeIndex);
            if (faceEdges.contains(edgeIndex))
                touchesClosedRegion = true;
        }

        // 混合了区域边和开放边的异常 Wire 不整体加入，未被区域使用的
        // 边会在下面作为独立边加入，避免重复或错误封闭。
        if (!hasEdge || touchesClosedRegion)
            continue;

        curves.push_back(wire);
        for (int edgeIndex : indices)
            addedEdges.insert(edgeIndex);
    }

    for (int index = 1; index <= allEdges.Extent(); ++index)
    {
        if (!faceEdges.contains(index) && !addedEdges.contains(index))
            curves.push_back(TopoDS::Edge(allEdges(index)));
    }

    if (curves.empty())
    {
        if (message)
            *message = QObject::tr("没有额外的开放或独立轮廓。");
        return false;
    }

    try
    {
        BRepBuilderAPI_FindPlane planeFinder(source, 1.0e-6);
        if (planeFinder.Found() && !planeFinder.Plane().IsNull())
            normal = planeFinder.Plane()->Pln().Axis().Direction();
    }
    catch (...)
    {
    }

    if (suggestedDirection)
        *suggestedDirection = directionTypeFromDir(normal);
    if (profileNormal)
        *profileNormal = normal;
    if (message)
        *message = QObject::tr("检测到 %1 组开放或独立轮廓。").arg(static_cast<int>(curves.size()));
    return true;
}

void hideSourceGeometry(const QString& uuid)
{
    // Builder 只更新来源对象的逻辑显隐。一次建模操作的树刷新、历史提交和
    // Render 统一由 GeometryService 在 recreate*() 完整成功后执行。
    GeometryDisplayManager::hide(uuid, false);
}

GeomShapeObject* validShapeObject(const QString& uuid)
{
    if (uuid.isEmpty())
        return nullptr;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return nullptr;
    GeomShapeObject* obj = mgr->getGeomShapeObject(uuid);
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return nullptr;
    return obj;
}

QSet<int> parseEdgeSelection(const QString& text, int edgeCount, bool& ok)
{
    ok = true;
    QSet<int> selected;
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return selected;

    const QStringList parts = trimmed.split(',', Qt::SkipEmptyParts);
    for (const QString& rawPart : parts)
    {
        const QString part = rawPart.trimmed();
        if (part.isEmpty())
            continue;

        if (part.contains('-'))
        {
            const QStringList range = part.split('-', Qt::SkipEmptyParts);
            if (range.size() != 2)
            {
                ok = false;
                return {};
            }

            bool okStart = false;
            bool okEnd = false;
            int start = range[0].trimmed().toInt(&okStart);
            int end = range[1].trimmed().toInt(&okEnd);
            if (!okStart || !okEnd || start <= 0 || end <= 0 || start > edgeCount || end > edgeCount)
            {
                ok = false;
                return {};
            }
            if (start > end)
                std::swap(start, end);
            for (int i = start; i <= end; ++i)
                selected.insert(i);
            continue;
        }

        bool okIndex = false;
        const int index = part.toInt(&okIndex);
        if (!okIndex || index <= 0 || index > edgeCount)
        {
            ok = false;
            return {};
        }
        selected.insert(index);
    }

    return selected;
}

QString encodeStableEdgeSelection(const TopoDS_Shape& shape, const QSet<int>& edgeIndices)
{
    if (shape.IsNull() || edgeIndices.isEmpty())
        return QString();

    TopTools_IndexedMapOfShape indexedEdges;
    TopExp::MapShapes(shape, TopAbs_EDGE, indexedEdges);
    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    QJsonArray descriptors;
    QList<int> sortedIndices = edgeIndices.values();
    std::sort(sortedIndices.begin(), sortedIndices.end());
    for (int index : sortedIndices)
    {
        if (index <= 0 || index > indexedEdges.Extent())
            continue;
        EdgeDescriptor descriptor;
        if (describeEdge(TopoDS::Edge(indexedEdges(index)), index, descriptor, &edgeFaces))
            descriptors.append(descriptorJson(descriptor));
    }
    if (descriptors.isEmpty())
        return QString();

    const QByteArray json = QJsonDocument(descriptors).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(kStableEdgeSelectionPrefix) +
           QString::fromLatin1(json.toBase64(QByteArray::Base64UrlEncoding |
                                              QByteArray::OmitTrailingEquals));
}

QSet<int> resolveStableEdgeSelection(const TopoDS_Shape& shape,
                                     const QString& text,
                                     bool& ok)
{
    ok = true;
    if (shape.IsNull())
    {
        ok = false;
        return {};
    }

    const QString trimmed = text.trimmed();
    TopTools_IndexedMapOfShape indexedEdges;
    TopExp::MapShapes(shape, TopAbs_EDGE, indexedEdges);
    const bool isV3 = trimmed.startsWith(QLatin1String(kStableEdgeSelectionPrefix));
    const bool isV2 = trimmed.startsWith(QLatin1String(kLegacyStableEdgeSelectionPrefix));
    if (!isV3 && !isV2)
        return parseEdgeSelection(trimmed, indexedEdges.Extent(), ok);

    const int prefixLength = static_cast<int>(std::strlen(
        isV3 ? kStableEdgeSelectionPrefix : kLegacyStableEdgeSelectionPrefix));
    QByteArray encoded = trimmed.mid(prefixLength).toLatin1();
    const QByteArray json = QByteArray::fromBase64(encoded, QByteArray::Base64UrlEncoding);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        ok = false;
        return {};
    }

    TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
    TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);
    QVector<EdgeDescriptor> candidates;
    candidates.reserve(indexedEdges.Extent());
    for (int index = 1; index <= indexedEdges.Extent(); ++index)
    {
        EdgeDescriptor descriptor;
        if (describeEdge(TopoDS::Edge(indexedEdges(index)), index, descriptor, &edgeFaces))
            candidates.push_back(descriptor);
    }

    const double scale = shapeScale(shape);
    QSet<int> selected;
    for (const QJsonValue& value : document.array())
    {
        EdgeDescriptor expected;
        if (!descriptorFromJson(value.toObject(), expected))
        {
            ok = false;
            return {};
        }

        int bestIndex = 0;
        double bestScore = std::numeric_limits<double>::max();
        for (const EdgeDescriptor& candidate : candidates)
        {
            if (selected.contains(candidate.index))
                continue;
            const double score = edgeMatchScore(expected, candidate, scale);
            if (score < bestScore)
            {
                bestScore = score;
                bestIndex = candidate.index;
            }
        }

        // 相同工程重开、拓扑顺序改变时分数通常接近 0；上游尺寸轻微变化时
        // 仍允许匹配，但曲线类型不一致或位置完全不同会被拒绝。
        if (bestIndex <= 0 || bestScore > 8.0)
        {
            ok = false;
            return {};
        }
        selected.insert(bestIndex);
    }
    return selected;
}



void registerShape(const QString& uuid, const QString& name, TopoDS_Shape* shape,
                   bool createActors)
{
    if (uuid.isEmpty() || !shape || shape->IsNull())
    {
        delete shape;
        return;
    }

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        delete shape;
        return;
    }

    // 隐藏节点只恢复 OCC Shape，第一次勾选显示时再做 OCC->VTK 转换。
    // 这不仅用于工程批量打开，也避免未应用/中间结果和隐藏 STEP 零件
    // 在后台提前三角化。可见对象仍按 fastDisplayMode 决定显示精度。
    // 交互式创建时，新参数节点在首次应用前天然是未勾选状态；这不代表调用方
    // 要求延迟创建 Actor。只有工程快速加载模式中的隐藏节点才跳过显示转换。
    const bool shouldCreateActors =
        createActors &&
        (!CadBody::fastDisplayMode() || GeometryDisplayManager::isVisible(uuid));
    auto* object = new GeomShapeObject(uuid, shape, name, shouldCreateActors);
    GeometryCleanup::removeExistingGeometry(uuid, false);
    mgr->appendGeomShapeObject(object);

    // registerShape() runs inside GeometryService::recreate*().  Do not render or
    // re-enter GeometryWorkbench while the builder is still completing the
    // operation (for example, before an extrude hides its source geometry).
    // GeometryService performs the single final refresh/render after recreate*()
    // returns successfully.  Keep createActors semantics unchanged.
    GeometryDisplayManager::registerObject(uuid, object, false, false);
}

void removeGeometryAndDimNode(const QString& key)
{
    GeometryDisplayManager::removeObject(key, true, false);
    GeometryDisplayManager::forgetVisibilityState(key);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

} // namespace GeometryBuildUtils
