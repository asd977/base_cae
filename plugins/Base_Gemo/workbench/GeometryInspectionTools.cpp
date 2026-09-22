#include "GeometryInspectionTools.h"

#include <Bnd_Box.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Sewing.hxx>
#include <BRepBuilderAPI_MakeSolid.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepAlgoAPI_Check.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Precision.hxx>
#include <ShapeFix_Shape.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedDataMapOfShapeListOfShape.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Circ.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Elips.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Sphere.hxx>
#include <gp_Torus.hxx>

#include <QObject>
#include <QtMath>
#include <exception>
#include <algorithm>
#include <cmath>

namespace
{
QString shapeTypeName(TopAbs_ShapeEnum type)
{
    switch (type)
    {
    case TopAbs_COMPOUND: return QObject::tr("组合体");
    case TopAbs_COMPSOLID: return QObject::tr("复合实体");
    case TopAbs_SOLID: return QObject::tr("实体");
    case TopAbs_SHELL: return QObject::tr("壳");
    case TopAbs_FACE: return QObject::tr("面");
    case TopAbs_WIRE: return QObject::tr("线框");
    case TopAbs_EDGE: return QObject::tr("边");
    case TopAbs_VERTEX: return QObject::tr("点");
    default: return QObject::tr("未知");
    }
}

int countSubShapes(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    TopTools_IndexedMapOfShape indexed;
    TopExp::MapShapes(shape, type, indexed);
    return indexed.Extent();
}


Standard_Real maxVertexTolerance(const TopoDS_Shape& shape)
{
    Standard_Real maxTol = Precision::Confusion();
    TopTools_IndexedMapOfShape vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (Standard_Integer i = 1; i <= vertices.Extent(); ++i)
    {
        const TopoDS_Vertex vertex = TopoDS::Vertex(vertices(i));
        maxTol = std::max(maxTol, BRep_Tool::Tolerance(vertex));
    }
    return maxTol;
}

bool massProperties(const TopoDS_Shape& shape, int dim, GProp_GProps& props)
{
    if (shape.IsNull())
        return false;
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

void fillBounds(const TopoDS_Shape& shape, QVector3D& minPoint, QVector3D& maxPoint)
{
    minPoint = QVector3D();
    maxPoint = QVector3D();
    if (shape.IsNull())
        return;

    Bnd_Box box;
    BRepBndLib::Add(shape, box, Standard_True);
    if (box.IsVoid())
        return;

    Standard_Real xmin = 0.0, ymin = 0.0, zmin = 0.0;
    Standard_Real xmax = 0.0, ymax = 0.0, zmax = 0.0;
    box.Get(xmin, ymin, zmin, xmax, ymax, zmax);
    minPoint = QVector3D(xmin, ymin, zmin);
    maxPoint = QVector3D(xmax, ymax, zmax);
}

QString curveTypeName(GeomAbs_CurveType type)
{
    switch (type)
    {
    case GeomAbs_Line: return QObject::tr("直线");
    case GeomAbs_Circle: return QObject::tr("圆弧/圆");
    case GeomAbs_Ellipse: return QObject::tr("椭圆");
    case GeomAbs_Parabola: return QObject::tr("抛物线");
    case GeomAbs_Hyperbola: return QObject::tr("双曲线");
    case GeomAbs_BezierCurve: return QObject::tr("Bezier 曲线");
    case GeomAbs_BSplineCurve: return QObject::tr("B 样条曲线");
    case GeomAbs_OffsetCurve: return QObject::tr("偏移曲线");
    default: return QObject::tr("其他曲线");
    }
}

QString surfaceTypeName(GeomAbs_SurfaceType type)
{
    switch (type)
    {
    case GeomAbs_Plane: return QObject::tr("平面");
    case GeomAbs_Cylinder: return QObject::tr("圆柱面");
    case GeomAbs_Cone: return QObject::tr("圆锥面");
    case GeomAbs_Sphere: return QObject::tr("球面");
    case GeomAbs_Torus: return QObject::tr("圆环面");
    case GeomAbs_BezierSurface: return QObject::tr("Bezier 曲面");
    case GeomAbs_BSplineSurface: return QObject::tr("B 样条曲面");
    case GeomAbs_SurfaceOfRevolution: return QObject::tr("旋转曲面");
    case GeomAbs_SurfaceOfExtrusion: return QObject::tr("拉伸曲面");
    case GeomAbs_OffsetSurface: return QObject::tr("偏移曲面");
    default: return QObject::tr("其他曲面");
    }
}

bool directionForShape(const TopoDS_Shape& shape, gp_Dir& direction)
{
    if (shape.IsNull())
        return false;

    if (shape.ShapeType() == TopAbs_EDGE)
    {
        const TopoDS_Edge edge = TopoDS::Edge(shape);
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line)
            return false;
        direction = curve.Line().Direction();
        return true;
    }

    if (shape.ShapeType() == TopAbs_FACE)
    {
        const TopoDS_Face face = TopoDS::Face(shape);
        BRepAdaptor_Surface surface(face, Standard_True);
        if (surface.GetType() != GeomAbs_Plane)
            return false;
        direction = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            direction.Reverse();
        return true;
    }

    return false;
}
}

namespace GeometryInspectionTools
{
CheckReport inspect(const TopoDS_Shape& shape, const CheckOptions& options)
{
    CheckReport report;
    report.hasShape = !shape.IsNull();
    if (!report.hasShape)
    {
        report.issues << QObject::tr("几何为空。");
        return report;
    }

    try
    {
        report.shapeType = shapeTypeName(shape.ShapeType());
        report.vertexCount = countSubShapes(shape, TopAbs_VERTEX);
        report.edgeCount = countSubShapes(shape, TopAbs_EDGE);
        report.wireCount = countSubShapes(shape, TopAbs_WIRE);
        report.faceCount = countSubShapes(shape, TopAbs_FACE);
        report.shellCount = countSubShapes(shape, TopAbs_SHELL);
        report.solidCount = countSubShapes(shape, TopAbs_SOLID);
        report.compoundCount = countSubShapes(shape, TopAbs_COMPOUND);

        BRepCheck_Analyzer analyzer(shape, Standard_True);
        report.brepValid = analyzer.IsValid();

        // 与网格导出保持一致：BRepCheck 通过并不代表 Boolean/网格边界一定可用。
        // 关闭 SmallEdge 硬失败，只检查拓扑 + self-interference。
        BRepAlgoAPI_Check bopCheck(shape, Standard_False, Standard_True);
        report.bopValid = bopCheck.IsValid();
        report.valid = report.brepValid && report.bopValid;

        TopTools_IndexedDataMapOfShapeListOfShape edgeFaces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, edgeFaces);

        TopTools_IndexedMapOfShape edgeMap;
        TopExp::MapShapes(shape, TopAbs_EDGE, edgeMap);
        for (int i = 1; i <= edgeMap.Extent(); ++i)
        {
            const TopoDS_Edge edge = TopoDS::Edge(edgeMap(i));
            if (BRep_Tool::Degenerated(edge))
                ++report.degeneratedEdgeCount;

            GProp_GProps props;
            BRepGProp::LinearProperties(edge, props);
            const double length = props.Mass();
            if (length > 0.0 && length < options.smallEdgeLength)
                ++report.smallEdgeCount;

            report.maxTolerance = std::max(report.maxTolerance,
                                           static_cast<double>(BRep_Tool::Tolerance(edge)));

            bool isFreeEdge = !edgeFaces.Contains(edge);
            if (!isFreeEdge)
            {
                const TopTools_ListOfShape& ancestors = edgeFaces.FindFromKey(edge);
                if (ancestors.Extent() > 2)
                    ++report.nonManifoldEdgeCount;
                if (ancestors.Extent() < 2)
                {
                    // 周期曲面的接缝边只属于一个面，但并不是开放边。
                    const bool isSeam = ancestors.Extent() == 1 &&
                        BRep_Tool::IsClosed(edge, TopoDS::Face(ancestors.First()));
                    isFreeEdge = !isSeam;
                }
            }
            if (isFreeEdge)
                ++report.freeEdgeCount;
        }

        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(shape, TopAbs_FACE, faceMap);
        for (int i = 1; i <= faceMap.Extent(); ++i)
        {
            const TopoDS_Face face = TopoDS::Face(faceMap(i));
            GProp_GProps props;
            BRepGProp::SurfaceProperties(face, props);
            const double area = props.Mass();
            if (area > 0.0 && area < options.smallFaceArea)
                ++report.smallFaceCount;
            report.maxTolerance = std::max(report.maxTolerance,
                                           static_cast<double>(BRep_Tool::Tolerance(face)));
        }

        TopTools_IndexedMapOfShape vertexMap;
        TopExp::MapShapes(shape, TopAbs_VERTEX, vertexMap);
        for (int i = 1; i <= vertexMap.Extent(); ++i)
        {
            const TopoDS_Vertex vertex = TopoDS::Vertex(vertexMap(i));
            report.maxTolerance = std::max(report.maxTolerance,
                                           static_cast<double>(BRep_Tool::Tolerance(vertex)));
        }

        GProp_GProps linearProps;
        BRepGProp::LinearProperties(shape, linearProps);
        report.totalLength = linearProps.Mass();

        GProp_GProps surfaceProps;
        BRepGProp::SurfaceProperties(shape, surfaceProps);
        report.totalArea = surfaceProps.Mass();

        GProp_GProps volumeProps;
        BRepGProp::VolumeProperties(shape, volumeProps);
        report.totalVolume = volumeProps.Mass();

        const GProp_GProps* centerProps = nullptr;
        if (report.solidCount > 0 && report.totalVolume > Precision::Confusion())
            centerProps = &volumeProps;
        else if (report.faceCount > 0 && report.totalArea > Precision::Confusion())
            centerProps = &surfaceProps;
        else if (report.edgeCount > 0 && report.totalLength > Precision::Confusion())
            centerProps = &linearProps;

        if (centerProps)
        {
            const gp_Pnt center = centerProps->CentreOfMass();
            report.center = QVector3D(center.X(), center.Y(), center.Z());
        }

        fillBounds(shape, report.boundsMin, report.boundsMax);
        report.closed = report.solidCount > 0 && report.valid && report.freeEdgeCount == 0;

        if (!report.brepValid)
            report.issues << QObject::tr("BRep 拓扑检查未通过。");
        if (!report.bopValid)
            report.issues << QObject::tr("检测到自干涉或不满足 OCCT Boolean 前置条件。");
        if (report.solidCount == 0)
            report.issues << QObject::tr("没有封闭实体；当前结果是面、壳或线框。");
        if (report.solidCount > 0 && report.freeEdgeCount > 0)
            report.issues << QObject::tr("检测到 %1 条开放边。").arg(report.freeEdgeCount);
        if (report.nonManifoldEdgeCount > 0)
            report.issues << QObject::tr("检测到 %1 条非流形边。").arg(report.nonManifoldEdgeCount);
        if (report.degeneratedEdgeCount > 0)
            report.issues << QObject::tr("检测到 %1 条退化边。").arg(report.degeneratedEdgeCount);
        if (report.smallEdgeCount > 0)
            report.issues << QObject::tr("检测到 %1 条小边。").arg(report.smallEdgeCount);
        if (report.smallFaceCount > 0)
            report.issues << QObject::tr("检测到 %1 个小面。").arg(report.smallFaceCount);
        if (report.maxTolerance > std::max(options.repairTolerance * 10.0, 1.0e-4))
            report.issues << QObject::tr("几何误差较大：%1。").arg(report.maxTolerance, 0, 'g', 8);
    }
    catch (const Standard_Failure& failure)
    {
        report.valid = false;
        report.issues << QObject::tr("检查失败：%1").arg(QString::fromLocal8Bit(failure.GetMessageString()));
    }
    catch (const std::exception& exception)
    {
        report.valid = false;
        report.issues << QObject::tr("检查失败：%1").arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
        report.valid = false;
        report.issues << QObject::tr("检查失败：未知异常。");
    }

    return report;
}

bool repair(const TopoDS_Shape& source,
            const CheckOptions& options,
            TopoDS_Shape& repaired,
            QStringList& actions,
            QString& error)
{
    repaired.Nullify();
    actions.clear();
    error.clear();
    if (source.IsNull())
    {
        error = QObject::tr("几何为空。");
        return false;
    }

    try
    {
        TopoDS_Shape candidate = source;

        const Standard_Real repairTolerance =
            std::max<Standard_Real>(options.repairTolerance, Precision::Confusion());

        ShapeFix_Shape fixer(candidate);
        fixer.SetPrecision(repairTolerance);
        fixer.SetMinTolerance(Precision::Confusion());
        const Standard_Real existingMaxTol = maxVertexTolerance(candidate);
        fixer.SetMaxTolerance(std::max<Standard_Real>(
            std::max<Standard_Real>(repairTolerance * 100.0, 1.0e-4),
            existingMaxTol * 1.05));
        // OCCT 默认关闭顶点位置修复。STEP 中常见的 tolerance 级 Edge-Face
        // 自干涉正需要该官方模式；修复后本函数仍会执行 BRep/BOP 双重验证。
        fixer.FixVertexPositionMode() = 1;
        fixer.Perform();
        if (!fixer.Shape().IsNull())
        {
            candidate = fixer.Shape();
            actions << QObject::tr("修复几何方向、顶点位置、误差和连接关系");
        }

        TopTools_IndexedMapOfShape solidMap;
        TopExp::MapShapes(candidate, TopAbs_SOLID, solidMap);
        TopTools_IndexedMapOfShape faceMap;
        TopExp::MapShapes(candidate, TopAbs_FACE, faceMap);
        if (options.sewFaces && solidMap.Extent() == 0 && faceMap.Extent() > 1)
        {
            BRepBuilderAPI_Sewing sewing(repairTolerance,
                                        Standard_True, Standard_True, Standard_True, Standard_False);
            sewing.Add(candidate);
            sewing.Perform();
            const TopoDS_Shape sewed = sewing.SewedShape();
            if (!sewed.IsNull())
            {
                candidate = sewed;
                actions << QObject::tr("缝合相邻曲面");

                // Sewing 常返回 Shell。若它已经形成一个有效闭壳，则转换为 Solid，
                // 避免“看起来封闭但仍只是壳”导致后续布尔/网格失败。
                TopTools_IndexedMapOfShape sewnSolids;
                TopExp::MapShapes(candidate, TopAbs_SOLID, sewnSolids);
                if (sewnSolids.Extent() == 0)
                {
                    TopTools_IndexedMapOfShape sewnShells;
                    TopExp::MapShapes(candidate, TopAbs_SHELL, sewnShells);
                    if (sewnShells.Extent() == 1)
                    {
                        const TopoDS_Shell shell = TopoDS::Shell(sewnShells(1));
                        BRepBuilderAPI_MakeSolid makeSolid(shell);
                        const TopoDS_Solid solid = makeSolid.Solid();
                        if (!solid.IsNull())
                        {
                            BRepCheck_Analyzer solidAnalyzer(solid, Standard_True);
                            if (solidAnalyzer.IsValid())
                            {
                                candidate = solid;
                                actions << QObject::tr("将闭合壳转换为实体");
                            }
                        }
                    }
                }
            }
        }

        if (options.unifySameDomain)
        {
            ShapeUpgrade_UnifySameDomain unify(candidate, Standard_True, Standard_True, Standard_True);
            unify.SetLinearTolerance(std::max(options.repairTolerance, Precision::Confusion()));
            unify.Build();
            const TopoDS_Shape unified = unify.Shape();
            if (!unified.IsNull())
            {
                candidate = unified;
                actions << QObject::tr("合并同域面和多余分割边");
            }
        }

        BRepCheck_Analyzer analyzer(candidate, Standard_True);
        if (!analyzer.IsValid())
        {
            error = QObject::tr("自动修复后 BRep 拓扑仍未通过检查，原结果已保留。");
            return false;
        }

        BRepAlgoAPI_Check bopCheck(candidate, Standard_False, Standard_True);
        if (!bopCheck.IsValid())
        {
            error = QObject::tr(
                "自动修复后仍存在自干涉或 Boolean 前置检查失败；"
                "ShapeFix 不能安全消除此类几何交叉，原结果已保留。");
            return false;
        }

        repaired = candidate;
        return !repaired.IsNull();
    }
    catch (const Standard_Failure& failure)
    {
        error = QObject::tr("修复失败：%1").arg(QString::fromLocal8Bit(failure.GetMessageString()));
    }
    catch (const std::exception& exception)
    {
        error = QObject::tr("修复失败：%1").arg(QString::fromLocal8Bit(exception.what()));
    }
    catch (...)
    {
        error = QObject::tr("修复失败：未知异常。");
    }
    return false;
}

bool entityShape(const TopoDS_Shape& root, int dim, int id, TopoDS_Shape& out)
{
    out.Nullify();
    if (root.IsNull() || id <= 0 || dim < 0 || dim > 3)
        return false;

    if (dim == 0)
    {
        out = root;
        return true;
    }

    TopAbs_ShapeEnum type = TopAbs_EDGE;
    if (dim == 2)
        type = TopAbs_FACE;
    else if (dim == 3)
        type = TopAbs_SOLID;

    TopTools_IndexedMapOfShape indexed;
    TopExp::MapShapes(root, type, indexed);
    if (id > indexed.Extent())
        return false;

    out = indexed(id);
    return !out.IsNull();
}

EntityInfo entityInfo(const TopoDS_Shape& root, int dim, int id)
{
    EntityInfo info;
    TopoDS_Shape shape;
    if (!entityShape(root, dim, id, shape))
        return info;

    try
    {
        info.valid = true;
        info.typeName = dim == 0 ? QObject::tr("整体")
                                 : dim == 1 ? QObject::tr("边 %1").arg(id)
                                            : dim == 2 ? QObject::tr("面 %1").arg(id)
                                                       : QObject::tr("体 %1").arg(id);

        if (dim == 0)
        {
            const CheckReport report = inspect(shape);
            info.geometryType = report.shapeType;
            info.measure = report.solidCount > 0 ? report.totalVolume
                                                 : report.faceCount > 0 ? report.totalArea
                                                                      : report.totalLength;
            info.center = report.center;
            info.boundsMin = report.boundsMin;
            info.boundsMax = report.boundsMax;
            info.properties << QObject::tr("总边长：%1").arg(report.totalLength, 0, 'g', 10)
                            << QObject::tr("总面积：%1").arg(report.totalArea, 0, 'g', 10)
                            << QObject::tr("总体积：%1").arg(report.totalVolume, 0, 'g', 10)
                            << QObject::tr("点/边/面/体：%1 / %2 / %3 / %4")
                                   .arg(report.vertexCount).arg(report.edgeCount)
                                   .arg(report.faceCount).arg(report.solidCount);
            return info;
        }

        GProp_GProps props;
        massProperties(shape, dim, props);
        info.measure = props.Mass();
        const gp_Pnt center = props.CentreOfMass();
        info.center = QVector3D(center.X(), center.Y(), center.Z());
        fillBounds(shape, info.boundsMin, info.boundsMax);

        if (dim == 1)
        {
            const TopoDS_Edge edge = TopoDS::Edge(shape);
            BRepAdaptor_Curve curve(edge);
            info.geometryType = curveTypeName(curve.GetType());
            if (curve.GetType() == GeomAbs_Circle)
                info.properties << QObject::tr("半径：%1").arg(curve.Circle().Radius(), 0, 'g', 10);
            else if (curve.GetType() == GeomAbs_Ellipse)
            {
                info.properties << QObject::tr("长半轴：%1").arg(curve.Ellipse().MajorRadius(), 0, 'g', 10)
                                << QObject::tr("短半轴：%1").arg(curve.Ellipse().MinorRadius(), 0, 'g', 10);
            }

            TopoDS_Vertex first;
            TopoDS_Vertex last;
            TopExp::Vertices(edge, first, last, Standard_True);
            if (!first.IsNull() && !last.IsNull())
            {
                const gp_Pnt p1 = BRep_Tool::Pnt(first);
                const gp_Pnt p2 = BRep_Tool::Pnt(last);
                info.properties << QObject::tr("起点：(%1, %2, %3)")
                                       .arg(p1.X(), 0, 'g', 8).arg(p1.Y(), 0, 'g', 8).arg(p1.Z(), 0, 'g', 8)
                                << QObject::tr("终点：(%1, %2, %3)")
                                       .arg(p2.X(), 0, 'g', 8).arg(p2.Y(), 0, 'g', 8).arg(p2.Z(), 0, 'g', 8);
            }
        }
        else if (dim == 2)
        {
            const TopoDS_Face face = TopoDS::Face(shape);
            BRepAdaptor_Surface surface(face, Standard_True);
            info.geometryType = surfaceTypeName(surface.GetType());
            if (surface.GetType() == GeomAbs_Plane)
            {
                gp_Dir normal = surface.Plane().Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED)
                    normal.Reverse();
                info.properties << QObject::tr("法向：(%1, %2, %3)")
                                       .arg(normal.X(), 0, 'g', 8)
                                       .arg(normal.Y(), 0, 'g', 8)
                                       .arg(normal.Z(), 0, 'g', 8);
            }
            else if (surface.GetType() == GeomAbs_Cylinder)
                info.properties << QObject::tr("半径：%1").arg(surface.Cylinder().Radius(), 0, 'g', 10);
            else if (surface.GetType() == GeomAbs_Sphere)
                info.properties << QObject::tr("半径：%1").arg(surface.Sphere().Radius(), 0, 'g', 10);
            else if (surface.GetType() == GeomAbs_Cone)
            {
                info.properties << QObject::tr("参考半径：%1").arg(surface.Cone().RefRadius(), 0, 'g', 10)
                                << QObject::tr("半角：%1°").arg(qRadiansToDegrees(surface.Cone().SemiAngle()), 0, 'g', 8);
            }
            else if (surface.GetType() == GeomAbs_Torus)
            {
                info.properties << QObject::tr("主半径：%1").arg(surface.Torus().MajorRadius(), 0, 'g', 10)
                                << QObject::tr("截面半径：%1").arg(surface.Torus().MinorRadius(), 0, 'g', 10);
            }
        }
        else
        {
            info.geometryType = QObject::tr("实体");
        }
    }
    catch (...)
    {
        info.valid = false;
    }
    return info;
}

PairMeasureResult measurePair(const TopoDS_Shape& rootA, int dimA, int idA,
                              const TopoDS_Shape& rootB, int dimB, int idB)
{
    PairMeasureResult result;
    TopoDS_Shape shapeA;
    TopoDS_Shape shapeB;
    if (!entityShape(rootA, dimA, idA, shapeA) || !entityShape(rootB, dimB, idB, shapeB))
    {
        result.message = QObject::tr("请选择两个有效实体。");
        return result;
    }

    try
    {
        BRepExtrema_DistShapeShape distance(shapeA, shapeB);
        distance.Perform();
        if (!distance.IsDone() || distance.NbSolution() <= 0)
        {
            result.message = QObject::tr("最短距离计算失败。");
            return result;
        }

        result.valid = true;
        result.distance = distance.Value();
        const gp_Pnt p1 = distance.PointOnShape1(1);
        const gp_Pnt p2 = distance.PointOnShape2(1);
        result.pointA = QVector3D(p1.X(), p1.Y(), p1.Z());
        result.pointB = QVector3D(p2.X(), p2.Y(), p2.Z());

        gp_Dir directionA(1, 0, 0);
        gp_Dir directionB(1, 0, 0);
        if (directionForShape(shapeA, directionA) && directionForShape(shapeB, directionB))
        {
            const double dot = std::clamp(std::abs(directionA.Dot(directionB)), 0.0, 1.0);
            double angleRad = std::acos(dot);

            // 对平面，directionForShape() 返回法向。
            // 线-线、面-面直接取两方向夹角；线-面应取“直线与平面”夹角，
            // 即 90° 减去直线与平面法向的夹角。
            const bool linePlanePair =
                (shapeA.ShapeType() == TopAbs_EDGE && shapeB.ShapeType() == TopAbs_FACE) ||
                (shapeA.ShapeType() == TopAbs_FACE && shapeB.ShapeType() == TopAbs_EDGE);
            if (linePlanePair)
                angleRad = 1.57079632679489661923 - angleRad;

            result.hasAngle = true;
            result.angleDeg = qRadiansToDegrees(std::max(0.0, angleRad));
        }
    }
    catch (const Standard_Failure& failure)
    {
        result.message = QObject::tr("测量失败：%1").arg(QString::fromLocal8Bit(failure.GetMessageString()));
    }
    catch (...)
    {
        result.message = QObject::tr("测量失败：未知异常。");
    }
    return result;
}
}
