#include "SketchShapeBuilder.h"

#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <TColgp_HArray1OfPnt.hxx>
#include <ShapeAnalysis_FreeBounds.hxx>
#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <gp_Ax2.hxx>
#include <gp_Circ.hxx>
#include <gp_Dir.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Wire.hxx>
#include <TopoDS_Edge.hxx>
#include <TopTools_HSequenceOfShape.hxx>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <TopoDS.hxx>

namespace GeometrySketch
{

namespace
{

gp_Dir normalOf(const PlaneSettings& settings)
{
    switch (settings.plane)
    {
    case Plane::XY:
        return gp_Dir(0.0, 0.0, 1.0);
    case Plane::XZ:
        return gp_Dir(0.0, 1.0, 0.0);
    case Plane::YZ:
        return gp_Dir(1.0, 0.0, 0.0);
    }
    return gp_Dir(0.0, 0.0, 1.0);
}

ShapeBuildResult fail(const QString& message)
{
    ShapeBuildResult result;
    result.ok = false;
    result.message = message;
    return result;
}


double pointDistance(const Point2D& a, const Point2D& b)
{
    return std::hypot(a.u - b.u, a.v - b.v);
}

Point2D lerp(const Point2D& a, const Point2D& b, double t)
{
    return {a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
}

Point2D safeBlend(const Point2D& a, const Point2D& b, double ta, double tb, double t)
{
    const double denom = tb - ta;
    if (std::abs(denom) <= 1.0e-12)
        return b;
    const double wa = (tb - t) / denom;
    const double wb = (t - ta) / denom;
    return {a.u * wa + b.u * wb, a.v * wa + b.v * wb};
}

Point2D centripetalCatmullPoint(const Point2D& p0, const Point2D& p1,
                                const Point2D& p2, const Point2D& p3,
                                double localT)
{
    // 向心 Catmull-Rom：比普通 Catmull-Rom 更不容易甩线、打圈。
    // p1 -> p2 是当前段，localT 范围 [0, 1]。
    constexpr double alpha = 0.5;
    auto nextT = [](double t, const Point2D& a, const Point2D& b)
    {
        const double d = std::hypot(a.u - b.u, a.v - b.v);
        return t + std::pow(std::max(d, 1.0e-9), alpha);
    };

    const double t0 = 0.0;
    const double t1 = nextT(t0, p0, p1);
    const double t2 = nextT(t1, p1, p2);
    const double t3 = nextT(t2, p2, p3);
    const double t = t1 + (t2 - t1) * std::clamp(localT, 0.0, 1.0);

    const Point2D a1 = safeBlend(p0, p1, t0, t1, t);
    const Point2D a2 = safeBlend(p1, p2, t1, t2, t);
    const Point2D a3 = safeBlend(p2, p3, t2, t3, t);
    const Point2D b1 = safeBlend(a1, a2, t0, t2, t);
    const Point2D b2 = safeBlend(a2, a3, t1, t3, t);
    return safeBlend(b1, b2, t1, t2, t);
}

void appendIfFar(std::vector<Point2D>& out, const Point2D& p, double eps)
{
    if (out.empty() || pointDistance(out.back(), p) > eps)
        out.push_back(p);
}

double signedAngleSweep(double startAngle, double endAngle)
{
    double sweep = endAngle - startAngle;
    constexpr double twoPi = 6.28318530717958647692;
    constexpr double pi = 3.14159265358979323846;
    while (sweep < -pi) sweep += twoPi;
    while (sweep > pi) sweep -= twoPi;
    return sweep;
}

double clampArcSweep(double sweep)
{
    constexpr double twoPi = 6.28318530717958647692;
    constexpr double maxSweep = twoPi - 1.0e-5;
    return std::clamp(sweep, -maxSweep, maxSweep);
}

Point2D centerAngleEndpointForSweep(const Point2D& start, const Point2D& center, double sweep)
{
    const double radius = pointDistance(start, center);
    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = a0 + sweep;
    return {center.u + radius * std::cos(a1), center.v + radius * std::sin(a1)};
}

bool centerAngleArcThroughPoints(const std::vector<Point2D>& src,
                                 Point2D& start,
                                 Point2D& mid,
                                 Point2D& end,
                                 double arcSweep = 0.0)
{
    if (src.size() != 3)
        return false;

    start = src[0];
    const Point2D center = src[1];
    const Point2D anglePoint = src[2];
    const double radius = pointDistance(start, center);
    const double au = anglePoint.u - center.u;
    const double av = anglePoint.v - center.v;
    const double angleLen = std::hypot(au, av);
    if (radius <= 1.0e-12 || angleLen <= 1.0e-12)
        return false;

    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = std::atan2(av, au);
    const double sweep = std::abs(arcSweep) > 1.0e-12
                             ? clampArcSweep(arcSweep)
                             : signedAngleSweep(a0, a1);
    if (std::abs(sweep) <= 1.0e-12)
        return false;

    const double midAngle = a0 + sweep * 0.5;
    mid = {center.u + radius * std::cos(midAngle), center.v + radius * std::sin(midAngle)};
    end = std::abs(arcSweep) > 1.0e-12
              ? centerAngleEndpointForSweep(start, center, sweep)
              : Point2D{center.u + radius * au / angleLen, center.v + radius * av / angleLen};
    return true;
}

Point2D tangentArcMidPoint(const Point2D& start, const Point2D& tangentPoint, const Point2D& end, bool& ok)
{
    ok = false;
    const double tx0 = tangentPoint.u - start.u;
    const double ty0 = tangentPoint.v - start.v;
    const double len = std::hypot(tx0, ty0);
    if (len <= 1.0e-12)
        return {};

    const double tx = tx0 / len;
    const double ty = ty0 / len;
    const double nx = -ty;
    const double ny = tx;
    const double dx = end.u - start.u;
    const double dy = end.v - start.v;
    const double denom = 2.0 * (nx * dx + ny * dy);
    const double d2 = dx * dx + dy * dy;
    if (std::abs(denom) <= 1.0e-12 || d2 <= 1.0e-18)
        return {};

    const double a = d2 / denom;
    const double cu = start.u + nx * a;
    const double cv = start.v + ny * a;
    const double radius = std::abs(a);
    if (radius <= 1.0e-12)
        return {};

    auto angleOf = [&](const Point2D& pnt) { return std::atan2(pnt.v - cv, pnt.u - cu); };
    auto normalizePositive = [](double value)
    {
        constexpr double twoPi = 6.28318530717958647692;
        while (value < 0.0) value += twoPi;
        while (value >= twoPi) value -= twoPi;
        return value;
    };

    const double a0 = angleOf(start);
    const double a1 = angleOf(end);
    const double ccwTx = -std::sin(a0);
    const double ccwTy = std::cos(a0);
    const bool useCcw = (ccwTx * tx + ccwTy * ty) >= 0.0;
    const double sweep = useCcw ? normalizePositive(a1 - a0)
                                : -normalizePositive(a0 - a1);
    if (std::abs(sweep) <= 1.0e-12)
        return {};

    const double midAngle = a0 + sweep * 0.5;
    ok = true;
    return {cu + radius * std::cos(midAngle), cv + radius * std::sin(midAngle)};
}

TopoDS_Edge makeProfileSegmentEdge(const SketchDocument::ProfileSegment& segment,
                                   const PlaneSettings& planeSettings,
                                   QString& error)
{
    const gp_Pnt start = toWorldPoint(planeSettings, segment.start.u, segment.start.v);
    const gp_Pnt end = toWorldPoint(planeSettings, segment.end.u, segment.end.v);

    if (segment.kind == SketchDocument::ProfileSegmentKind::Line)
    {
        BRepBuilderAPI_MakeEdge edgeMaker(start, end);
        if (!edgeMaker.IsDone())
            error = QStringLiteral("直线段生成失败。");
        return edgeMaker.Edge();
    }

    Point2D mid2d = segment.control;
    if (segment.kind == SketchDocument::ProfileSegmentKind::ArcTangent)
    {
        bool ok = false;
        mid2d = tangentArcMidPoint(segment.start, segment.control, segment.end, ok);
        if (!ok)
        {
            error = QStringLiteral("切线圆弧生成失败，请重新选择切线方向和终点。");
            return {};
        }
    }

    const gp_Pnt mid = toWorldPoint(planeSettings, mid2d.u, mid2d.v);
    GC_MakeArcOfCircle arcMaker(start, mid, end);
    if (!arcMaker.IsDone())
    {
        error = QStringLiteral("圆弧段生成失败，请检查构造点是否过近或共线。");
        return {};
    }

    BRepBuilderAPI_MakeEdge edgeMaker(arcMaker.Value());
    if (!edgeMaker.IsDone())
        error = QStringLiteral("圆弧边生成失败。");
    return edgeMaker.Edge();
}

bool addClosingLineIfNeeded(BRepBuilderAPI_MakeWire& wireMaker,
                            const SketchDocument& document,
                            const PlaneSettings& planeSettings,
                            bool closed,
                            QString& error)
{
    const auto& segments = document.profileSegments();
    if (segments.empty() || !closed)
        return true;

    const Point2D first = segments.front().start;
    const Point2D last = segments.back().end;
    if (pointDistance(first, last) <= 1.0e-7)
        return true;

    BRepBuilderAPI_MakeEdge closeEdge(toWorldPoint(planeSettings, last.u, last.v),
                                      toWorldPoint(planeSettings, first.u, first.v));
    if (!closeEdge.IsDone())
    {
        error = QStringLiteral("闭合线段生成失败。");
        return false;
    }
    wireMaker.Add(closeEdge.Edge());
    return true;
}

bool profileClosedForBuild(const SketchDocument& document)
{
    if (document.kind() != SketchDocument::Kind::Profile)
        return false;
    if (document.isClosed())
        return true;

    if (document.hasProfileSegments())
    {
        const auto& segments = document.profileSegments();
        if (!segments.empty())
            return pointDistance(segments.front().start, segments.back().end) <= 1.0e-7;
    }

    const auto& points = document.points();
    return points.size() >= 3 && pointDistance(points.front(), points.back()) <= 1.0e-7;
}

std::vector<Point2D> normalizedProfilePointsForBuild(const SketchDocument& document, bool closed)
{
    std::vector<Point2D> points = document.points();
    if (closed && points.size() >= 2 && pointDistance(points.front(), points.back()) <= 1.0e-7)
        points.pop_back();
    return points;
}

ShapeBuildResult buildConnectedClosedProfile(const std::vector<SketchDocument>& documents,
                                             const PlaneSettings& planeSettings)
{
    if (documents.size() < 2)
        return fail(QString());

    const double tol = std::max(1.0e-7, planeSettings.gridStep * 1.0e-4);
    PlaneSettings curveSettings = planeSettings;
    curveSettings.outputMode = OutputMode::Curve;

    BRep_Builder builder;
    TopoDS_Compound edgeCompound;
    builder.MakeCompound(edgeCompound);
    int sourceEdgeCount = 0;

    for (const SketchDocument& document : documents)
    {
        if (!document.isRenderable())
            continue;
        const ShapeBuildResult one = SketchShapeBuilder::build(document, curveSettings);
        if (!one.ok || one.shape.IsNull())
            return fail(QString());
        for (TopExp_Explorer exp(one.shape, TopAbs_EDGE); exp.More(); exp.Next())
        {
            builder.Add(edgeCompound, exp.Current());
            ++sourceEdgeCount;
        }
    }

    if (sourceEdgeCount < 2)
        return fail(QString());

    Handle(TopTools_HSequenceOfShape) edges = new TopTools_HSequenceOfShape();
    for (TopExp_Explorer exp(edgeCompound, TopAbs_EDGE); exp.More(); exp.Next())
        edges->Append(exp.Current());

    Handle(TopTools_HSequenceOfShape) wires = new TopTools_HSequenceOfShape();
    ShapeAnalysis_FreeBounds::ConnectEdgesToWires(edges, tol, Standard_False, wires);
    if (wires->IsEmpty())
        return fail(QString());

    TopoDS_Compound closedWires;
    TopoDS_Compound openWires;
    ShapeAnalysis_FreeBounds::DispatchWires(wires, closedWires, openWires);

    TopoDS_Wire closedWire;
    int closedCount = 0;
    for (TopExp_Explorer exp(closedWires, TopAbs_WIRE); exp.More(); exp.Next())
    {
        closedWire = TopoDS::Wire(exp.Current());
        ++closedCount;
    }
    int openCount = 0;
    for (TopExp_Explorer exp(openWires, TopAbs_WIRE); exp.More(); exp.Next())
        ++openCount;

    if (closedCount != 1 || openCount != 0 || closedWire.IsNull())
        return fail(QString());

    BRepBuilderAPI_MakeFace faceMaker(closedWire, Standard_True);
    faceMaker.Build();
    if (!faceMaker.IsDone() || faceMaker.Shape().IsNull())
        return fail(QString());

    ShapeBuildResult result;
    result.ok = true;
    result.shape = faceMaker.Shape();
    result.message = QStringLiteral("已将多个开放草图对象按真实曲线组合为闭合草图面。");
    return result;
}

} // namespace


std::vector<Point2D> SketchShapeBuilder::sampleCurvePoints(const std::vector<Point2D>& input)
{
    if (input.size() < 2)
        return input;

    std::vector<Point2D> src;
    src.reserve(input.size());
    for (const auto& p : input)
        appendIfFar(src, p, 1.0e-9);

    if (src.size() < 3)
        return src;

    std::vector<Point2D> out;
    const int samplesPerSegment = 16;
    out.reserve((src.size() - 1) * samplesPerSegment + 1);

    for (std::size_t i = 0; i + 1 < src.size(); ++i)
    {
        const Point2D& p0 = (i == 0) ? src[i] : src[i - 1];
        const Point2D& p1 = src[i];
        const Point2D& p2 = src[i + 1];
        const Point2D& p3 = (i + 2 < src.size()) ? src[i + 2] : src[i + 1];

        // 端点处使用重复端点，保证曲线从首个点击点开始，到最后一个点击点结束。
        for (int j = 0; j < samplesPerSegment; ++j)
        {
            const double t = static_cast<double>(j) / static_cast<double>(samplesPerSegment);
            appendIfFar(out, centripetalCatmullPoint(p0, p1, p2, p3, t), 1.0e-9);
        }
    }
    appendIfFar(out, src.back(), 1.0e-9);
    return out;
}

ShapeBuildResult SketchShapeBuilder::build(const SketchDocument& document, const PlaneSettings& planeSettings)
{
    if (!document.isRenderable())
        return fail(QStringLiteral("草图点数不足，无法生成几何。"));

    try
    {
        if (document.kind() == SketchDocument::Kind::Circle)
        {
            const auto& center = document.points().front();
            const double radius = document.circleRadius();
            if (radius <= 1.0e-9)
                return fail(QStringLiteral("圆半径必须大于 0。"));

            gp_Circ circle(gp_Ax2(toWorldPoint(planeSettings, center.u, center.v), normalOf(planeSettings)), radius);
            BRepBuilderAPI_MakeEdge edgeMaker(circle);
            if (!edgeMaker.IsDone())
                return fail(QStringLiteral("圆边生成失败。"));

            BRepBuilderAPI_MakeWire wireMaker(edgeMaker.Edge());
            if (!wireMaker.IsDone() || wireMaker.Wire().IsNull())
                return fail(QStringLiteral("圆轮廓生成失败。"));

            BRepBuilderAPI_MakeFace faceMaker(wireMaker.Wire());
            faceMaker.Build();

            ShapeBuildResult result;
            result.ok = true;
            if (planeSettings.outputMode == OutputMode::Entity && faceMaker.IsDone())
                result.shape = faceMaker.Shape();
            else
                result.shape = wireMaker.Wire();
            result.message = (planeSettings.outputMode == OutputMode::Entity && faceMaker.IsDone())
                                 ? QStringLiteral("已生成圆实体。")
                                 : QStringLiteral("已生成圆曲线。");
            return result;
        }


        if (document.kind() == SketchDocument::Kind::Arc)
        {
            const auto& points = document.points();
            if (points.size() != 3)
                return fail(QStringLiteral("圆弧需要 3 个点：起点、经过点、终点。"));

            Point2D start = points[0];
            Point2D mid = points[1];
            Point2D end = points[2];
            if (document.arcKind() == SketchDocument::ArcKind::CenterAngle &&
                !centerAngleArcThroughPoints(points, start, mid, end, document.arcSweep()))
            {
                return fail(QStringLiteral("圆弧半径或角度过小。"));
            }

            const gp_Pnt p1 = toWorldPoint(planeSettings, start.u, start.v);
            const gp_Pnt p2 = toWorldPoint(planeSettings, mid.u, mid.v);
            const gp_Pnt p3 = toWorldPoint(planeSettings, end.u, end.v);
            GC_MakeArcOfCircle arcMaker(p1, p2, p3);
            if (!arcMaker.IsDone())
                return fail(QStringLiteral("圆弧生成失败，请检查 3 个点是否共线或过近。"));

            BRepBuilderAPI_MakeEdge edgeMaker(arcMaker.Value());
            if (!edgeMaker.IsDone())
                return fail(QStringLiteral("圆弧边生成失败。"));

            BRepBuilderAPI_MakeWire wireMaker(edgeMaker.Edge());
            if (!wireMaker.IsDone() || wireMaker.Wire().IsNull())
                return fail(QStringLiteral("圆弧线框生成失败。"));

            ShapeBuildResult result;
            result.ok = true;
            result.shape = wireMaker.Wire();
            result.message = QStringLiteral("已生成圆弧。");
            return result;
        }

        if (document.kind() == SketchDocument::Kind::Curve)
        {
            const auto& points = document.points();
            if (points.size() < 3)
                return fail(QStringLiteral("样条至少需要 3 个点。"));

            std::vector<Point2D> uniquePoints;
            uniquePoints.reserve(points.size());
            for (const Point2D& point : points)
                appendIfFar(uniquePoints, point, 1.0e-9);
            if (uniquePoints.size() < 3)
                return fail(QStringLiteral("样条生成失败，请检查点是否过近或重复。"));

            Handle(TColgp_HArray1OfPnt) interpolationPoints =
                new TColgp_HArray1OfPnt(1, static_cast<Standard_Integer>(uniquePoints.size()));
            for (Standard_Integer i = 1; i <= interpolationPoints->Length(); ++i)
            {
                const Point2D& point = uniquePoints[static_cast<std::size_t>(i - 1)];
                interpolationPoints->SetValue(i, toWorldPoint(planeSettings, point.u, point.v));
            }

            GeomAPI_Interpolate interpolation(interpolationPoints, Standard_False, 1.0e-7);
            interpolation.Perform();
            if (!interpolation.IsDone() || interpolation.Curve().IsNull())
                return fail(QStringLiteral("样条插值失败，请调整控制点。"));

            BRepBuilderAPI_MakeEdge edgeMaker(interpolation.Curve());
            if (!edgeMaker.IsDone() || edgeMaker.Edge().IsNull())
                return fail(QStringLiteral("样条边生成失败。"));

            BRepBuilderAPI_MakeWire wireMaker(edgeMaker.Edge());
            if (!wireMaker.IsDone() || wireMaker.Wire().IsNull())
                return fail(QStringLiteral("样条线框生成失败。"));

            ShapeBuildResult result;
            result.ok = true;
            result.shape = wireMaker.Wire();
            result.message = QStringLiteral("已生成真实 B 样条曲线。");
            return result;
        }

        if (document.kind() == SketchDocument::Kind::Profile)
        {
            const bool closed = profileClosedForBuild(document);

            // 连续手绘轮廓可能只有 2 个真实顶点，但由“直线段 + 圆弧段”闭合。
            // 例如半圆 D 形：P1 -> P2 是直线，P2 -> P1 是圆弧。
            // 这种情况下不能先按点数判断“闭合轮廓至少 3 点”，否则会在真正构造
            // OCC 圆弧边之前直接失败，表现为闭合后圆弧消失。
            if (document.hasProfileSegments())
            {
                if (document.profileSegments().empty())
                    return fail(QStringLiteral("轮廓段为空。"));

                BRepBuilderAPI_MakeWire wireMaker;
                for (const auto& segment : document.profileSegments())
                {
                    QString error;
                    const TopoDS_Edge edge = makeProfileSegmentEdge(segment, planeSettings, error);
                    if (!error.isEmpty() || edge.IsNull())
                        return fail(error.isEmpty() ? QStringLiteral("轮廓段生成失败。") : error);
                    wireMaker.Add(edge);
                }

                QString closeError;
                if (!addClosingLineIfNeeded(wireMaker, document, planeSettings, closed, closeError))
                    return fail(closeError);

                if (!wireMaker.IsDone() || wireMaker.Wire().IsNull())
                    return fail(QStringLiteral("轮廓线生成失败。"));

                if (planeSettings.outputMode == OutputMode::Entity && closed)
                {
                    BRepBuilderAPI_MakeFace faceMaker(wireMaker.Wire());
                    faceMaker.Build();
                    if (faceMaker.IsDone() && !faceMaker.Shape().IsNull())
                    {
                        ShapeBuildResult result;
                        result.ok = true;
                        result.shape = faceMaker.Shape();
                        result.message = QStringLiteral("已生成草图实体。");
                        return result;
                    }
                    return fail(QStringLiteral("闭合轮廓生成面失败，请检查线段与圆弧是否首尾相接。"));
                }

                ShapeBuildResult result;
                result.ok = true;
                result.shape = wireMaker.Wire();
                result.message = QStringLiteral("已生成草图曲线。");
                return result;
            }

            const std::vector<Point2D> points = normalizedProfilePointsForBuild(document, closed);
            if (points.size() < (closed ? 3U : 2U))
                return fail(QStringLiteral("轮廓点数不足。"));

            BRepBuilderAPI_MakePolygon polygon;
            for (const auto& p : points)
                polygon.Add(toWorldPoint(planeSettings, p.u, p.v));

            if (closed)
                polygon.Close();

            if (!polygon.IsDone() || polygon.Wire().IsNull())
                return fail(QStringLiteral("轮廓线生成失败。"));

            if (planeSettings.outputMode == OutputMode::Entity && closed)
            {
                BRepBuilderAPI_MakeFace faceMaker(polygon.Wire());
                faceMaker.Build();
                if (faceMaker.IsDone())
                {
                    ShapeBuildResult result;
                    result.ok = true;
                    result.shape = faceMaker.Shape();
                    result.message = QStringLiteral("已生成草图实体。");
                    return result;
                }
            }

            ShapeBuildResult result;
            result.ok = true;
            result.shape = polygon.Wire();
            result.message = planeSettings.outputMode == OutputMode::Entity && closed
                                 ? QStringLiteral("实体生成失败，已生成曲线。")
                                 : QStringLiteral("已生成草图曲线。") ;
            return result;
        }
    }
    catch (const std::exception& e)
    {
        return fail(QStringLiteral("OCC 生成异常：%1").arg(e.what()));
    }
    catch (...)
    {
        return fail(QStringLiteral("OCC 生成异常。"));
    }

    return fail(QStringLiteral("当前草图类型暂不支持生成几何。"));
}

ShapeBuildResult SketchShapeBuilder::buildMany(const std::vector<SketchDocument>& documents,
                                               const PlaneSettings& planeSettings)
{
    // 手绘轮廓常常由“直线 + 圆弧 + 曲线”等多个开放草图对象组成。
    // 如果这些对象端点可以首尾连接成闭合环，优先组合成一个面，
    // 这样胶囊形、槽口等轮廓可以手绘后直接拉伸，而不是只能得到一堆独立线段。
    if (planeSettings.outputMode == OutputMode::Entity && documents.size() > 1)
    {
        ShapeBuildResult connected = buildConnectedClosedProfile(documents, planeSettings);
        if (connected.ok && !connected.shape.IsNull())
            return connected;
    }

    std::vector<TopoDS_Shape> shapes;
    QStringList messages;

    for (const auto& document : documents)
    {
        if (!document.isRenderable())
            continue;

        ShapeBuildResult one = build(document, planeSettings);
        if (!one.ok || one.shape.IsNull())
            return one;

        shapes.push_back(one.shape);
        messages.push_back(one.message);
    }

    if (shapes.empty())
        return fail(QStringLiteral("草图点数不足，无法生成几何。"));

    ShapeBuildResult result;
    result.ok = true;
    result.message = shapes.size() == 1
                         ? messages.value(0)
                         : QStringLiteral("已生成 %1 个草图对象。").arg(shapes.size());

    if (shapes.size() == 1)
    {
        result.shape = shapes.front();
        return result;
    }

    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto& shape : shapes)
        builder.Add(compound, shape);

    result.shape = compound;
    return result;
}

} // namespace GeometrySketch
