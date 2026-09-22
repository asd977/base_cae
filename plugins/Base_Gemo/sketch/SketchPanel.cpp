#include "SketchPanel.h"
#include "../builders/ImportGeometryBuilder.h"
#include "../panels/GeometryPanelUi.h"
#include "../workbench/GeometryWorkbench.h"

#include <EventManager.h>
#include <GeometryCleanup.h>
#include <GeometryDisplayManager.h>
#include <GeometryFeatureRegistry.h>
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include "CadDocument.h"
#include "CadBody.h"
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/PreProcessingService.h>
#include <services/VisualWidgetService.h>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QCursor>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QItemSelectionModel>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QWidget>

#include "ui_SketchPanel.h"

#include <BRepAdaptor_Curve.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_CurveType.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Circ.hxx>
#include <vtkActor.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

using GeometrySketch::Command;
using GeometrySketch::OutputMode;
using GeometrySketch::Plane;
using GeometrySketch::PlaneSettings;
using GeometrySketch::Point2D;
using GeometrySketch::SketchDocument;
using GeometrySketch::SketchShapeBuilder;

namespace
{

VisualWidgetService* readyVisualService()
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    return (vis && vis->renderer()) ? vis : nullptr;
}

QString commandText(Command command)
{
    switch (command)
    {
    case Command::Polyline: return QStringLiteral("折线轮廓");
    case Command::Rectangle: return QStringLiteral("矩形");
    case Command::Circle: return QStringLiteral("圆");
    case Command::Arc: return QStringLiteral("圆弧");
    case Command::Curve: return QStringLiteral("平滑样条");
    case Command::Point: return QStringLiteral("辅助点");
    case Command::Select:
    default: return QStringLiteral("选择");
    }
}

QString formatCoord(double value)
{
    return QString::number(value, 'f', 3);
}

GeometrySketch::SketchDocument translatedDocument(const GeometrySketch::SketchDocument& source,
                                                      double du, double dv)
{
    using Document = GeometrySketch::SketchDocument;
    using Point = GeometrySketch::Point2D;
    Document result;
    auto moved = [du, dv](const Point& p) { return Point{p.u + du, p.v + dv}; };

    if (source.kind() == Document::Kind::Circle && !source.points().empty())
    {
        const Point center = moved(source.points().front());
        result.setCircle(center.u, center.v, source.circleRadius());
    }
    else if (source.kind() == Document::Kind::Arc)
    {
        std::vector<Point> points;
        for (const Point& point : source.points())
            points.push_back(moved(point));
        result.setArc(points, source.isFinished(), source.arcKind());
        result.setArcSweep(source.arcSweep());
    }
    else if (source.kind() == Document::Kind::Curve)
    {
        std::vector<Point> points;
        for (const Point& point : source.points())
            points.push_back(moved(point));
        result.setCurve(points, source.isFinished());
    }
    else if (source.kind() == Document::Kind::PointsOnly)
    {
        for (const Point& point : source.points())
        {
            const Point translated = moved(point);
            result.appendStandalonePoint(translated.u, translated.v);
        }
    }
    else if (source.kind() == Document::Kind::Profile)
    {
        std::vector<Point> keyPoints;
        for (const Point& point : source.points())
            keyPoints.push_back(moved(point));
        if (source.hasProfileSegments())
        {
            std::vector<Document::ProfileSegment> segments = source.profileSegments();
            for (auto& segment : segments)
            {
                segment.start = moved(segment.start);
                segment.control = moved(segment.control);
                segment.end = moved(segment.end);
            }
            result.setProfileSegments(keyPoints, segments, source.isClosed(), source.isFinished());
        }
        else if (source.hasCustomDisplayPoints())
        {
            std::vector<Point> display;
            for (const Point& point : source.displayPoints())
                display.push_back(moved(point));
            result.setProfileWithDisplayPoints(keyPoints, display, source.isClosed(), source.isFinished());
        }
        else
        {
            result.setProfile(keyPoints, source.isClosed(), source.isFinished());
        }
    }
    result.setConstruction(source.isConstruction());
    return result;
}

double radiansToDegrees(double value)
{
    constexpr double pi = 3.14159265358979323846;
    return value * 180.0 / pi;
}

double degreesToRadians(double value)
{
    constexpr double pi = 3.14159265358979323846;
    return value * pi / 180.0;
}

double uiArcSweepDegrees(double sweep)
{
    return std::clamp(radiansToDegrees(sweep), -360.0, 360.0);
}

QString formatAngleDegrees(double sweep)
{
    return QString::number(uiArcSweepDegrees(sweep), 'f', 2) + QStringLiteral("°");
}

QString shortNumber(double value)
{
    QString text = QString::number(value, 'f', 3);
    while (text.contains('.') && text.endsWith('0'))
        text.chop(1);
    if (text.endsWith('.'))
        text.chop(1);
    return text;
}

bool looksLikeRectangle(const SketchDocument& document)
{
    if (document.kind() != SketchDocument::Kind::Profile || !document.isClosed() || document.points().size() != 4)
        return false;

    const auto& p = document.points();
    const double eps = 1.0e-9;
    return std::abs(p[0].v - p[1].v) <= eps &&
           std::abs(p[1].u - p[2].u) <= eps &&
           std::abs(p[2].v - p[3].v) <= eps &&
           std::abs(p[3].u - p[0].u) <= eps;
}

std::vector<Point2D> rectangleGuide(const Point2D& a, const Point2D& b)
{
    return {{a.u, a.v}, {b.u, a.v}, {b.u, b.v}, {a.u, b.v}};
}

std::vector<Point2D> circleGuide(const Point2D& center, double radius)
{
    std::vector<Point2D> points;
    if (radius <= 0.0)
        return points;

    constexpr int segments = 72;
    points.reserve(segments);
    constexpr double twoPi = 6.28318530717958647692;
    for (int i = 0; i < segments; ++i)
    {
        const double t = twoPi * static_cast<double>(i) / static_cast<double>(segments);
        points.push_back({center.u + radius * std::cos(t), center.v + radius * std::sin(t)});
    }
    return points;
}

double signedArea2(const Point2D& a, const Point2D& b, const Point2D& c)
{
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}

bool isNearlyCollinear(const Point2D& a, const Point2D& b, const Point2D& c, double eps)
{
    return std::abs(signedArea2(a, b, c)) <= eps;
}

bool circleCenterFromThreePoints(const Point2D& start, const Point2D& mid, const Point2D& end,
                                 Point2D& center, double* radius = nullptr)
{
    const double scale = std::max({std::hypot(start.u - mid.u, start.v - mid.v),
                                   std::hypot(mid.u - end.u, mid.v - end.v),
                                   std::hypot(start.u - end.u, start.v - end.v),
                                   1.0});
    const double areaTolerance = 1.0e-8 * scale * scale;
    if (std::abs(signedArea2(start, mid, end)) <= areaTolerance)
        return false;

    const double d = 2.0 * (start.u * (mid.v - end.v) + mid.u * (end.v - start.v) + end.u * (start.v - mid.v));
    if (std::abs(d) <= 1.0e-12)
        return false;

    const double s2 = start.u * start.u + start.v * start.v;
    const double m2 = mid.u * mid.u + mid.v * mid.v;
    const double e2 = end.u * end.u + end.v * end.v;
    center = {
        (s2 * (mid.v - end.v) + m2 * (end.v - start.v) + e2 * (start.v - mid.v)) / d,
        (s2 * (end.u - mid.u) + m2 * (start.u - end.u) + e2 * (mid.u - start.u)) / d
    };

    const double r = std::hypot(start.u - center.u, start.v - center.v);
    if (r <= 1.0e-12)
        return false;
    if (radius)
        *radius = r;
    return true;
}

std::vector<Point2D> arcGuide(const Point2D& start, const Point2D& mid, const Point2D& end)
{
    std::vector<Point2D> points;
    Point2D center;
    double radius = 0.0;
    if (!circleCenterFromThreePoints(start, mid, end, center, &radius))
        return {start, mid, end};
    const double cu = center.u;
    const double cv = center.v;

    auto angleOf = [&](const Point2D& p) { return std::atan2(p.v - cv, p.u - cu); };
    auto normalizePositive = [](double a)
    {
        constexpr double twoPi = 6.28318530717958647692;
        while (a < 0.0) a += twoPi;
        while (a >= twoPi) a -= twoPi;
        return a;
    };

    constexpr double twoPi = 6.28318530717958647692;
    const double a0 = angleOf(start);
    const double a1 = angleOf(mid);
    const double a2 = angleOf(end);
    const double ccwEnd = normalizePositive(a2 - a0);
    const double ccwMid = normalizePositive(a1 - a0);
    const bool midOnCcw = ccwMid <= ccwEnd;
    const double sweep = midOnCcw ? ccwEnd : -(twoPi - ccwEnd);

    const int segments = std::max(12, static_cast<int>(std::ceil(std::abs(sweep) / (twoPi / 48.0))));
    points.reserve(static_cast<std::size_t>(segments + 1));
    for (int i = 0; i <= segments; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double a = a0 + sweep * t;
        points.push_back({cu + radius * std::cos(a), cv + radius * std::sin(a)});
    }
    return points;
}

std::vector<std::pair<Point2D, Point2D>> threePointArcConstructionSegments(const Point2D& start,
                                                                            const Point2D& mid,
                                                                            const Point2D& end)
{
    Point2D center;
    if (!circleCenterFromThreePoints(start, mid, end, center))
        return {{start, mid}, {mid, end}};

    return {
        {start, end},
        {center, start},
        {center, mid},
        {center, end}
    };
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

Point2D centerAngleEndpoint(const Point2D& start, const Point2D& center, const Point2D& anglePoint, bool* ok = nullptr)
{
    if (ok)
        *ok = false;

    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double au = anglePoint.u - center.u;
    const double av = anglePoint.v - center.v;
    const double angleLen = std::hypot(au, av);
    if (radius <= 1.0e-12 || angleLen <= 1.0e-12)
        return anglePoint;

    if (ok)
        *ok = true;
    return {center.u + radius * au / angleLen, center.v + radius * av / angleLen};
}

double centerAngleSweepFromPoint(const Point2D& start, const Point2D& center, const Point2D& anglePoint, bool* ok = nullptr)
{
    if (ok)
        *ok = false;

    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double au = anglePoint.u - center.u;
    const double av = anglePoint.v - center.v;
    if (radius <= 1.0e-12 || std::hypot(au, av) <= 1.0e-12)
        return 0.0;

    if (ok)
        *ok = true;
    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = std::atan2(av, au);
    return signedAngleSweep(a0, a1);
}

Point2D centerAngleEndpointForSweep(const Point2D& start, const Point2D& center, double sweep)
{
    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = a0 + sweep;
    return {center.u + radius * std::cos(a1), center.v + radius * std::sin(a1)};
}

std::vector<std::pair<Point2D, Point2D>> centerAngleArcConstructionSegments(const Point2D& start,
                                                                            const Point2D& center,
                                                                            const Point2D& anglePoint,
                                                                            double arcSweep = 0.0)
{
    if (std::abs(arcSweep) > 1.0e-12)
    {
        const Point2D end = centerAngleEndpointForSweep(start, center, clampArcSweep(arcSweep));
        return {{center, start}, {center, end}};
    }

    bool endpointOk = false;
    const Point2D end = centerAngleEndpoint(start, center, anglePoint, &endpointOk);
    if (!endpointOk)
        return {{center, start}, {center, anglePoint}};

    return {{center, start}, {center, end}};
}

std::vector<Point2D> centerAngleArcGuide(const Point2D& start,
                                         const Point2D& center,
                                         const Point2D& anglePoint,
                                         double arcSweep = 0.0)
{
    std::vector<Point2D> points;
    bool endpointOk = false;
    const Point2D end = std::abs(arcSweep) > 1.0e-12
                            ? centerAngleEndpointForSweep(start, center, clampArcSweep(arcSweep))
                            : centerAngleEndpoint(start, center, anglePoint, &endpointOk);
    if (std::abs(arcSweep) <= 1.0e-12 && !endpointOk)
        return {start, center, anglePoint};

    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = std::atan2(end.v - center.v, end.u - center.u);
    const double sweep = std::abs(arcSweep) > 1.0e-12
                             ? clampArcSweep(arcSweep)
                             : signedAngleSweep(a0, a1);
    if (std::abs(sweep) <= 1.0e-12)
        return {start, end};

    constexpr double twoPi = 6.28318530717958647692;
    const int segments = std::max(12, static_cast<int>(std::ceil(std::abs(sweep) / (twoPi / 48.0))));
    points.reserve(static_cast<std::size_t>(segments + 1));
    for (int i = 0; i <= segments; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double a = a0 + sweep * t;
        points.push_back({center.u + radius * std::cos(a), center.v + radius * std::sin(a)});
    }
    return points;
}

bool centerAngleArcThroughPoints(const Point2D& start,
                                 const Point2D& center,
                                 const Point2D& anglePoint,
                                 Point2D& mid,
                                 Point2D& end,
                                 double arcSweep = 0.0)
{
    bool endpointOk = false;
    end = std::abs(arcSweep) > 1.0e-12
              ? centerAngleEndpointForSweep(start, center, clampArcSweep(arcSweep))
              : centerAngleEndpoint(start, center, anglePoint, &endpointOk);
    if (std::abs(arcSweep) <= 1.0e-12 && !endpointOk)
        return false;

    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double a0 = std::atan2(start.v - center.v, start.u - center.u);
    const double a1 = std::atan2(end.v - center.v, end.u - center.u);
    const double sweep = std::abs(arcSweep) > 1.0e-12
                             ? clampArcSweep(arcSweep)
                             : signedAngleSweep(a0, a1);
    if (radius <= 1.0e-12 || std::abs(sweep) <= 1.0e-12)
        return false;

    const double midAngle = a0 + sweep * 0.5;
    mid = {center.u + radius * std::cos(midAngle), center.v + radius * std::sin(midAngle)};
    return true;
}


bool tangentArcCenter(const Point2D& start, const Point2D& tangentPoint, const Point2D& end,
                      Point2D& center, double* radius = nullptr)
{
    const double tx0 = tangentPoint.u - start.u;
    const double ty0 = tangentPoint.v - start.v;
    const double len = std::hypot(tx0, ty0);
    if (len <= 1.0e-12)
        return false;

    const double tx = tx0 / len;
    const double ty = ty0 / len;
    const double nx = -ty;
    const double ny = tx;
    const double dx = end.u - start.u;
    const double dy = end.v - start.v;
    const double denom = 2.0 * (nx * dx + ny * dy);
    const double d2 = dx * dx + dy * dy;
    const double scale = std::max({len, std::sqrt(d2), 1.0});
    if (std::abs(denom) <= 1.0e-8 * scale || d2 <= 1.0e-18)
        return false;

    const double a = d2 / denom;
    const double r = std::abs(a);
    if (r <= 1.0e-12)
        return false;

    center = {start.u + nx * a, start.v + ny * a};
    if (radius)
        *radius = r;
    return true;
}

std::vector<std::pair<Point2D, Point2D>> tangentArcConstructionSegments(const Point2D& start,
                                                                        const Point2D& tangentPoint,
                                                                        const Point2D& end)
{
    Point2D center;
    if (!tangentArcCenter(start, tangentPoint, end, center))
        return {{start, tangentPoint}, {start, end}};

    return {
        {start, tangentPoint},
        {center, start},
        {center, end}
    };
}

std::vector<Point2D> tangentArcGuide(const Point2D& start, const Point2D& tangentPoint, const Point2D& end)
{
    std::vector<Point2D> points;
    const double tx0 = tangentPoint.u - start.u;
    const double ty0 = tangentPoint.v - start.v;
    const double len = std::hypot(tx0, ty0);
    if (len <= 1.0e-12)
        return {start, end};

    const double tx = tx0 / len;
    const double ty = ty0 / len;
    // 圆心必须落在起点处所选切线的法线上：C = start + normal * a。
    const double nx = -ty;
    const double ny = tx;
    const double dx = end.u - start.u;
    const double dy = end.v - start.v;
    const double denom = 2.0 * (nx * dx + ny * dy);
    const double d2 = dx * dx + dy * dy;
    if (std::abs(denom) <= 1.0e-12 || d2 <= 1.0e-18)
        return {start, end};

    const double a = d2 / denom;
    const double cu = start.u + nx * a;
    const double cv = start.v + ny * a;
    const double radius = std::abs(a);
    if (radius <= 1.0e-12)
        return {start, end};

    auto angleOf = [&](const Point2D& pnt) { return std::atan2(pnt.v - cv, pnt.u - cu); };
    auto normalizePositive = [](double value)
    {
        constexpr double twoPi = 6.28318530717958647692;
        while (value < 0.0) value += twoPi;
        while (value >= twoPi) value -= twoPi;
        return value;
    };

    constexpr double twoPi = 6.28318530717958647692;
    const double a0 = angleOf(start);
    const double a1 = angleOf(end);

    // 圆弧方向必须保证起点处切线方向和用户选择的切线方向一致。
    const double ccwTx = -std::sin(a0);
    const double ccwTy = std::cos(a0);
    const bool useCcw = (ccwTx * tx + ccwTy * ty) >= 0.0;
    double sweep = 0.0;
    if (useCcw)
        sweep = normalizePositive(a1 - a0);
    else
        sweep = -normalizePositive(a0 - a1);

    if (std::abs(sweep) <= 1.0e-12)
        return {start, end};

    const int segments = std::max(12, static_cast<int>(std::ceil(std::abs(sweep) / (twoPi / 64.0))));
    points.reserve(static_cast<std::size_t>(segments + 1));
    for (int i = 0; i <= segments; ++i)
    {
        const double t = static_cast<double>(i) / static_cast<double>(segments);
        const double aa = a0 + sweep * t;
        points.push_back({cu + radius * std::cos(aa), cv + radius * std::sin(aa)});
    }
    return points;
}


Point2D catmullRom(const Point2D& p0, const Point2D& p1, const Point2D& p2, const Point2D& p3, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    return {
        0.5 * ((2.0 * p1.u) + (-p0.u + p2.u) * t + (2.0 * p0.u - 5.0 * p1.u + 4.0 * p2.u - p3.u) * t2 + (-p0.u + 3.0 * p1.u - 3.0 * p2.u + p3.u) * t3),
        0.5 * ((2.0 * p1.v) + (-p0.v + p2.v) * t + (2.0 * p0.v - 5.0 * p1.v + 4.0 * p2.v - p3.v) * t2 + (-p0.v + 3.0 * p1.v - 3.0 * p2.v + p3.v) * t3)
    };
}

std::vector<Point2D> curveGuide(const std::vector<Point2D>& input)
{
    // 预览和最终生成必须共用同一套采样算法。
    return SketchShapeBuilder::sampleCurvePoints(input);
}


double distanceToSegment(const Point2D& p, const Point2D& a, const Point2D& b)
{
    const double abU = b.u - a.u;
    const double abV = b.v - a.v;
    const double len2 = abU * abU + abV * abV;
    if (len2 <= 1.0e-18)
        return std::hypot(p.u - a.u, p.v - a.v);

    const double t = std::clamp(((p.u - a.u) * abU + (p.v - a.v) * abV) / len2, 0.0, 1.0);
    const double projU = a.u + t * abU;
    const double projV = a.v + t * abV;
    return std::hypot(p.u - projU, p.v - projV);
}

QString planeShortText(Plane plane)
{
    switch (plane)
    {
    case Plane::XY: return QStringLiteral("XY");
    case Plane::XZ: return QStringLiteral("XZ");
    case Plane::YZ: return QStringLiteral("YZ");
    }
    return QStringLiteral("XY");
}

QJsonArray pointsToJson(const std::vector<Point2D>& points)
{
    QJsonArray arr;
    for (const auto& p : points)
    {
        QJsonObject pt;
        pt[QStringLiteral("u")] = p.u;
        pt[QStringLiteral("v")] = p.v;
        arr.append(pt);
    }
    return arr;
}

std::vector<Point2D> pointsFromJson(const QJsonArray& arr)
{
    std::vector<Point2D> points;
    points.reserve(static_cast<std::size_t>(arr.size()));
    for (const auto& value : arr)
    {
        const QJsonObject pt = value.toObject();
        points.push_back({pt.value(QStringLiteral("u")).toDouble(),
                          pt.value(QStringLiteral("v")).toDouble()});
    }
    return points;
}



double cross2(const Point2D& a, const Point2D& b, const Point2D& c)
{
    return (b.u - a.u) * (c.v - a.v) - (b.v - a.v) * (c.u - a.u);
}

double dot2(const Point2D& a, const Point2D& b)
{
    return a.u * b.u + a.v * b.v;
}

Point2D add2(const Point2D& a, const Point2D& b)
{
    return {a.u + b.u, a.v + b.v};
}

Point2D sub2(const Point2D& a, const Point2D& b)
{
    return {a.u - b.u, a.v - b.v};
}

Point2D mul2(const Point2D& a, double s)
{
    return {a.u * s, a.v * s};
}

double length2(const Point2D& a)
{
    return std::hypot(a.u, a.v);
}

void appendPointIfFar(std::vector<Point2D>& out, const Point2D& p, double eps)
{
    if (out.empty() || std::hypot(out.back().u - p.u, out.back().v - p.v) > eps)
        out.push_back(p);
}

Point2D normalized2(const Point2D& a)
{
    const double len = length2(a);
    if (len <= 1.0e-12)
        return {0.0, 0.0};
    return {a.u / len, a.v / len};
}

double polygonSignedArea(const std::vector<Point2D>& points)
{
    if (points.size() < 3)
        return 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& a = points[i];
        const auto& b = points[(i + 1) % points.size()];
        area += a.u * b.v - b.u * a.v;
    }
    return 0.5 * area;
}

bool onSegment2(const Point2D& a, const Point2D& b, const Point2D& p, double eps)
{
    return std::abs(cross2(a, b, p)) <= eps &&
           p.u >= std::min(a.u, b.u) - eps && p.u <= std::max(a.u, b.u) + eps &&
           p.v >= std::min(a.v, b.v) - eps && p.v <= std::max(a.v, b.v) + eps;
}

bool segmentsIntersect2(const Point2D& a, const Point2D& b,
                        const Point2D& c, const Point2D& d, double eps)
{
    const double c1 = cross2(a, b, c);
    const double c2 = cross2(a, b, d);
    const double c3 = cross2(c, d, a);
    const double c4 = cross2(c, d, b);

    if (((c1 > eps && c2 < -eps) || (c1 < -eps && c2 > eps)) &&
        ((c3 > eps && c4 < -eps) || (c3 < -eps && c4 > eps)))
        return true;

    if (onSegment2(a, b, c, eps) || onSegment2(a, b, d, eps) ||
        onSegment2(c, d, a, eps) || onSegment2(c, d, b, eps))
        return true;
    return false;
}

bool profileHasSelfIntersection(const std::vector<Point2D>& points, bool closed, double eps)
{
    const int n = static_cast<int>(points.size());
    const int segCount = closed ? n : n - 1;
    if (n < 4 || segCount < 3)
        return false;

    for (int i = 0; i < segCount; ++i)
    {
        const int i0 = i;
        const int i1 = (i + 1) % n;
        for (int j = i + 1; j < segCount; ++j)
        {
            const int j0 = j;
            const int j1 = (j + 1) % n;
            if (i1 == j0 || j1 == i0)
                continue;
            if (closed && i == 0 && j == segCount - 1)
                continue;
            if (segmentsIntersect2(points[i0], points[i1], points[j0], points[j1], eps))
                return true;
        }
    }
    return false;
}

std::vector<Point2D> cleanedProfilePoints(const std::vector<Point2D>& input, double eps)
{
    std::vector<Point2D> out;
    out.reserve(input.size());
    for (const auto& p : input)
        appendPointIfFar(out, p, eps);
    return out;
}

bool isValidCommand(int value)
{
    switch (static_cast<Command>(value))
    {
    case Command::Select:
    case Command::Polyline:
    case Command::Rectangle:
    case Command::Circle:
    case Command::Point:
    case Command::Arc:
    case Command::Curve:
        return true;
    }
    return false;
}

struct DxfSketchConversion
{
    std::vector<SketchDocument> documents;
    int exactCount = 0;
    int approximatedCount = 0;
    int skippedCount = 0;
};

constexpr double kDxfSketchTolerance = 1.0e-7;
constexpr double kDxfTwoPi = 6.28318530717958647692;

Point2D dxfSketchPoint(const gp_Pnt& point)
{
    // DXF 是二维交换格式：文件 X/Y 映射到当前草图平面的 U/V。
    return {point.X(), point.Y()};
}

double dxfPointDistance(const Point2D& left, const Point2D& right)
{
    return std::hypot(left.u - right.u, left.v - right.v);
}

struct DxfEdgeRange
{
    double first = 0.0;
    double last = 0.0;
};

DxfEdgeRange orientedDxfEdgeRange(const TopoDS_Edge& edge, const BRepAdaptor_Curve& curve)
{
    DxfEdgeRange range{curve.FirstParameter(), curve.LastParameter()};
    if (edge.Orientation() == TopAbs_REVERSED)
        std::swap(range.first, range.last);
    return range;
}

Point2D dxfEdgePoint(const TopoDS_Edge& edge, const BRepAdaptor_Curve& curve, double ratio)
{
    const DxfEdgeRange range = orientedDxfEdgeRange(edge, curve);
    const double parameter = range.first + (range.last - range.first) * std::clamp(ratio, 0.0, 1.0);
    return dxfSketchPoint(curve.Value(parameter));
}

bool isFullDxfCircle(const TopoDS_Edge& edge, const BRepAdaptor_Curve& curve)
{
    if (curve.GetType() != GeomAbs_Circle)
        return false;
    const DxfEdgeRange range = orientedDxfEdgeRange(edge, curve);
    if (std::abs(range.last - range.first) >= kDxfTwoPi - 1.0e-7)
        return true;
    return dxfPointDistance(dxfEdgePoint(edge, curve, 0.0),
                            dxfEdgePoint(edge, curve, 1.0)) <= kDxfSketchTolerance;
}

int dxfEdgeSampleCount(const BRepAdaptor_Curve& curve)
{
    switch (curve.GetType())
    {
    case GeomAbs_Line:
        return 1;
    case GeomAbs_Circle:
    {
        const double sweep = std::abs(curve.LastParameter() - curve.FirstParameter());
        return std::max(12, static_cast<int>(std::ceil(64.0 * sweep / kDxfTwoPi)));
    }
    case GeomAbs_Ellipse:
    {
        const double sweep = std::abs(curve.LastParameter() - curve.FirstParameter());
        return std::max(24, static_cast<int>(std::ceil(96.0 * sweep / kDxfTwoPi)));
    }
    case GeomAbs_BSplineCurve:
    case GeomAbs_BezierCurve:
        return 48;
    default:
        return 32;
    }
}

std::vector<Point2D> sampledDxfEdge(const TopoDS_Edge& edge)
{
    std::vector<Point2D> points;
    BRepAdaptor_Curve curve(edge);
    const int samples = std::max(1, dxfEdgeSampleCount(curve));
    points.reserve(static_cast<std::size_t>(samples + 1));
    for (int i = 0; i <= samples; ++i)
    {
        const Point2D point = dxfEdgePoint(edge, curve,
                                           static_cast<double>(i) / static_cast<double>(samples));
        if (points.empty() || dxfPointDistance(points.back(), point) > kDxfSketchTolerance)
            points.push_back(point);
    }
    return points;
}

bool isExactDxfSketchEdge(const TopoDS_Edge& edge)
{
    const GeomAbs_CurveType type = BRepAdaptor_Curve(edge).GetType();
    return type == GeomAbs_Line || type == GeomAbs_Circle;
}

bool appendExactDxfWire(const std::vector<TopoDS_Edge>& edges,
                        DxfSketchConversion& conversion)
{
    if (edges.empty())
        return false;

    if (edges.size() == 1)
    {
        const TopoDS_Edge& edge = edges.front();
        BRepAdaptor_Curve curve(edge);
        if (curve.GetType() == GeomAbs_Circle && isFullDxfCircle(edge, curve))
        {
            const gp_Circ circle = curve.Circle();
            SketchDocument document;
            document.setCircle(circle.Location().X(), circle.Location().Y(), circle.Radius());
            document.setFinished(true);
            conversion.documents.push_back(document);
            ++conversion.exactCount;
            return true;
        }
        if (curve.GetType() == GeomAbs_Circle)
        {
            SketchDocument document;
            document.setArc({dxfEdgePoint(edge, curve, 0.0),
                             dxfEdgePoint(edge, curve, 0.5),
                             dxfEdgePoint(edge, curve, 1.0)},
                            true, SketchDocument::ArcKind::Through);
            conversion.documents.push_back(document);
            ++conversion.exactCount;
            return true;
        }
    }

    std::vector<Point2D> keyPoints;
    std::vector<SketchDocument::ProfileSegment> segments;
    keyPoints.reserve(edges.size() + 1);
    segments.reserve(edges.size());

    for (const TopoDS_Edge& edge : edges)
    {
        BRepAdaptor_Curve curve(edge);
        const Point2D start = dxfEdgePoint(edge, curve, 0.0);
        const Point2D end = dxfEdgePoint(edge, curve, 1.0);
        if (dxfPointDistance(start, end) <= kDxfSketchTolerance &&
            curve.GetType() != GeomAbs_Circle)
        {
            continue;
        }

        if (keyPoints.empty())
            keyPoints.push_back(start);

        SketchDocument::ProfileSegment segment;
        segment.start = start;
        segment.end = end;
        if (curve.GetType() == GeomAbs_Circle)
        {
            segment.kind = SketchDocument::ProfileSegmentKind::ArcThrough;
            segment.control = dxfEdgePoint(edge, curve, 0.5);
        }
        else
        {
            segment.kind = SketchDocument::ProfileSegmentKind::Line;
        }
        segments.push_back(segment);
        keyPoints.push_back(end);
    }

    if (segments.empty() || keyPoints.size() < 2)
        return false;

    const bool closed = keyPoints.size() >= 3 &&
                        dxfPointDistance(keyPoints.front(), keyPoints.back()) <= kDxfSketchTolerance;
    SketchDocument document;
    document.setProfileSegments(keyPoints, segments, closed, true);
    conversion.documents.push_back(document);
    ++conversion.exactCount;
    return true;
}

void appendApproximatedDxfEdge(const TopoDS_Edge& edge,
                               DxfSketchConversion& conversion)
{
    std::vector<Point2D> points = sampledDxfEdge(edge);
    if (points.size() < 2)
    {
        ++conversion.skippedCount;
        return;
    }

    const bool closed = points.size() >= 4 &&
                        dxfPointDistance(points.front(), points.back()) <= kDxfSketchTolerance;
    SketchDocument document;
    if (closed)
    {
        points.pop_back();
        document.setProfile(points, true, true);
    }
    else if (points.size() >= 3)
    {
        // 草图样条由导入曲线的均匀采样点重建，保持平滑而不是画成折线。
        document.setCurve(points, true);
    }
    else
    {
        document.setProfile(points, false, true);
    }
    conversion.documents.push_back(document);
    ++conversion.approximatedCount;
}

DxfSketchConversion convertDxfShapeToSketch(const TopoDS_Shape& shape)
{
    DxfSketchConversion conversion;
    for (TopExp_Explorer wireExplorer(shape, TopAbs_WIRE);
         wireExplorer.More(); wireExplorer.Next())
    {
        const TopoDS_Wire wire = TopoDS::Wire(wireExplorer.Current());
        if (wire.IsNull())
            continue;

        std::vector<TopoDS_Edge> edges;
        bool allExact = true;
        for (BRepTools_WireExplorer edgeExplorer(wire);
             edgeExplorer.More(); edgeExplorer.Next())
        {
            const TopoDS_Edge edge = edgeExplorer.Current();
            if (edge.IsNull())
                continue;
            edges.push_back(edge);
            allExact = allExact && isExactDxfSketchEdge(edge);
        }

        if (edges.empty())
            continue;
        if (allExact && appendExactDxfWire(edges, conversion))
            continue;

        // 混合轮廓中的直线/圆弧仍保留为精确草图对象，样条和椭圆则转为平滑样条。
        // 这些对象的端点保持一致，实体输出时仍可自动连接成面。
        for (const TopoDS_Edge& edge : edges)
        {
            if (isExactDxfSketchEdge(edge))
                appendExactDxfWire({edge}, conversion);
            else
                appendApproximatedDxfEdge(edge, conversion);
        }
    }
    return conversion;
}

} // namespace

Sketch2DDialog::Sketch2DDialog(QWidget* parent)
    : BasePage(PublicConstants::kPageGeomSketch2D), ui(new Ui::Sketch2DDialog)
{
    Q_UNUSED(parent);
    initUi();
    initViewCallbacks();
    initEventBridge();
    setCommand(Command::Select);
    updateActionButtons();
    GeometryPanelUi::apply(this);
    if (qApp)
        qApp->installEventFilter(this);
}

Sketch2DDialog::~Sketch2DDialog()
{
    if (qApp)
        qApp->removeEventFilter(this);
    exitSketchMode();
    delete ui;
}

void Sketch2DDialog::setInitialCommand(GeometrySketch::Command command)
{
    // 仅供“新建点/直线草图”入口设置首次工具；工程恢复仍由 loadJson()
    // 强制进入选择模式，避免重开工程后落入临时绘图命令。
    if (!m_isCurrentPage && !hasAnySketchData())
        setCommand(command);
}

bool Sketch2DDialog::eventFilter(QObject* watched, QEvent* event)
{
    if (!m_isCurrentPage || !event)
        return BasePage::eventFilter(watched, event);

    if (event->type() != QEvent::KeyPress)
        return BasePage::eventFilter(watched, event);

    auto* keyEvent = static_cast<QKeyEvent*>(event);
    const Qt::KeyboardModifiers modifiers = keyEvent->modifiers() &
        (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    const bool undo = keyEvent->key() == Qt::Key_Z && modifiers == Qt::ControlModifier;
    const bool redo = (keyEvent->key() == Qt::Key_Y && modifiers == Qt::ControlModifier) ||
                      (keyEvent->key() == Qt::Key_Z &&
                       modifiers == (Qt::ControlModifier | Qt::ShiftModifier));
    const bool cancel = keyEvent->key() == Qt::Key_Escape && modifiers == Qt::NoModifier;
    const bool remove = keyEvent->key() == Qt::Key_Delete && modifiers == Qt::NoModifier;
    if (!undo && !redo && !cancel && !remove)
        return BasePage::eventFilter(watched, event);

    // 正在编辑文本或数值时，保留输入控件自己的撤销、删除等行为。
    QWidget* focus = QApplication::focusWidget();
    if (focus && (focus->inherits("QLineEdit") || focus->inherits("QTextEdit") ||
                  focus->inherits("QPlainTextEdit")))
    {
        return BasePage::eventFilter(watched, event);
    }

    if (!keyEvent->isAutoRepeat())
    {
        if (undo)
            undoLast();
        else if (redo)
            redoLast();
        else if (cancel)
            cancelCurrentCommand();
        else
            removeSelectedPoint();
    }
    return true;
}

void Sketch2DDialog::beforeLoad()
{
    m_isCurrentPage = true;
    m_visibilityOnEntry = GeometryDisplayManager::isVisible(m_uuid);
    m_hadRenderableDataOnEntry = hasVisibleGeometryDocuments();
    m_visibilityExplicitlyChanged = false;

    // 新建空草图即使树节点尚未勾选，也必须能正常绘制；已有草图则严格
    // 遵循模型树的显隐状态，避免“未勾选但蓝色预览仍显示”的双重语义。
    m_editorPreviewVisible = !m_hadRenderableDataOnEntry || m_visibilityOnEntry;

    m_view.setObjectPrefix(m_uuid);
    if (m_visibilityOnEntry)
        hideCommittedPresentationForEditing();

    resetCursorInfo();
    updatePlaneAndView();
    enterSketchMode();
    refreshPointTable();
    refreshObjectTable();
    updateInfoPanel();
    updatePreview();

    // 记录进入草图时真正的建模状态。绘图命令本身只是编辑器状态，
    // 不应因为用户切换过“直线/圆”等工具就被误判为几何修改。
    m_stateOnEntry = saveJson();
    m_stateOnEntry.remove(QStringLiteral("command"));
}

void Sketch2DDialog::beforeUnload()
{
    // closeProject() 会对所有几何页面做一次兜底卸载。未激活的草图不能
    // 因此再次 finalize 或改写显隐状态，否则保存/切换工程时会出现状态漂移。
    if (!m_isCurrentPage)
        return;

    finalizeSketchEditing();

    QJsonObject currentState = saveJson();
    currentState.remove(QStringLiteral("command"));
    const bool geometryEdited = currentState != m_stateOnEntry;
    const bool desiredVisible = GeometryDisplayManager::isVisible(m_uuid);

    if (geometryEdited)
    {
        if (auto* pre = ServiceRegistry::instance().getService<PreProcessingService>())
        {
            if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
            {
                // 提交几何变化；显隐仍以模型树当前状态为准，不再回退到进入页面时的旧状态。
                workbench->refreshTreePresentation(m_uuid);
            }
        }
    }

    setCommand(Command::Select);
    clearEndpointSelection();
    m_isCurrentPage = false;
    exitSketchMode();

    // 编辑时隐藏的是普通几何 Actor，退出草图后恢复模型树当前的逻辑显隐。
    GeometryDisplayManager::setVisible(m_uuid, desiredVisible, false);
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->render();
}

void Sketch2DDialog::syncVisibilityFromTree(bool visible)
{
    if (!m_isCurrentPage)
        return;

    m_visibilityExplicitlyChanged = true;
    m_editorPreviewVisible = visible;
    if (!visible)
    {
        m_view.clearPreview();
        return;
    }

    // 编辑状态只保留草图编辑器自己的预览，避免与普通 Geometry Actor
    // 同时显示形成重线；退出页面时再恢复普通 Actor。
    hideCommittedPresentationForEditing();
    updatePreview();
}

QJsonObject Sketch2DDialog::saveJson()
{
    syncDocumentFromTable();

    QJsonObject obj;
    obj["plane"] = ui->planeCombo ? ui->planeCombo->currentIndex() : 0;
    obj["outputMode"] = static_cast<int>(outputMode());
    obj["offset"] = ui->offsetSpin ? ui->offsetSpin->value() : 0.0;
    obj["snap"] = ui->snapCheck ? ui->snapCheck->isChecked() : true;
    obj["gridSnap"] = ui->gridSnapCheck ? ui->gridSnapCheck->isChecked() : true;
    obj["gridStep"] = ui->gridStepSpin ? ui->gridStepSpin->value() : 1.0;
    obj["gridSnapStep"] = gridSnapStep();
    obj["pointSnapRange"] = pointSnapRange();
    obj["ortho"] = ui->orthoCheck ? ui->orthoCheck->isChecked() : false;
    obj["autoClose"] = ui->autoCloseCheck ? ui->autoCloseCheck->isChecked() : true;
    obj["autoCloseRange"] = autoCloseRange();
    obj["showAxes"] = ui->showAxesCheck ? ui->showAxesCheck->isChecked() : false;
    obj["showAxesConfigured"] = true;
    obj["arcMode"] = (ui->arcModeCombo && ui->arcModeCombo->currentData().isValid())
                         ? ui->arcModeCombo->currentData().toInt()
                         : static_cast<int>(StandaloneArcMode::ThreePoint);
    obj["command"] = static_cast<int>(currentCommand());
    obj["document"] = m_document.toJson();

    QJsonArray committed;
    for (const auto& document : m_committedDocuments)
        committed.append(document.toJson());
    obj["committedDocuments"] = committed;
    obj["referencePoints"] = pointsToJson(m_referencePoints);
    return obj;
}

void Sketch2DDialog::loadJson(const QJsonObject& obj)
{
    m_loadingJson = true;
    if (ui->planeCombo)
        ui->planeCombo->setCurrentIndex(obj.value("plane").toInt(0));
    if (ui->outputModeCombo)
        ui->outputModeCombo->setCurrentIndex(obj.value("outputMode").toInt(static_cast<int>(OutputMode::Entity)));
    if (ui->offsetSpin)
        ui->offsetSpin->setValue(obj.value("offset").toDouble(0.0));
    if (ui->snapCheck)
        ui->snapCheck->setChecked(obj.value("snap").toBool(true));
    if (ui->gridSnapCheck)
        ui->gridSnapCheck->setChecked(obj.value("gridSnap").toBool(true));
    const double loadedGridStep = obj.value("gridStep").toDouble(1.0);
    if (ui->gridStepSpin)
        ui->gridStepSpin->setValue(loadedGridStep);
    if (ui->gridSnapStepSpin)
        ui->gridSnapStepSpin->setValue(obj.value("gridSnapStep").toDouble(loadedGridStep));
    if (ui->pointSnapSpin)
        ui->pointSnapSpin->setValue(obj.value("pointSnapRange").toDouble(0.12));
    if (ui->orthoCheck)
        ui->orthoCheck->setChecked(obj.value("ortho").toBool(false));
    if (ui->autoCloseCheck)
        ui->autoCloseCheck->setChecked(obj.value("autoClose").toBool(true));
    if (ui->autoCloseToleranceSpin)
        ui->autoCloseToleranceSpin->setValue(obj.value("autoCloseRange").toDouble(0.25));
    if (ui->showAxesCheck)
    {
        const bool hasAxisPreference = obj.value("showAxesConfigured").toBool(false);
        ui->showAxesCheck->setChecked(hasAxisPreference ? obj.value("showAxes").toBool(false) : false);
    }
    if (ui->arcModeCombo)
    {
        const QSignalBlocker blocker(ui->arcModeCombo);
        const int arcMode = obj.value("arcMode").toInt(static_cast<int>(StandaloneArcMode::ThreePoint));
        const int index = ui->arcModeCombo->findData(arcMode);
        ui->arcModeCombo->setCurrentIndex(index >= 0 ? index : 0);
    }

    m_document.fromJson(obj.value("document").toObject());
    m_referencePoints = pointsFromJson(obj.value("referencePoints").toArray());
    if (m_document.kind() == SketchDocument::Kind::PointsOnly)
    {
        const auto& oldPoints = m_document.points();
        m_referencePoints.insert(m_referencePoints.end(), oldPoints.begin(), oldPoints.end());
        m_document.clear();
    }

    m_committedDocuments.clear();
    const QJsonArray committed = obj.value("committedDocuments").toArray();
    for (const auto& value : committed)
    {
        SketchDocument document;
        document.fromJson(value.toObject());
        if (document.isRenderable())
            m_committedDocuments.push_back(document);
    }
    if (m_document.isRenderable() && m_document.isFinished())
    {
        m_committedDocuments.push_back(m_document);
        m_document.clear();
    }
    m_undoStack.clear();
    m_redoStack.clear();
    m_editingCommittedIndex = -1;

    // 打开草图时始终进入选择模式。绘图命令属于临时交互状态，
    // 不随工程保存，避免重开后直接落入半完成的绘图命令。
    setCommand(Command::Select);

    m_loadingJson = false;
    refreshPointTable();
    refreshObjectTable();
    if (m_isCurrentPage)
    {
        updatePlaneAndView();
        updatePreview();
    }
    else
    {
        updateInfoPanel();
        updateActionButtons();
    }
}

void Sketch2DDialog::initUi()
{
    ui->setupUi(this);
    setStyleSheet(QStringLiteral(R"(
        QWidget#Sketch2DDialog,
        QWidget#Sketch2DDialog QLabel,
        QWidget#Sketch2DDialog QGroupBox,
        QWidget#Sketch2DDialog QCheckBox,
        QWidget#Sketch2DDialog QPushButton,
        QWidget#Sketch2DDialog QComboBox,
        QWidget#Sketch2DDialog QDoubleSpinBox,
        QWidget#Sketch2DDialog QTableWidget {
            color:#000000;
        }
    )"));

    ui->planeGroup->setTitle(tr("基本属性"));
    ui->toolGroupBox->setTitle(tr("绘制工具"));
    ui->optionGroup->setTitle(tr("绘图辅助"));
    ui->editGroup->setTitle(tr("修整"));
    ui->tableGroup->setTitle(tr("草图元素"));
    ui->bottomGroup->setTitle(tr("状态"));
    ui->toolGroupBox->setVisible(false);
    ui->editGroup->setVisible(false);
    ui->tableGroup->setVisible(true);
    ui->bottomGroup->setVisible(false);
    ui->generateButton->setVisible(false);

    // 保留一张紧凑的草图元素表；点坐标通过图形区控制点和 U/V 输入框编辑。
    ui->pointsTitleLabel->setVisible(false);
    ui->pointsTable->setVisible(false);
    ui->editObjectButton->setVisible(false);
    ui->deleteObjectButton->setVisible(false);
    ui->objectsTitleLabel->setText(tr("草图元素"));
    ui->objectsTitleLabel->setVisible(false);
    ui->pointCoordinateGroup->setVisible(false);
    ui->arcAngleRow->setVisible(false);

    if (ui->gridSnapCheck)
        ui->gridSnapCheck->setChecked(true);
    if (ui->snapCheck)
    {
        ui->snapCheck->setText(tr("端点"));
        ui->snapCheck->setToolTip(tr("吸附到已有端点和参考点。"));
    }
    if (ui->gridSnapCheck)
    {
        ui->gridSnapCheck->setText(tr("启用网格吸附"));
        ui->gridSnapCheck->setToolTip(tr("开启后按吸附步长取点。"));
    }
    if (ui->gridStepLabel)
        ui->gridStepLabel->setText(tr("网格边长"));
    if (ui->gridStepSpin)
    {
        ui->gridStepSpin->setPrefix(QString());
        ui->gridStepSpin->setToolTip(tr("背景网格单格边长。"));
    }
    if (ui->gridSnapStepSpin)
    {
        ui->gridSnapStepSpin->setPrefix(QString());
        ui->gridSnapStepSpin->setToolTip(tr("网格吸附的取点步长。"));
        ui->gridSnapStepSpin->setEnabled(ui->gridSnapCheck && ui->gridSnapCheck->isChecked());
    }
    if (ui->gridSnapStepLabel)
        ui->gridSnapStepLabel->setText(tr("吸附步长"));
    if (ui->autoCloseCheck)
    {
        ui->autoCloseCheck->setText(tr("首点"));
        ui->autoCloseCheck->setToolTip(tr("靠近轮廓首点时显示闭合吸附。"));
    }
    if (ui->orthoCheck)
    {
        ui->orthoCheck->setText(tr("正交"));
        ui->orthoCheck->setToolTip(tr("锁定水平或垂直方向。"));
    }
    if (ui->showAxesCheck)
    {
        ui->showAxesCheck->setChecked(false);
        ui->showAxesCheck->setText(tr("轴线"));
        ui->showAxesCheck->setToolTip(tr("显示坐标轴。"));
    }
    if (ui->arcModeCombo)
    {
        ui->arcModeCombo->clear();
        ui->arcModeCombo->addItem(tr("三点"), static_cast<int>(StandaloneArcMode::ThreePoint));
        ui->arcModeCombo->addItem(tr("中心角"), static_cast<int>(StandaloneArcMode::CenterAngle));
        ui->arcModeCombo->setToolTip(tr("选择圆弧方式。"));
        ui->arcModeCombo->setVisible(false);
    }
    if (ui->arcModeLabel)
        ui->arcModeLabel->setVisible(false);

    ui->planeCombo->clear();
    ui->planeCombo->addItem(tr("XY 平面"), static_cast<int>(Plane::XY));
    ui->planeCombo->addItem(tr("XZ 平面"), static_cast<int>(Plane::XZ));
    ui->planeCombo->addItem(tr("YZ 平面"), static_cast<int>(Plane::YZ));
    if (ui->outputModeCombo)
    {
        ui->outputModeCombo->clear();
        ui->outputModeCombo->addItem(tr("实体"), static_cast<int>(OutputMode::Entity));
        ui->outputModeCombo->addItem(tr("曲线"), static_cast<int>(OutputMode::Curve));
        ui->outputModeCombo->setToolTip(tr("实体或曲线输出。"));
    }
    if (ui->importDxfButton)
    {
        ui->importDxfButton->setText(tr("导入 DXF 到草图"));
        ui->importDxfButton->setToolTip(
            tr("将 DXF 中的直线、圆、圆弧和样条转为可继续编辑的草图元素。"));
    }

    ui->lineButton->setText(tr("折线轮廓"));
    ui->rectButton->setText(tr("矩形"));
    ui->circleButton->setText(tr("圆"));
    if (ui->selectButton)
        ui->selectButton->setText(tr("选择"));
    if (ui->arcButton)
        ui->arcButton->setText(tr("圆弧"));
    if (ui->curveButton)
        ui->curveButton->setText(tr("平滑样条"));
    ui->pointButton->setText(tr("点"));
    for (auto* button : {ui->selectButton, ui->lineButton, ui->rectButton, ui->circleButton, ui->arcButton, ui->curveButton, ui->pointButton})
    {
        if (!button)
            continue;
        button->setMinimumHeight(30);
        button->setToolTip(tr("切换草图工具"));
    }
    if (ui->selectButton)
        ui->selectButton->setToolTip(tr("选择草图对象。"));
    ui->lineButton->setToolTip(tr("连续绘制直线段，拐点保持尖角。"));
    ui->rectButton->setToolTip(tr("两点画矩形。"));
    ui->circleButton->setToolTip(tr("圆心 + 半径点。"));
    if (ui->arcButton)
        ui->arcButton->setToolTip(tr("按当前圆弧方式取点。"));
    if (ui->curveButton)
        ui->curveButton->setToolTip(tr("经过控制点生成平滑 B 样条。"));
    ui->pointButton->setToolTip(tr("吸附参考点。"));
    ui->finishButton->setText(tr("结束"));
    ui->finishButton->setToolTip(tr("结束绘制。"));
    ui->closeButton->setText(tr("闭合"));
    ui->closeButton->setToolTip(tr("闭合轮廓。"));
    ui->undoButton->setToolTip(tr("撤销草图内最近一次编辑。"));
    ui->redoButton->setToolTip(tr("重做刚刚撤销的草图编辑。"));
    ui->deleteButton->setToolTip(tr("删除选中点。"));
    ui->reverseButton->setToolTip(tr("反转轮廓方向。"));
    if (ui->mergeEndpointsButton)
    {
        ui->mergeEndpointsButton->setText(tr("合并"));
        ui->mergeEndpointsButton->setToolTip(tr("合并重复点和接近端点。"));
    }
    if (ui->checkClosureButton)
    {
        ui->checkClosureButton->setText(tr("检查"));
        ui->checkClosureButton->setToolTip(tr("检查闭合、自交和面积。"));
    }
    ui->clearButton->setToolTip(tr("清空草图。"));
    ui->generateButton->setToolTip(tr("应用草图。"));

    auto styleSketchTable = [](QTableWidget* table, int minHeight, int maxHeight)
    {
        if (!table)
            return;
        table->setMinimumHeight(minHeight);
        table->setMaximumHeight(maxHeight);
        table->setAlternatingRowColors(true);
        table->setShowGrid(true);
        table->setGridStyle(Qt::SolidLine);
        table->setStyleSheet(QStringLiteral(R"(
            QTableWidget {
                background:#ffffff;
                alternate-background-color:#f8fafc;
                border:1px solid #cbd5e1;
                border-radius:4px;
                gridline-color:#d6dee8;
                selection-background-color:#dbeafe;
                selection-color:#000000;
                outline:0;
            }
            QTableWidget::item {
                padding:3px 6px;
                border:0;
            }
            QHeaderView::section {
                background:#f1f5f9;
                color:#000000;
                border:0;
                border-right:1px solid #cbd5e1;
                border-bottom:1px solid #cbd5e1;
                padding:4px 6px;
                font-weight:600;
            }
            QTableCornerButton::section {
                background:#f1f5f9;
                border:0;
                border-right:1px solid #cbd5e1;
                border-bottom:1px solid #cbd5e1;
            }
        )"));
    };

    ui->pointsTable->setColumnCount(2);
    ui->pointsTable->setHorizontalHeaderLabels({tr("U"), tr("V")});
    ui->pointsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui->pointsTable->horizontalHeader()->setMinimumHeight(28);
    ui->pointsTable->verticalHeader()->setVisible(true);
    ui->pointsTable->verticalHeader()->setDefaultSectionSize(26);
    ui->pointsTable->verticalHeader()->setMinimumWidth(52);
    ui->pointsTable->setToolTip(tr("对象坐标。"));
    styleSketchTable(ui->pointsTable, 160, 240);

    if (ui->pointsTitleLabel)
    {
        ui->pointsTitleLabel->setWordWrap(false);
        ui->pointsTitleLabel->setStyleSheet("color:#000000; font-weight:600; margin-top:2px;");
    }
    if (ui->objectsTitleLabel)
    {
        ui->objectsTitleLabel->setWordWrap(false);
        ui->objectsTitleLabel->setStyleSheet("color:#000000; font-weight:600; margin-top:2px;");
    }

    ui->objectsTable->setColumnCount(4);
    ui->objectsTable->setHorizontalHeaderLabels({tr("#"), tr("类型"), tr("状态"), tr("几何信息")});
    ui->objectsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->objectsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->objectsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->objectsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    ui->objectsTable->horizontalHeader()->setMinimumHeight(28);
    ui->objectsTable->verticalHeader()->setVisible(false);
    ui->objectsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->objectsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->objectsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->objectsTable->setToolTip(tr("草图元素列表。单击选择，Ctrl/Shift 多选，双击进入控制点编辑。"));
    styleSketchTable(ui->objectsTable, 118, 190);

    if (ui->editObjectButton)
    {
        ui->editObjectButton->setText(tr("编辑"));
        ui->editObjectButton->setToolTip(tr("把选中的草图对象载入坐标表编辑。"));
    }
    if (ui->deleteObjectButton)
    {
        ui->deleteObjectButton->setText(tr("删除"));
        ui->deleteObjectButton->setToolTip(tr("删除选中的草图对象。"));
    }

    m_toolGroup = new QButtonGroup(this);
    m_toolGroup->setExclusive(true);
    m_toolGroup->addButton(ui->selectButton, static_cast<int>(Command::Select));
    m_toolGroup->addButton(ui->lineButton, static_cast<int>(Command::Polyline));
    m_toolGroup->addButton(ui->rectButton, static_cast<int>(Command::Rectangle));
    m_toolGroup->addButton(ui->circleButton, static_cast<int>(Command::Circle));
    m_toolGroup->addButton(ui->arcButton, static_cast<int>(Command::Arc));
    m_toolGroup->addButton(ui->curveButton, static_cast<int>(Command::Curve));
    m_toolGroup->addButton(ui->pointButton, static_cast<int>(Command::Point));

    connect(ui->planeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Sketch2DDialog::updatePlaneAndView);
    if (ui->outputModeCombo)
        connect(ui->outputModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &Sketch2DDialog::updatePlaneAndView);
    if (ui->importDxfButton)
        connect(ui->importDxfButton, &QPushButton::clicked, this, &Sketch2DDialog::importDxfToSketch);
    connect(ui->offsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &Sketch2DDialog::updatePlaneAndView);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    connect(m_toolGroup, &QButtonGroup::idClicked, this,
            [this](int id) { setCommand(static_cast<Command>(id)); });
#else
    connect(m_toolGroup, QOverload<int>::of(&QButtonGroup::buttonClicked), this,
            [this](int id) { setCommand(static_cast<Command>(id)); });
#endif
    connect(ui->finishButton, &QPushButton::clicked, this, &Sketch2DDialog::finishOpenProfile);
    connect(ui->closeButton, &QPushButton::clicked, this, &Sketch2DDialog::closeProfile);
    connect(ui->clearButton, &QPushButton::clicked, this, &Sketch2DDialog::clearSketch);
    connect(ui->undoButton, &QPushButton::clicked, this, &Sketch2DDialog::undoLast);
    connect(ui->redoButton, &QPushButton::clicked, this, &Sketch2DDialog::redoLast);
    connect(ui->deleteButton, &QPushButton::clicked, this, &Sketch2DDialog::removeSelectedPoint);
    connect(ui->reverseButton, &QPushButton::clicked, this, &Sketch2DDialog::reverseProfile);
    connect(ui->mergeEndpointsButton, &QPushButton::clicked, this, &Sketch2DDialog::mergeProfileEndpoints);
    connect(ui->checkClosureButton, &QPushButton::clicked, this, &Sketch2DDialog::checkProfileClosure);
    connect(ui->generateButton, &QPushButton::clicked, this, &Sketch2DDialog::generateGeometry);
    connect(ui->pointsTable, &QTableWidget::cellChanged, this, [this](int, int)
    {
        syncDocumentFromTable();
        updatePreview();
    });
    connect(ui->pointsTable, &QTableWidget::itemSelectionChanged, this, &Sketch2DDialog::updateActionButtons);
    connect(ui->objectsTable, &QTableWidget::itemSelectionChanged, this, [this]() { updateActionButtons(); updateSelectionHighlight(); });
    connect(ui->objectsTable, &QTableWidget::cellDoubleClicked, this, [this](int, int) { editSelectedObject(); });
    connect(ui->editObjectButton, &QPushButton::clicked, this, &Sketch2DDialog::editSelectedObject);
    connect(ui->deleteObjectButton, &QPushButton::clicked, this, &Sketch2DDialog::deleteSelectedObject);
    connect(ui->snapCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); refreshStatusLabel(); });
    connect(ui->gridSnapCheck, &QCheckBox::toggled, this, [this](bool enabled)
    {
        if (ui->gridSnapStepSpin)
            ui->gridSnapStepSpin->setEnabled(enabled);
        updatePreview();
        refreshStatusLabel();
    });
    connect(ui->gridStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePreview(); });
    connect(ui->gridSnapStepSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePreview(); refreshStatusLabel(); });
    connect(ui->pointSnapSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePreview(); });
    connect(ui->autoCloseToleranceSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double) { updatePreview(); });
    connect(ui->orthoCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); refreshStatusLabel(); });
    connect(ui->autoCloseCheck, &QCheckBox::toggled, this, [this](bool) { updatePreview(); refreshStatusLabel(); });
    if (ui->arcAngleSpin)
    {
        connect(ui->arcAngleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value)
        {
            if (m_updatingArcAngleSpin || !ui->arcAngleRow || !ui->arcAngleRow->isVisible())
                return;
            previewCenterAngleSweep(degreesToRadians(value));
        });
    }
    if (ui->arcAngleApplyButton)
        connect(ui->arcAngleApplyButton, &QPushButton::clicked, this, &Sketch2DDialog::commitCenterAngleSweepFromInput);
    if (ui->arcModeCombo)
    {
        connect(ui->arcModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int)
        {
            if (currentCommand() == Command::Arc &&
                m_document.kind() == SketchDocument::Kind::Arc &&
                !m_document.isFinished() &&
                !m_document.points().empty())
            {
                pushUndo();
                m_document.clear();
                clearPendingPoint();
                refreshPointTable();
                setStatus(commandText(Command::Arc), true);
            }
            updatePreview();
        });
    }
    connect(ui->showAxesCheck, &QCheckBox::toggled, this, [this](bool)
    {
        m_view.setObjectPrefix(m_uuid);
        m_view.setPlaneSettings(planeSettings());
    });

    connect(new QShortcut(QKeySequence(Qt::Key_S), this), &QShortcut::activated, this, [this]() { setCommand(Command::Select); });
    connect(new QShortcut(QKeySequence(Qt::Key_L), this), &QShortcut::activated, this, [this]() { setCommand(Command::Polyline); });
    connect(new QShortcut(QKeySequence(Qt::Key_R), this), &QShortcut::activated, this, [this]() { setCommand(Command::Rectangle); });
    connect(new QShortcut(QKeySequence(Qt::Key_C), this), &QShortcut::activated, this, [this]() { setCommand(Command::Circle); });
    connect(new QShortcut(QKeySequence(Qt::Key_A), this), &QShortcut::activated, this, [this]() { setCommand(Command::Arc); });
    connect(new QShortcut(QKeySequence(Qt::Key_B), this), &QShortcut::activated, this, [this]() { setCommand(Command::Curve); });
    connect(new QShortcut(QKeySequence(Qt::Key_P), this), &QShortcut::activated, this, [this]() { setCommand(Command::Point); });
    connect(new QShortcut(QKeySequence(Qt::Key_Escape), this), &QShortcut::activated, this, &Sketch2DDialog::cancelCurrentCommand);
    // Ctrl+Z / Ctrl+Y 由 eventFilter 处理。草图绘制焦点位于独立的
    // VTK 视图中，页面局部 QShortcut 无法稳定收到这些按键。
    connect(new QShortcut(QKeySequence(Qt::Key_Delete), this), &QShortcut::activated, this, &Sketch2DDialog::removeSelectedPoint);
    if (ui->pointUSpin)
        connect(ui->pointUSpin, &QDoubleSpinBox::editingFinished, this, &Sketch2DDialog::applySelectedPointCoordinates);
    if (ui->pointVSpin)
        connect(ui->pointVSpin, &QDoubleSpinBox::editingFinished, this, &Sketch2DDialog::applySelectedPointCoordinates);

    resetCursorInfo();
    updateInfoPanel();
}

void Sketch2DDialog::initEventBridge()
{
    auto& em = EventManager::instance();
    em.registerHandler(PublicConstants::kActionSketchEnterEdit, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) enterSketchMode(); });
    em.registerHandler(PublicConstants::kActionSketchExitEdit, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) exitSketchMode(); });
    // 顶部工具栏的撤销/重做按当前编辑上下文工作。草图页面激活时，
    // 它们操作草图内部历史，而不是三维几何历史。
    em.registerHandler(PublicConstants::kActionGeomUndo, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) undoLast(); });
    em.registerHandler(PublicConstants::kActionGeomRedo, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) redoLast(); });
    em.registerHandler(PublicConstants::kActionSketchToolLine, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Polyline); });
    em.registerHandler(PublicConstants::kActionSketchToolPolygon, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Polyline); });
    em.registerHandler(PublicConstants::kActionSketchToolRectangle, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Rectangle); });
    em.registerHandler(PublicConstants::kActionSketchToolCircle, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Circle); });
    em.registerHandler(PublicConstants::kActionSketchToolPoint, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Point); });
    // 兼容旧菜单动作：草图已经实时保存，不再存在独立“应用草图”。
    // 旧动作只负责结束当前未完成对象，不再形成第二套提交入口。
    em.registerHandler(PublicConstants::kActionSketchGenerate, this,
                       [this](const QVariantMap&)
                       {
                           if (m_isCurrentPage)
                               finalizeSketchEditing();
                       });

    auto unsupported = [this](const QString& name)
    {
        if (!m_isCurrentPage)
            return;
        setStatus(tr("%1暂未支持。").arg(name), true);
    };

    em.registerHandler(PublicConstants::kActionSketchToolArc, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Arc); });
    em.registerHandler(PublicConstants::kActionSketchToolSpline, this,
                       [this](const QVariantMap&) { if (m_isCurrentPage) setCommand(Command::Curve); });
    em.registerHandler(PublicConstants::kActionSketchConstraintCoincident, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束")); });
    em.registerHandler(PublicConstants::kActionSketchConstraintHorizontal, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束")); });
    em.registerHandler(PublicConstants::kActionSketchConstraintVertical, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束")); });
    em.registerHandler(PublicConstants::kActionSketchConstraintFixed, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束")); });
    em.registerHandler(PublicConstants::kActionSketchConstraintEqual, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束")); });
    em.registerHandler(PublicConstants::kActionSketchSolve, this,
                       [unsupported](const QVariantMap&) { unsupported(QStringLiteral("约束求解")); });
}

void Sketch2DDialog::initViewCallbacks()
{
    m_view.clickHandler = [this](double u, double v, bool rightButton)
    {
        if (m_isCurrentPage)
            handleSketchClick(u, v, rightButton);
    };

    m_view.moveHandler = [this](double u, double v)
    {
        if (m_isCurrentPage)
            handleSketchMove(u, v);
    };

    m_view.boxSelectHandler = [this](double u0, double v0, double u1, double v1, bool crossing, bool additive)
    {
        if (m_isCurrentPage)
            handleSketchBoxSelection(u0, v0, u1, v1, crossing, additive);
    };

    m_view.pointDragStartHandler = [this](double u, double v)
    {
        return m_isCurrentPage && beginControlPointDrag(u, v);
    };

    m_view.pointDragMoveHandler = [this](double u, double v)
    {
        if (m_isCurrentPage)
            dragControlPointTo(u, v);
    };

    m_view.pointDragEndHandler = [this](double u, double v)
    {
        if (m_isCurrentPage)
            endControlPointDrag(u, v);
    };
}

PlaneSettings Sketch2DDialog::planeSettings() const
{
    PlaneSettings settings;
    settings.plane = static_cast<Plane>(ui->planeCombo ? ui->planeCombo->currentIndex() : 0);
    settings.offset = ui->offsetSpin ? ui->offsetSpin->value() : 0.0;
    settings.gridStep = gridStep();
    settings.showAxes = ui->showAxesCheck && ui->showAxesCheck->isChecked();
    settings.outputMode = outputMode();
    return settings;
}

OutputMode Sketch2DDialog::outputMode() const
{
    if (!ui->outputModeCombo)
        return OutputMode::Entity;
    return static_cast<OutputMode>(ui->outputModeCombo->currentIndex());
}

bool Sketch2DDialog::outputAsEntity() const
{
    return outputMode() == OutputMode::Entity;
}

Command Sketch2DDialog::currentCommand() const
{
    return m_currentCommand;
}

StandaloneArcMode Sketch2DDialog::standaloneArcMode() const
{
    const int value = (ui->arcModeCombo && ui->arcModeCombo->currentData().isValid())
                          ? ui->arcModeCombo->currentData().toInt()
                          : static_cast<int>(StandaloneArcMode::ThreePoint);
    return value == static_cast<int>(StandaloneArcMode::CenterAngle)
               ? StandaloneArcMode::CenterAngle
               : StandaloneArcMode::ThreePoint;
}

SketchDocument::ArcKind Sketch2DDialog::currentArcKind() const
{
    return standaloneArcMode() == StandaloneArcMode::CenterAngle
               ? SketchDocument::ArcKind::CenterAngle
               : SketchDocument::ArcKind::Through;
}

QString Sketch2DDialog::currentModeText() const
{
    if (currentCommand() == Command::Polyline)
    {
        switch (m_profileSegmentMode)
        {
        case ProfileSegmentMode::ArcThreePoint:
            return QStringLiteral("三点弧");
        case ProfileSegmentMode::ArcCenterAngle:
            return QStringLiteral("中心角弧");
        case ProfileSegmentMode::ArcTangentEnd:
            return QStringLiteral("切弧");
        case ProfileSegmentMode::Line:
        default:
            return QStringLiteral("直线");
        }
    }

    if (currentCommand() == Command::Arc)
    {
        return currentArcKind() == SketchDocument::ArcKind::CenterAngle
                   ? QStringLiteral("中心角弧")
                   : QStringLiteral("三点弧");
    }

    return commandText(currentCommand());
}

void Sketch2DDialog::setCommand(Command command)
{
    if (command != Command::Polyline)
        resetProfileSegmentState();
    m_currentCommand = command;
    m_view.setBoxSelectionEnabled(command == Command::Select);
    if (command != Command::Select)
    {
        clearEndpointSelection();
        if (ui->objectsTable)
            ui->objectsTable->clearSelection();
        m_view.clearSelectionHighlight();
    }
    if (m_toolGroup)
    {
        auto* button = m_toolGroup->button(static_cast<int>(command));
        if (button)
            button->setChecked(true);
    }

    clearPendingPoint();
    refreshPointTable();

    setStatus(currentModeText(), true);
    updateInfoPanel();
}

void Sketch2DDialog::setStatus(const QString& text, bool ok)
{
    m_statusMessage = text;
    m_statusOk = ok;
    updateInfoPanel();
    refreshStatusLabel();
}

QString Sketch2DDialog::operationHintText() const
{
    const Command command = currentCommand();
    if (command == Command::Select)
    {
        if (m_selectedPointObjectIndex >= 0 && m_selectedPointIndex >= 0)
            return tr("修改下方 U、V 坐标。");

        const QList<int> rows = selectedCommittedIndices();
        if (rows.size() == 1)
            return tr("单击控制点编辑，右键操作。");
        if (rows.size() > 1)
            return tr("已选择 %1 个对象。").arg(rows.size());
        return tr("单击选择，拖动框选，右键绘图。");
    }

    if (command == Command::Polyline)
        return tr("单击下一点；右键切换线段、圆弧或结束。");
    if (command == Command::Rectangle)
        return tr("依次单击两个对角点。");
    if (command == Command::Circle)
        return tr("依次单击圆心和半径点。");
    if (command == Command::Arc)
    {
        return currentArcKind() == SketchDocument::ArcKind::CenterAngle
                   ? tr("依次单击起点、圆心和终点。")
                   : tr("依次单击起点、经过点和终点。");
    }
    if (command == Command::Curve)
        return tr("连续单击控制点，右键结束。");
    if (command == Command::Point)
        return tr("单击添加辅助点。");

    return QString();
}

void Sketch2DDialog::updateInfoPanel()
{
    if (ui->currentOperationLabel)
        ui->currentOperationLabel->setText(currentModeText());
    if (ui->cursorInfoLabel)
        ui->cursorInfoLabel->setText(m_cursorStatusText.isEmpty()
                                       ? QStringLiteral("U --   V --")
                                       : m_cursorStatusText);
    if (ui->operationHintLabel)
    {
        const QString status = m_statusMessage.trimmed();
        ui->operationHintLabel->setText(!m_statusOk && !status.isEmpty()
                                          ? status
                                          : operationHintText());
        ui->operationHintLabel->setStyleSheet(!m_statusOk
                                                ? QStringLiteral("color:#b42318;")
                                                : QStringLiteral("color:#475569;"));
    }
}

void Sketch2DDialog::refreshStatusLabel()
{
    // 草图状态只显示在设置面板，图形区不再常驻显示坐标、模式或吸附文字。
    updateInfoPanel();

    if (ui->statusLabel)
    {
        ui->statusLabel->clear();
        ui->statusLabel->setStyleSheet(QString());
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->setSketchStatusText(QString());
}

void Sketch2DDialog::updateCursorInfo(double u, double v, SketchSnapKind snapKind, bool nearFirstPoint, bool orthoApplied)
{
    Q_UNUSED(snapKind);
    Q_UNUSED(nearFirstPoint);
    Q_UNUSED(orthoApplied);

    m_cursorStatusText = QStringLiteral("U %1 mm   V %2 mm")
                             .arg(formatCoord(u), formatCoord(v));
    refreshStatusLabel();
}

void Sketch2DDialog::resetCursorInfo()
{
    m_cursorStatusText = QStringLiteral("U --   V --");
    refreshStatusLabel();
}

bool Sketch2DDialog::centerAngleControlPoints(Point2D& start, Point2D& center) const
{
    if (currentCommand() == Command::Arc &&
        m_document.kind() == SketchDocument::Kind::Arc &&
        m_document.arcKind() == SketchDocument::ArcKind::CenterAngle &&
        m_document.points().size() == 2 &&
        !m_document.isFinished())
    {
        start = m_document.points()[0];
        center = m_document.points()[1];
        return true;
    }

    if (currentCommand() == Command::Polyline &&
        m_profileSegmentMode == ProfileSegmentMode::ArcCenterAngle &&
        m_document.kind() == SketchDocument::Kind::Profile &&
        m_profileArcMidPending &&
        !m_document.points().empty() &&
        !m_document.isClosed() &&
        !m_document.isFinished())
    {
        start = m_document.points().back();
        center = m_profileArcMid;
        return true;
    }

    return false;
}

void Sketch2DDialog::updateArcAngleDisplay(double sweep, bool active)
{
    m_arcAngleStatusText = active
                               ? QStringLiteral("角度：%1").arg(formatAngleDegrees(sweep))
                               : QString();

    if (ui->arcAngleRow)
        ui->arcAngleRow->setVisible(active);

    if (ui->arcAngleSpin)
    {
        const QSignalBlocker blocker(ui->arcAngleSpin);
        m_updatingArcAngleSpin = true;
        ui->arcAngleSpin->setValue(active ? uiArcSweepDegrees(sweep) : 0.0);
        m_updatingArcAngleSpin = false;
    }

    if (ui->arcAngleApplyButton)
        ui->arcAngleApplyButton->setEnabled(active && std::abs(sweep) > 1.0e-9);

    refreshStatusLabel();
}

bool Sketch2DDialog::previewCenterAngleSweep(double sweep)
{
    Point2D start;
    Point2D center;
    if (!centerAngleControlPoints(start, center))
        return false;

    const double geometrySweep = clampArcSweep(sweep);
    const Point2D anglePoint = centerAngleEndpointForSweep(start, center, geometrySweep);
    m_centerAngleTracking = true;
    m_centerAngleTrackStart = start;
    m_centerAngleTrackCenter = center;
    m_centerAngleLastAngle = std::atan2(anglePoint.v - center.v, anglePoint.u - center.u);
    m_centerAngleSweep = sweep;
    updateArcAngleDisplay(sweep, true);
    updateRubberLine(anglePoint.u, anglePoint.v);
    return true;
}

bool Sketch2DDialog::commitCenterAngleSweep(double sweep)
{
    Point2D start;
    Point2D center;
    if (!centerAngleControlPoints(start, center))
        return false;

    const double geometrySweep = clampArcSweep(sweep);
    const Point2D anglePoint = centerAngleEndpointForSweep(start, center, geometrySweep);
    Point2D midPoint;
    Point2D endPoint;
    if (!centerAngleArcThroughPoints(start, center, anglePoint, midPoint, endPoint, geometrySweep))
    {
        setStatus(tr("中心角弧无效。"), false);
        return false;
    }

    if (currentCommand() == Command::Arc)
    {
        if (isDegenerateCenterAngleArc(start, center, anglePoint, geometrySweep))
        {
            setStatus(tr("圆弧半径或角度过小。"), false);
            return false;
        }

        pushUndo();
        m_document.appendArcPoint(anglePoint.u, anglePoint.v, SketchDocument::ArcKind::CenterAngle);
        m_document.setArcSweep(geometrySweep);
        m_document.setFinished(true);
        resetCenterAngleTracking();
        commitActiveDocument(tr("圆弧已生成。"));
        return true;
    }

    if (currentCommand() == Command::Polyline && m_profileSegmentMode == ProfileSegmentMode::ArcCenterAngle)
    {
        if (isDegenerateArc(start, midPoint, endPoint))
        {
            setStatus(tr("中心角弧无效。"), false);
            return false;
        }

        const bool closesProfile = endpointClosesActiveProfile(endPoint);
        if (closesProfile)
            endPoint = m_document.points().front();

        pushUndo();
        m_document.appendProfileArcThrough(midPoint, endPoint);
        m_profileArcMidPending = false;
        resetCenterAngleTracking();
        if (closesProfile)
        {
            commitClosedActiveProfile();
            return true;
        }
        refreshPointTable();
        if (ui->pointsTable && !m_document.displayPoints().empty())
            ui->pointsTable->selectRow(static_cast<int>(m_document.displayPoints().size()) - 1);
        updatePreview();
        setStatus(tr("中心角弧"), true);
        return true;
    }

    return false;
}

void Sketch2DDialog::commitCenterAngleSweepFromInput()
{
    if (!ui->arcAngleSpin)
        return;
    commitCenterAngleSweep(degreesToRadians(ui->arcAngleSpin->value()));
}

void Sketch2DDialog::enterSketchMode()
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->setSketchModeEnabled(true, tr("二维草图"));

    m_view.setObjectPrefix(m_uuid);
    m_view.setPlaneSettings(planeSettings());
    m_view.enter();
    resetCursorInfo();
    updatePreview();
    autoFitSketchView();

    // 页面切换时 VTK 窗口尺寸可能还没有完成更新。事件循环和布局稳定后
    // 再按最终视口校正一次，避免用户每次进入草图都要手动正视和缩放。
    QTimer::singleShot(0, this,
        [this]()
        {
            if (m_isCurrentPage)
                autoFitSketchView();
        });
    QTimer::singleShot(80, this,
        [this]()
        {
            if (m_isCurrentPage)
                autoFitSketchView();
        });

    setStatus(tr("就绪"), true);
}

void Sketch2DDialog::exitSketchMode()
{
    m_view.exit();
    clearPendingPoint();
    resetCursorInfo();

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->setSketchModeEnabled(false);
}

void Sketch2DDialog::hideCommittedPresentationForEditing()
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!vis || m_uuid.isEmpty())
        return;

    // 编辑草图时只保留 SketchViewController 的统一预览。普通几何 Actor
    // 必须彻底隐藏，否则模型树选中高亮会与草图预览叠加成两种蓝色。
    vis->setVisible(GeometryDisplayManager::facePropId(m_uuid), false, false);
    vis->setVisible(GeometryDisplayManager::edgePropId(m_uuid), false, false);

    // 同时直接压掉当前对象 Actor 的可见性，防止 actor 重新注册或树高亮刷新后
    // 留下一份普通几何显示；这里只改临时 VTK 状态，不改逻辑 visible 状态。
    if (auto* manager = GeomManager::GeomManagerInstance::getInstance())
    {
        if (auto* object = manager->getGeomShapeObject(m_uuid))
        {
            if (object->getSurfActor())
                object->getSurfActor()->SetVisibility(0);
            if (object->getEdgeActor())
                object->getEdgeActor()->SetVisibility(0);
        }
    }
}

void Sketch2DDialog::updatePlaneAndView()
{
    if (m_loadingJson || !m_isCurrentPage)
    {
        refreshObjectTable();
        return;
    }

    clearPendingPoint();
    m_view.setObjectPrefix(m_uuid);
    m_view.setPlaneSettings(planeSettings());
    refreshObjectTable();
    updatePreview();

    // 草图是参数化对象：平面、偏移或输出类型变化后，已应用几何也要同步重建。
    if (m_isCurrentPage && hasVisibleGeometryDocuments())
        updateGeometryFromDocuments(tr("草图设置已更新。"), false);
}

void Sketch2DDialog::autoFitSketchView()
{
    std::vector<Point2D> points;
    const auto appendDocumentPoints = [&points](const SketchDocument& document)
    {
        const auto& displayPoints = document.displayPoints();
        points.insert(points.end(), displayPoints.begin(), displayPoints.end());
    };

    appendDocumentPoints(m_document);
    for (const SketchDocument& document : m_committedDocuments)
        appendDocumentPoints(document);
    points.insert(points.end(), m_referencePoints.begin(), m_referencePoints.end());

    if (!m_view.fitToPoints(points, 1.25))
        m_view.alignCameraToPlane();
}

void Sketch2DDialog::refreshPointTable()
{
    if (!ui->pointsTable)
        return;

    const QSignalBlocker blocker(ui->pointsTable);
    ui->pointsTable->setRowCount(0);

    const auto& points = tableShowsReferencePoints() ? m_referencePoints : m_document.displayPoints();
    const bool readOnlyDisplayPoints = !tableShowsReferencePoints() &&
                                       (m_document.hasCustomDisplayPoints() ||
                                        (m_document.kind() == SketchDocument::Kind::Profile &&
                                         m_document.hasProfileSegments()));
    if (readOnlyDisplayPoints)
        ui->pointsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    else
        ui->pointsTable->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed | QAbstractItemView::AnyKeyPressed);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto& p = points[i];
        const int row = ui->pointsTable->rowCount();
        ui->pointsTable->insertRow(row);
        QString rowLabel = QString::number(row + 1);
        if (!tableShowsReferencePoints())
        {
            if (m_document.kind() == SketchDocument::Kind::Circle && row == 0)
            {
                rowLabel = tr("圆心");
            }
            else if (m_document.kind() == SketchDocument::Kind::Arc)
            {
                if (m_document.arcKind() == SketchDocument::ArcKind::CenterAngle)
                {
                    if (row == 0) rowLabel = tr("起点");
                    else if (row == 1) rowLabel = tr("中心");
                    else if (row == 2) rowLabel = tr("角度");
                }
                else
                {
                    if (row == 0) rowLabel = tr("起点");
                    else if (row == 1) rowLabel = tr("经过");
                    else if (row == 2) rowLabel = tr("终点");
                }
            }
        }
        ui->pointsTable->setVerticalHeaderItem(row, new QTableWidgetItem(rowLabel));
        ui->pointsTable->setItem(row, 0, new QTableWidgetItem(QString::number(p.u, 'f', 3)));
        ui->pointsTable->setItem(row, 1, new QTableWidgetItem(QString::number(p.v, 'f', 3)));
    }

    updatePointTableTitle();
}

void Sketch2DDialog::refreshObjectTable()
{
    if (!ui->objectsTable)
        return;

    const QList<int> previousSelection = selectedCommittedIndices();
    const QSignalBlocker blocker(ui->objectsTable);
    ui->objectsTable->setRowCount(0);

    for (int i = 0; i < static_cast<int>(m_committedDocuments.size()); ++i)
    {
        const auto& document = documentForCommittedRow(i);
        const int row = ui->objectsTable->rowCount();
        ui->objectsTable->insertRow(row);
        ui->objectsTable->setItem(row, 0, new QTableWidgetItem(QString::number(row + 1)));
        ui->objectsTable->setItem(row, 1, new QTableWidgetItem(documentTypeText(document)));
        ui->objectsTable->setItem(row, 2, new QTableWidgetItem(documentStateText(document)));
        ui->objectsTable->setItem(row, 3, new QTableWidgetItem(documentDetailText(document)));
    }

    QList<int> rowsToRestore;
    if (m_editingCommittedIndex >= 0 && m_editingCommittedIndex < static_cast<int>(m_committedDocuments.size()))
        rowsToRestore.push_back(m_editingCommittedIndex);
    else
    {
        for (int row : previousSelection)
        {
            if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()))
                rowsToRestore.push_back(row);
        }
    }
    for (int row : rowsToRestore)
    {
        const QModelIndex index = ui->objectsTable->model()->index(row, 0);
        ui->objectsTable->selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
    }

    if (ui->objectsTitleLabel)
        ui->objectsTitleLabel->setText(tr("已应用对象：%1 个").arg(static_cast<int>(m_committedDocuments.size())));

    updateActionButtons();
}

void Sketch2DDialog::updatePointTableTitle()
{
    if (!ui->pointsTitleLabel)
        return;

    if (tableShowsReferencePoints())
    {
        ui->pointsTitleLabel->setText(tr("辅助点：%1").arg(static_cast<int>(m_referencePoints.size())));
        return;
    }

    switch (m_document.kind())
    {
    case SketchDocument::Kind::Profile:
        ui->pointsTitleLabel->setText(tr("轮廓：%1 点，%2")
                                    .arg(documentDisplayPointCount(m_document))
                                    .arg(documentEffectivelyClosed(m_document) ? tr("已闭合") : tr("开放")));
        break;
    case SketchDocument::Kind::Circle:
        ui->pointsTitleLabel->setText(tr("圆：R=%1").arg(formatCoord(m_document.circleRadius())));
        break;
    case SketchDocument::Kind::Arc:
        ui->pointsTitleLabel->setText((m_document.arcKind() == SketchDocument::ArcKind::CenterAngle
                                         ? tr("中心角：%1 / 3")
                                         : tr("圆弧：%1 / 3"))
                                    .arg(static_cast<int>(m_document.points().size())));
        break;
    case SketchDocument::Kind::Curve:
        ui->pointsTitleLabel->setText(tr("样条：%1 点")
                                    .arg(documentDisplayPointCount(m_document)));
        break;
    default:
        ui->pointsTitleLabel->setText(tr("坐标：空"));
        break;
    }
}

int Sketch2DDialog::selectedCommittedIndex() const
{
    const QList<int> rows = selectedCommittedIndices();
    return rows.size() == 1 ? rows.front() : -1;
}

QList<int> Sketch2DDialog::selectedCommittedIndices() const
{
    QList<int> rows;
    if (!ui->objectsTable || !ui->objectsTable->selectionModel())
        return rows;

    const QModelIndexList selected = ui->objectsTable->selectionModel()->selectedRows();
    for (const QModelIndex& index : selected)
    {
        const int row = index.row();
        if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()) && !rows.contains(row))
            rows.push_back(row);
    }
    std::sort(rows.begin(), rows.end());
    return rows;
}

void Sketch2DDialog::selectObjectRows(const QList<int>& rows, bool additive)
{
    if (!ui->objectsTable || !ui->objectsTable->selectionModel())
        return;

    if (!additive)
        ui->objectsTable->clearSelection();

    for (int row : rows)
    {
        if (row < 0 || row >= ui->objectsTable->rowCount())
            continue;
        const QModelIndex index = ui->objectsTable->model()->index(row, 0);
        ui->objectsTable->selectionModel()->select(
            index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        ui->objectsTable->setCurrentCell(row, 0, QItemSelectionModel::NoUpdate);
    }
    updateSelectionHighlight();
    updateActionButtons();
    updateInfoPanel();
}

void Sketch2DDialog::clearSketchSelection()
{
    if (ui->objectsTable)
        ui->objectsTable->clearSelection();
    clearEndpointSelection();
    updateSelectionHighlight();
    updateActionButtons();
    updateInfoPanel();
}

QString Sketch2DDialog::documentTypeText(const SketchDocument& document) const
{
    switch (document.kind())
    {
    case SketchDocument::Kind::Circle:
        return outputAsEntity() ? tr("圆实体") : tr("圆曲线");
    case SketchDocument::Kind::Arc:
        return tr("圆弧");
    case SketchDocument::Kind::Curve:
        return tr("平滑样条");
    case SketchDocument::Kind::Profile:
    {
        const bool closed = documentEffectivelyClosed(document);
        if (looksLikeRectangle(document))
            return outputAsEntity() && closed ? tr("矩形实体") : tr("矩形曲线");
        return outputAsEntity() && closed ? tr("实体") : tr("曲线");
    }
    default:
        return tr("未知");
    }
}

QString Sketch2DDialog::documentDetailText(const SketchDocument& document) const
{
    switch (document.kind())
    {
    case SketchDocument::Kind::Circle:
        if (!document.points().empty())
        {
            const auto& c = document.points().front();
            return tr("圆心 (%1, %2)，R=%3")
                .arg(shortNumber(c.u))
                .arg(shortNumber(c.v))
                .arg(shortNumber(document.circleRadius()));
        }
        return tr("圆心未定义");
    case SketchDocument::Kind::Profile:
        return tr("%1 点，%2")
            .arg(documentDisplayPointCount(document))
            .arg(outputAsEntity() && documentEffectivelyClosed(document) ? tr("实体") : tr("曲线"));
    case SketchDocument::Kind::Arc:
        return document.arcKind() == SketchDocument::ArcKind::CenterAngle
                   ? tr("中心角：起点 / 中心 / 角度")
                   : tr("三点：起点 / 经过点 / 终点");
    case SketchDocument::Kind::Curve:
        if (document.hasCustomDisplayPoints())
            return tr("%1 个关键点，自定义曲线").arg(documentDisplayPointCount(document));
        return tr("%1 个经过点，样条").arg(documentDisplayPointCount(document));
    default:
        return tr("不可编辑对象");
    }
}

QString Sketch2DDialog::documentStateText(const SketchDocument& document) const
{
    if (document.isConstruction())
        return tr("构造");
    if (document.kind() == SketchDocument::Kind::Circle)
        return tr("闭合");
    if (document.kind() == SketchDocument::Kind::Profile)
        return documentEffectivelyClosed(document) ? tr("闭合") : tr("开放");
    return tr("开放");
}

bool Sketch2DDialog::documentEffectivelyClosed(const SketchDocument& document) const
{
    if (document.kind() == SketchDocument::Kind::Circle)
        return true;
    if (document.kind() != SketchDocument::Kind::Profile)
        return false;
    if (document.isClosed())
        return true;

    if (document.hasProfileSegments())
    {
        const auto& segments = document.profileSegments();
        if (!segments.empty())
        {
            const auto& first = segments.front().start;
            const auto& last = segments.back().end;
            return std::hypot(first.u - last.u, first.v - last.v) <= autoCloseTolerance();
        }
    }

    const auto& points = document.points();
    if (points.size() < 3)
        return false;
    return std::hypot(points.front().u - points.back().u,
                      points.front().v - points.back().v) <= autoCloseTolerance();
}

int Sketch2DDialog::documentDisplayPointCount(const SketchDocument& document) const
{
    if (document.kind() != SketchDocument::Kind::Profile)
        return static_cast<int>(document.displayPoints().size());

    int count = static_cast<int>(document.displayPoints().size());
    const auto& points = document.displayPoints();
    if (count > 1 && std::hypot(points.front().u - points.back().u,
                                points.front().v - points.back().v) <= autoCloseTolerance())
        --count;
    return std::max(0, count);
}

double Sketch2DDialog::distanceToDocument(const SketchDocument& document, double u, double v) const
{
    const Point2D p{u, v};
    if (document.kind() == SketchDocument::Kind::Circle && !document.points().empty())
    {
        const auto& c = document.points().front();
        return std::abs(std::hypot(p.u - c.u, p.v - c.v) - document.circleRadius());
    }

    std::vector<Point2D> points;
    if (document.kind() == SketchDocument::Kind::Arc && document.points().size() == 3)
    {
        const auto& src = document.points();
        points = document.arcKind() == SketchDocument::ArcKind::CenterAngle
                     ? centerAngleArcGuide(src[0], src[1], src[2], document.arcSweep())
                     : arcGuide(src[0], src[1], src[2]);
    }
    else if (document.kind() == SketchDocument::Kind::Curve && document.points().size() >= 3)
    {
        points = curveGuide(document.points());
    }
    else if (document.kind() == SketchDocument::Kind::Profile)
    {
        if (document.hasProfileSegments())
        {
            for (const auto& segment : document.profileSegments())
            {
                if (segment.kind == SketchDocument::ProfileSegmentKind::Line)
                {
                    if (points.empty())
                        points.push_back(segment.start);
                    points.push_back(segment.end);
                }
                else if (segment.kind == SketchDocument::ProfileSegmentKind::ArcThrough)
                {
                    const auto sampled = arcGuide(segment.start, segment.control, segment.end);
                    for (const auto& pt : sampled)
                        points.push_back(pt);
                }
                else
                {
                    const auto sampled = tangentArcGuide(segment.start, segment.control, segment.end);
                    for (const auto& pt : sampled)
                        points.push_back(pt);
                }
            }
        }
        else
        {
            points = document.points();
        }
    }
    else
    {
        return 1.0e100;
    }

    if (points.size() < 2)
        return 1.0e100;

    double best = 1.0e100;
    for (std::size_t i = 1; i < points.size(); ++i)
        best = std::min(best, distanceToSegment(p, points[i - 1], points[i]));
    if (document.kind() == SketchDocument::Kind::Profile && document.isClosed() && points.size() >= 3)
        best = std::min(best, distanceToSegment(p, points.back(), points.front()));
    return best;
}

int Sketch2DDialog::findNearestCommittedObject(double u, double v) const
{
    // 视图区选择不能太苛刻，否则用户必须精确点在线上才选中。
    // 这里按网格步长给一个适中容差，同时仍然只按最近距离选择一个对象。
    const double threshold = std::max(pointSnapTolerance(), precisionStep() * 0.35);
    double best = threshold;
    int bestIndex = -1;
    for (int i = 0; i < static_cast<int>(m_committedDocuments.size()); ++i)
    {
        const double d = distanceToDocument(documentForCommittedRow(i), u, v);
        if (d < best)
        {
            best = d;
            bestIndex = i;
        }
    }
    return bestIndex;
}

bool Sketch2DDialog::findNearestEndpoint(double u, double v, int& objectIndex, int& pointIndex, Point2D& point) const
{
    // 端点选择使用比吸附更宽松的容差。吸附需要精确，选择则应允许
    // 用户在可见控制点附近点击，而不是必须压中一个像素。
    const double threshold = std::max(pointSnapTolerance() * 2.0, precisionStep() * 0.75);
    double best = threshold;
    objectIndex = -1;
    pointIndex = -1;

    for (int row = 0; row < static_cast<int>(m_committedDocuments.size()); ++row)
    {
        const auto& document = documentForCommittedRow(row);
        // 只把真正可编辑的控制点作为端点。displayPoints() 对圆、圆弧和
        // 样条可能包含大量采样点，不能把这些采样点误当成可编辑端点。
        const auto& points = document.points();
        for (int i = 0; i < static_cast<int>(points.size()); ++i)
        {
            const double distance = std::hypot(points[static_cast<std::size_t>(i)].u - u,
                                               points[static_cast<std::size_t>(i)].v - v);
            if (distance < best)
            {
                best = distance;
                objectIndex = row;
                pointIndex = i;
                point = points[static_cast<std::size_t>(i)];
            }
        }
    }
    return objectIndex >= 0;
}

bool Sketch2DDialog::documentSelectionBounds(const SketchDocument& document,
                                             double& minU, double& maxU,
                                             double& minV, double& maxV) const
{
    std::vector<Point2D> points;
    if (document.kind() == SketchDocument::Kind::Circle && !document.points().empty())
    {
        const auto& center = document.points().front();
        const double radius = document.circleRadius();
        minU = center.u - radius;
        maxU = center.u + radius;
        minV = center.v - radius;
        maxV = center.v + radius;
        return radius > geometryTolerance();
    }
    if (document.kind() == SketchDocument::Kind::Arc && document.points().size() == 3)
    {
        const auto& src = document.points();
        points = document.arcKind() == SketchDocument::ArcKind::CenterAngle
                     ? centerAngleArcGuide(src[0], src[1], src[2], document.arcSweep())
                     : arcGuide(src[0], src[1], src[2]);
    }
    else if (document.kind() == SketchDocument::Kind::Curve && document.points().size() >= 3)
    {
        points = curveGuide(document.points());
    }
    else if (document.kind() == SketchDocument::Kind::Profile && document.hasProfileSegments())
    {
        for (const auto& segment : document.profileSegments())
        {
            std::vector<Point2D> sampled;
            if (segment.kind == SketchDocument::ProfileSegmentKind::Line)
                sampled = {segment.start, segment.end};
            else if (segment.kind == SketchDocument::ProfileSegmentKind::ArcThrough)
                sampled = arcGuide(segment.start, segment.control, segment.end);
            else
                sampled = tangentArcGuide(segment.start, segment.control, segment.end);
            points.insert(points.end(), sampled.begin(), sampled.end());
        }
    }
    else
    {
        points = document.displayPoints();
    }

    if (points.empty())
        return false;
    minU = maxU = points.front().u;
    minV = maxV = points.front().v;
    for (const auto& p : points)
    {
        minU = std::min(minU, p.u);
        maxU = std::max(maxU, p.u);
        minV = std::min(minV, p.v);
        maxV = std::max(maxV, p.v);
    }
    return true;
}

Command Sketch2DDialog::preferredCommandForDocument(const SketchDocument& document) const
{
    if (document.kind() == SketchDocument::Kind::Circle)
        return Command::Circle;
    if (document.kind() == SketchDocument::Kind::Arc)
        return Command::Arc;
    if (document.kind() == SketchDocument::Kind::Curve)
        return Command::Curve;
    if (looksLikeRectangle(document))
        return Command::Rectangle;
    return Command::Polyline;
}

bool Sketch2DDialog::hasEditableActiveDocument() const
{
    return m_document.isRenderable() || !m_document.points().empty();
}

bool Sketch2DDialog::isEditingCommittedObject() const
{
    return m_editingCommittedIndex >= 0 &&
           m_editingCommittedIndex < static_cast<int>(m_committedDocuments.size());
}

const SketchDocument& Sketch2DDialog::documentForCommittedRow(int row) const
{
    if (row == m_editingCommittedIndex && !m_document.points().empty())
        return m_document;
    return m_committedDocuments[static_cast<std::size_t>(row)];
}

void Sketch2DDialog::selectObjectRow(int row)
{
    if (row < 0 || row >= static_cast<int>(m_committedDocuments.size()))
        return;
    selectObjectRows(QList<int>{row}, false);
}

void Sketch2DDialog::editSelectedObject()
{
    const int row = selectedCommittedIndex();
    if (row < 0)
    {
        setStatus(tr("请选择一个草图对象。"), false);
        return;
    }

    // 新交互不再把对象载入已隐藏的坐标表，也不切回绘图命令。
    // 保持对象处于已提交状态，在图形区显示其控制点，用户点击端点后
    // 通过紧凑 U/V 输入框直接修改，修改结果立即重建草图。
    setCommand(Command::Select);
    clearEndpointSelection();
    selectObjectRow(row);
    const SketchDocument& document = documentForCommittedRow(row);
    m_view.updateSelectedPointMarkers(document.points());
    setStatus(tr("请选择一个端点，然后修改 U、V 坐标。"), true);
}

void Sketch2DDialog::deleteSelectedObject()
{
    QList<int> rows = selectedCommittedIndices();
    if (rows.isEmpty())
    {
        setStatus(tr("未选择对象。"), true);
        return;
    }

    pushUndo();
    std::sort(rows.begin(), rows.end(), std::greater<int>());
    for (int row : rows)
    {
        if (row < 0 || row >= static_cast<int>(m_committedDocuments.size()))
            continue;
        m_committedDocuments.erase(m_committedDocuments.begin() + row);
        if (row == m_editingCommittedIndex)
        {
            m_document.clear();
            m_editingCommittedIndex = -1;
        }
        else if (m_editingCommittedIndex > row)
        {
            --m_editingCommittedIndex;
        }
    }

    clearPendingPoint();
    clearEndpointSelection();
    refreshPointTable();
    refreshObjectTable();
    updatePreview();
    updateGeometryFromDocuments(tr("已删除选中的草图对象。"), false);
    setStatus(tr("已删除 %1 个对象。").arg(rows.size()), true);
}

void Sketch2DDialog::copySelectedObjects()
{
    const QList<int> rows = selectedCommittedIndices();
    if (rows.isEmpty())
    {
        setStatus(tr("未选择对象。"), true);
        return;
    }

    pushUndo();
    const double offset = std::max(gridSnapStep(), precisionStep());
    QList<int> newRows;
    for (int row : rows)
    {
        if (row < 0 || row >= static_cast<int>(m_committedDocuments.size()))
            continue;
        m_committedDocuments.push_back(translatedDocument(documentForCommittedRow(row), offset, offset));
        newRows.push_back(static_cast<int>(m_committedDocuments.size()) - 1);
    }
    refreshObjectTable();
    selectObjectRows(newRows, false);
    updatePreview();
    updateGeometryFromDocuments(tr("已复制草图对象。"), false);
    setStatus(tr("已复制 %1 个对象。").arg(newRows.size()), true);
}

void Sketch2DDialog::toggleSelectedConstruction()
{
    const QList<int> rows = selectedCommittedIndices();
    if (rows.isEmpty())
    {
        setStatus(tr("未选择对象。"), true);
        return;
    }

    bool makeConstruction = false;
    for (int row : rows)
    {
        if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()) &&
            !m_committedDocuments[static_cast<std::size_t>(row)].isConstruction())
        {
            makeConstruction = true;
            break;
        }
    }

    pushUndo();
    for (int row : rows)
    {
        if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()))
            m_committedDocuments[static_cast<std::size_t>(row)].setConstruction(makeConstruction);
    }
    updatePreview();
    updateGeometryFromDocuments(makeConstruction ? tr("已设为构造线。") : tr("已转为普通几何。"), false);
    setStatus(makeConstruction ? tr("已设为构造线。") : tr("已转为普通几何。"), true);
}

void Sketch2DDialog::syncDocumentFromTable()
{
    if (!ui->pointsTable)
        return;

    std::vector<Point2D> points;
    for (int row = 0; row < ui->pointsTable->rowCount(); ++row)
    {
        double u = 0.0;
        double v = 0.0;
        if (readPointRow(row, u, v))
            points.push_back({u, v});
    }

    if (tableShowsReferencePoints())
    {
        m_referencePoints = points;
        return;
    }

    if (m_document.hasCustomDisplayPoints())
    {
        m_document.setDisplayPoints(points);
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Circle)
    {
        if (!points.empty())
            m_document.setCircle(points.front().u, points.front().v, m_document.circleRadius());
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Arc)
    {
        if (!points.empty())
            m_document.setArc(points, points.size() == 3, m_document.arcKind());
        else
            m_document.clear();
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Curve)
    {
        if (!points.empty())
            m_document.setCurve(points, m_document.isFinished());
        else
            m_document.clear();
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Profile && m_document.hasProfileSegments())
    {
        // Continuous profiles keep real line/arc segments. The coordinate table only shows
        // construction points; blindly rebuilding from the table would flatten arcs into
        // many straight segments and cause vertical seam lines after extrusion.
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Profile && m_document.hasCustomDisplayPoints())
    {
        // Composite profiles may contain sampled geometry for arcs. The coordinate table shows
        // only construction/key points, so table edits should not overwrite the sampled geometry.
        m_document.setDisplayPoints(points);
        return;
    }

    if (!points.empty())
        m_document.setProfile(points, m_document.isClosed(), m_document.isFinished());
    else
        m_document.clear();
}

bool Sketch2DDialog::readPointRow(int row, double& u, double& v) const
{
    if (!ui->pointsTable || row < 0 || row >= ui->pointsTable->rowCount())
        return false;

    auto* uItem = ui->pointsTable->item(row, 0);
    auto* vItem = ui->pointsTable->item(row, 1);
    if (!uItem || !vItem)
        return false;

    bool okU = false;
    bool okV = false;
    u = uItem->text().toDouble(&okU);
    v = vItem->text().toDouble(&okV);
    return okU && okV;
}

void Sketch2DDialog::appendPointToTable(double u, double v, bool selectRow)
{
    if (!ui->pointsTable)
        return;

    const QSignalBlocker blocker(ui->pointsTable);
    const int row = ui->pointsTable->rowCount();
    ui->pointsTable->insertRow(row);
    ui->pointsTable->setItem(row, 0, new QTableWidgetItem(QString::number(u, 'f', 3)));
    ui->pointsTable->setItem(row, 1, new QTableWidgetItem(QString::number(v, 'f', 3)));
    if (selectRow)
    {
        ui->pointsTable->selectRow(row);
        ui->pointsTable->scrollToItem(ui->pointsTable->item(row, 0), QAbstractItemView::PositionAtCenter);
    }
}

double Sketch2DDialog::gridStep() const
{
    return ui->gridStepSpin ? std::max(0.001, ui->gridStepSpin->value()) : 1.0;
}

double Sketch2DDialog::gridSnapStep() const
{
    return ui->gridSnapStepSpin ? std::max(0.001, ui->gridSnapStepSpin->value()) : gridStep();
}

double Sketch2DDialog::precisionStep() const
{
    if (ui->gridSnapCheck && ui->gridSnapCheck->isChecked())
        return std::min(gridStep(), gridSnapStep());
    return gridStep();
}

double Sketch2DDialog::pointSnapRange() const
{
    return ui->pointSnapSpin ? std::max(0.001, ui->pointSnapSpin->value()) : 0.12;
}

double Sketch2DDialog::autoCloseRange() const
{
    return ui->autoCloseToleranceSpin ? std::max(0.001, ui->autoCloseToleranceSpin->value()) : 0.25;
}

bool Sketch2DDialog::orthoEnabled() const
{
    const bool locked = ui->orthoCheck && ui->orthoCheck->isChecked();
    const bool shiftHeld = (QApplication::keyboardModifiers() & Qt::ShiftModifier);
    return locked || shiftHeld;
}

bool Sketch2DDialog::applyOrthoFrom(const Point2D& base, double& u, double& v) const
{
    if (!orthoEnabled())
        return false;

    const double du = u - base.u;
    const double dv = v - base.v;
    if (std::abs(du) >= std::abs(dv))
        v = base.v;
    else
        u = base.u;
    return true;
}

bool Sketch2DDialog::tableShowsReferencePoints() const
{
    return currentCommand() == Command::Point;
}

bool Sketch2DDialog::hasActivePoints() const
{
    return !m_document.points().empty();
}

bool Sketch2DDialog::hasAnySketchData() const
{
    return hasActivePoints() || !m_committedDocuments.empty() || !m_referencePoints.empty();
}

bool Sketch2DDialog::hasVisibleGeometryDocuments() const
{
    return !visibleDocuments().empty();
}

double Sketch2DDialog::geometryTolerance() const
{
    return std::max(1.0e-9, precisionStep() * 1.0e-6);
}

double Sketch2DDialog::pointSnapTolerance() const
{
    return std::max(geometryTolerance(), pointSnapRange());
}

double Sketch2DDialog::autoCloseTolerance() const
{
    return std::max(geometryTolerance(), autoCloseRange());
}

bool Sketch2DDialog::isDegenerateRectangle(const Point2D& a, const Point2D& b) const
{
    const double eps = geometryTolerance();
    return std::abs(a.u - b.u) <= eps || std::abs(a.v - b.v) <= eps;
}

bool Sketch2DDialog::isDegenerateRadius(double radius) const
{
    return radius <= geometryTolerance();
}

bool Sketch2DDialog::isDegenerateArc(const Point2D& a, const Point2D& b, const Point2D& c) const
{
    const double step = precisionStep();
    const double eps = std::max(geometryTolerance() * geometryTolerance(), step * step * 1.0e-8);
    if (std::hypot(a.u - b.u, a.v - b.v) <= geometryTolerance())
        return true;
    if (std::hypot(b.u - c.u, b.v - c.v) <= geometryTolerance())
        return true;
    if (std::hypot(a.u - c.u, a.v - c.v) <= geometryTolerance())
        return true;
    return isNearlyCollinear(a, b, c, eps);
}

bool Sketch2DDialog::isDegenerateCenterAngleArc(const Point2D& start,
                                                const Point2D& center,
                                                const Point2D& anglePoint,
                                                double arcSweep) const
{
    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    if (radius <= geometryTolerance())
        return true;
    if (std::hypot(anglePoint.u - center.u, anglePoint.v - center.v) <= geometryTolerance())
        return true;

    bool endpointOk = false;
    const Point2D end = centerAngleEndpoint(start, center, anglePoint, &endpointOk);
    if (!endpointOk)
        return true;

    const double minSweep = std::max(1.0e-9, geometryTolerance() / std::max(radius, geometryTolerance()));
    if (std::abs(arcSweep) > 1.0e-12)
        return std::abs(clampArcSweep(arcSweep)) <= minSweep;

    bool sweepOk = false;
    const double sweep = centerAngleSweepFromPoint(start, center, anglePoint, &sweepOk);
    return !sweepOk || std::abs(sweep) <= minSweep;
}

void Sketch2DDialog::cleanupGeneratedGeometry()
{
    GeometryCleanup::removeExistingGeometry(m_uuid);
    if (auto* projectManager = ServiceRegistry::instance().getService<ProjectManagerService>())
        projectManager->setNodeDimmed(m_uuid, true);
}

void Sketch2DDialog::finishActiveDocumentBeforeNewObject()
{
    if (m_document.points().empty())
        return;

    if (m_document.isRenderable())
    {
        const Command resumeCommand = currentCommand();
        pushUndo();
        if (m_document.kind() == SketchDocument::Kind::Profile && !m_document.isClosed())
            m_document.setFinished(true);
        if (m_document.kind() == SketchDocument::Kind::Curve)
            m_document.setFinished(true);
        commitActiveDocument(tr("已应用当前对象。"));
        // 本函数用于“开始同一工具的下一个对象”，因此提交旧对象后
        // 恢复调用方正在使用的工具；普通完成操作仍会返回选择模式。
        setCommand(resumeCommand);
        return;
    }

    pushUndo();
    m_document.clear();
    refreshPointTable();
    updatePreview();
}

void Sketch2DDialog::pushUndo()
{
    m_undoStack.push_back({m_committedDocuments, m_document, m_referencePoints, m_editingCommittedIndex});
    if (m_undoStack.size() > 40)
        m_undoStack.erase(m_undoStack.begin());
    m_redoStack.clear();
    updateActionButtons();
}

void Sketch2DDialog::undoLast()
{
    if (m_undoStack.empty())
    {
        setStatus(tr("无可撤销。"), true);
        return;
    }

    m_redoStack.push_back({m_committedDocuments, m_document, m_referencePoints, m_editingCommittedIndex});
    if (m_redoStack.size() > 40)
        m_redoStack.erase(m_redoStack.begin());

    const SketchSnapshot snapshot = m_undoStack.back();
    m_undoStack.pop_back();
    m_committedDocuments = snapshot.committed;
    m_document = snapshot.active;
    m_referencePoints = snapshot.referencePoints;
    m_editingCommittedIndex = snapshot.editingCommittedIndex;
    resetProfileSegmentState();
    clearPendingPoint();
    refreshPointTable();
    refreshObjectTable();
    updatePreview();
    updateGeometryFromDocuments(tr("已撤销上一步。"), false);
    updateActionButtons();
    setStatus(tr("已撤销上一步。"), true);
}

void Sketch2DDialog::redoLast()
{
    if (m_redoStack.empty())
    {
        setStatus(tr("无可重做。"), true);
        return;
    }

    m_undoStack.push_back({m_committedDocuments, m_document, m_referencePoints, m_editingCommittedIndex});
    if (m_undoStack.size() > 40)
        m_undoStack.erase(m_undoStack.begin());

    const SketchSnapshot snapshot = m_redoStack.back();
    m_redoStack.pop_back();
    m_committedDocuments = snapshot.committed;
    m_document = snapshot.active;
    m_referencePoints = snapshot.referencePoints;
    m_editingCommittedIndex = snapshot.editingCommittedIndex;
    resetProfileSegmentState();
    clearPendingPoint();
    refreshPointTable();
    refreshObjectTable();
    updatePreview();
    updateGeometryFromDocuments(tr("已重做上一步。"), false);
    updateActionButtons();
    setStatus(tr("已重做上一步。"), true);
}

void Sketch2DDialog::updateActionButtons()
{
    if (ui->undoButton)
        ui->undoButton->setEnabled(!m_undoStack.empty());
    if (ui->redoButton)
        ui->redoButton->setEnabled(!m_redoStack.empty());

    const bool isProfile = m_document.kind() == SketchDocument::Kind::Profile;
    const bool isCurve = m_document.kind() == SketchDocument::Kind::Curve;
    const int activePointCount = static_cast<int>(m_document.points().size());
    const int tablePointCount = tableShowsReferencePoints()
                                ? static_cast<int>(m_referencePoints.size())
                                : activePointCount;
    const bool hasSelection = ui->pointsTable && ui->pointsTable->currentRow() >= 0;
    const int selectedObjectRow = selectedCommittedIndex();
    const bool hasObjectSelection = selectedObjectRow >= 0;
    const bool selectedProfile = hasObjectSelection &&
                                 documentForCommittedRow(selectedObjectRow).kind() == SketchDocument::Kind::Profile;
    const bool protectedSegmentProfile = isProfile && m_document.hasProfileSegments();

    const bool entityMode = outputAsEntity();
    const bool profileCommand = currentCommand() == Command::Polyline || isProfile;
    const bool curveCommand = currentCommand() == Command::Curve || isCurve;
    if (ui->closeButton)
    {
        ui->closeButton->setText(tr("闭合"));
        ui->closeButton->setVisible(entityMode && profileCommand);
        ui->closeButton->setEnabled(isProfile && activePointCount >= 3 && !m_document.isClosed());
    }
    if (ui->finishButton)
    {
        ui->finishButton->setText(tr("结束"));
        ui->finishButton->setVisible(curveCommand || profileCommand);
        ui->finishButton->setEnabled((isProfile && activePointCount >= 2 && !m_document.isFinished()) ||
                                (isCurve && activePointCount >= 3));
    }
    if (ui->deleteButton)
    {
        ui->deleteButton->setText(currentCommand() == Command::Select ? tr("删除对象") : tr("删除点"));
        const bool canDeletePoint = tablePointCount > 0 && hasSelection && !protectedSegmentProfile;
        ui->deleteButton->setEnabled(canDeletePoint || (currentCommand() == Command::Select && hasObjectSelection));
    }
    if (ui->reverseButton)
    {
        const bool reversibleArc = m_document.kind() == SketchDocument::Kind::Arc &&
                                   m_document.arcKind() == SketchDocument::ArcKind::Through;
        const bool canReverse = (isProfile || isCurve || reversibleArc) &&
                                activePointCount >= 2 && !protectedSegmentProfile;
        ui->reverseButton->setVisible(canReverse);
        ui->reverseButton->setEnabled(canReverse);
    }
    if (ui->mergeEndpointsButton)
        ui->mergeEndpointsButton->setEnabled(isProfile || selectedProfile);
    if (ui->checkClosureButton)
        ui->checkClosureButton->setEnabled(isProfile || selectedProfile);
    if (ui->clearButton)
        ui->clearButton->setEnabled(hasAnySketchData());
    if (ui->generateButton)
        ui->generateButton->setEnabled(hasVisibleGeometryDocuments());
    if (ui->editObjectButton)
        ui->editObjectButton->setEnabled(hasObjectSelection);
    if (ui->deleteObjectButton)
        ui->deleteObjectButton->setEnabled(hasObjectSelection);

    updatePointTableTitle();
    updateInfoPanel();
}

bool Sketch2DDialog::snap(double& u, double& v, int* snappedIndex, SketchSnapKind* snapKind) const
{
    if (snappedIndex)
        *snappedIndex = -1;
    if (snapKind)
        *snapKind = SketchSnapKind::None;

    if (ui->snapCheck && ui->snapCheck->isChecked())
    {
        const double threshold = pointSnapTolerance();
        double bestDistance = threshold;
        int bestActiveIndex = -1;
        Point2D bestPoint;
        bool found = false;

        auto tryPoint = [&](const Point2D& point, int activeIndex)
        {
            const double distance = std::hypot(point.u - u, point.v - v);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestActiveIndex = activeIndex;
                bestPoint = point;
                found = true;
            }
        };

        const auto& activePoints = m_document.displayPoints();
        for (std::size_t i = 0; i < activePoints.size(); ++i)
            tryPoint(activePoints[i], static_cast<int>(i));

        for (const auto& point : m_referencePoints)
            tryPoint(point, -1);

        for (const auto& document : m_committedDocuments)
        {
            for (const auto& point : document.displayPoints())
                tryPoint(point, -1);
        }

        if (found)
        {
            u = bestPoint.u;
            v = bestPoint.v;
            if (snappedIndex)
                *snappedIndex = bestActiveIndex;
            if (snapKind)
                *snapKind = SketchSnapKind::Point;
            return true;
        }
    }

    if (ui->gridSnapCheck && ui->gridSnapCheck->isChecked())
    {
        const double step = gridSnapStep();
        u = std::round(u / step) * step;
        v = std::round(v / step) * step;
        if (snapKind)
            *snapKind = SketchSnapKind::Grid;
        return true;
    }

    return false;
}

bool Sketch2DDialog::isNearFirstPoint(double u, double v) const
{
    if (!ui->autoCloseCheck || !ui->autoCloseCheck->isChecked())
        return false;
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 3)
        return false;

    const auto& first = m_document.points().front();
    return std::hypot(first.u - u, first.v - v) <= autoCloseTolerance();
}

void Sketch2DDialog::clearPendingPoint()
{
    m_hasPendingPoint = false;
    m_view.updateRubberLine(false, {}, {});
    m_view.updateConstructionGuide(false, {});
    m_view.updateTangentGuideLine(false, {}, {});
    m_view.updateSnapIndicator(false, {});
    resetCenterAngleTracking();
    updateInfoPanel();
}

void Sketch2DDialog::resetCenterAngleTracking()
{
    m_centerAngleTracking = false;
    m_centerAngleTrackStart = {};
    m_centerAngleTrackCenter = {};
    m_centerAngleLastAngle = 0.0;
    m_centerAngleSweep = 0.0;
    updateArcAngleDisplay(0.0, false);
}

double Sketch2DDialog::updateCenterAngleTracking(const Point2D& start, const Point2D& center, const Point2D& cursor)
{
    const double radius = std::hypot(start.u - center.u, start.v - center.v);
    const double cursorRadius = std::hypot(cursor.u - center.u, cursor.v - center.v);
    if (radius <= geometryTolerance() || cursorRadius <= geometryTolerance())
        return 0.0;

    const double rawAngle = std::atan2(cursor.v - center.v, cursor.u - center.u);
    const double startAngle = std::atan2(start.v - center.v, start.u - center.u);
    const double resetTolerance = std::max(geometryTolerance(), 1.0e-9);
    const bool sameArc =
        m_centerAngleTracking &&
        std::hypot(m_centerAngleTrackStart.u - start.u, m_centerAngleTrackStart.v - start.v) <= resetTolerance &&
        std::hypot(m_centerAngleTrackCenter.u - center.u, m_centerAngleTrackCenter.v - center.v) <= resetTolerance;

    if (!sameArc)
    {
        m_centerAngleTracking = true;
        m_centerAngleTrackStart = start;
        m_centerAngleTrackCenter = center;
        m_centerAngleLastAngle = rawAngle;
        m_centerAngleSweep = signedAngleSweep(startAngle, rawAngle);
        updateArcAngleDisplay(m_centerAngleSweep, true);
        return m_centerAngleSweep;
    }

    const double delta = signedAngleSweep(m_centerAngleLastAngle, rawAngle);
    m_centerAngleLastAngle = rawAngle;
    m_centerAngleSweep += delta;
    updateArcAngleDisplay(m_centerAngleSweep, true);
    return m_centerAngleSweep;
}

void Sketch2DDialog::finishCommand(const QString& message)
{
    clearPendingPoint();
    m_document.setFinished(true);
    refreshPointTable();
    updatePreview();
    setStatus(message, true);
}

std::vector<SketchDocument> Sketch2DDialog::visibleDocuments() const
{
    std::vector<SketchDocument> documents;
    documents.reserve(m_committedDocuments.size() + 1);

    for (int i = 0; i < static_cast<int>(m_committedDocuments.size()); ++i)
    {
        if (i == m_editingCommittedIndex)
            continue;
        documents.push_back(m_committedDocuments[static_cast<std::size_t>(i)]);
    }

    if (m_document.isRenderable())
        documents.push_back(m_document);
    return documents;
}

std::vector<Point2D> Sketch2DDialog::visiblePoints() const
{
    std::vector<Point2D> points;

    const double mergeTolerance = std::max(geometryTolerance(), 1.0e-9);
    const auto appendUnique = [&points, mergeTolerance](const Point2D& point) {
        const auto duplicate = std::find_if(points.begin(), points.end(), [&](const Point2D& existing) {
            return std::hypot(existing.u - point.u, existing.v - point.v) <= mergeTolerance;
        });
        if (duplicate == points.end())
            points.push_back(point);
    };

    // 已提交对象的控制点只在单选后显示，否则大型 DXF
    // 会同时画出数百个点，无法判断轮廓。正在绘制的对象仍显示控制点。
    for (const Point2D& point : m_document.displayPoints())
        appendUnique(point);
    for (const Point2D& point : m_referencePoints)
        appendUnique(point);

    return points;
}

bool Sketch2DDialog::commitActiveDocument(const QString& message)
{
    if (!m_document.isRenderable())
    {
        setStatus(tr("几何不足。"), false);
        return false;
    }

    if (m_document.kind() == SketchDocument::Kind::Curve || m_document.kind() == SketchDocument::Kind::Arc)
        m_document.setFinished(true);

    int rowToSelect = -1;
    if (isEditingCommittedObject())
    {
        rowToSelect = m_editingCommittedIndex;
        m_committedDocuments[static_cast<std::size_t>(m_editingCommittedIndex)] = m_document;
        m_editingCommittedIndex = -1;
    }
    else
    {
        rowToSelect = static_cast<int>(m_committedDocuments.size());
        m_committedDocuments.push_back(m_document);
    }

    m_document.clear();
    m_editingCommittedIndex = -1;
    resetProfileSegmentState();
    clearPendingPoint();
    refreshPointTable();
    refreshObjectTable();

    // 完成一个对象后回到选择模式，并保留新对象为当前选择。
    // 这样用户可以立即看到控制点并继续编辑，不会还停留在上一绘图命令里。
    setCommand(Command::Select);
    selectObjectRow(rowToSelect);
    updatePreview();
    updateGeometryFromDocuments(message, false);
    setStatus(message, true);
    return true;
}

void Sketch2DDialog::updateGeometryFromDocuments(const QString& message, bool showWarning)
{
    auto documents = visibleDocuments();
    documents.erase(std::remove_if(documents.begin(), documents.end(),
                                   [](const SketchDocument& document)
                                   { return document.isConstruction(); }),
                    documents.end());
    // 正在绘制的平滑曲线只放到橙色 rubber 层显示，不能再进入正式蓝色预览层，
    // 否则会同时出现蓝线和黄线，用户会误以为曲线乱跳。
    if (currentCommand() == Command::Curve &&
        m_document.kind() == SketchDocument::Kind::Curve &&
        !m_document.isFinished() &&
        m_document.isRenderable() &&
        !documents.empty())
    {
        documents.pop_back();
    }

    if (documents.empty())
    {
        cleanupGeneratedGeometry();
        if (showWarning)
            QMessageBox::warning(this, tr("无法更新草图"), tr("没有可生成的线、面或圆。"));
        if (!message.isEmpty())
            setStatus(message, true);
        return;
    }

    const auto result = SketchShapeBuilder::buildMany(documents, planeSettings());
    if (!result.ok || result.shape.IsNull())
    {
        if (showWarning)
            QMessageBox::warning(this, tr("无法更新草图"), result.message);
        return;
    }

    auto* manager = GeomManager::GeomManagerInstance::getInstance();
    if (!manager)
        return;

    const bool visible = GeometryDisplayManager::isVisible(m_uuid);
    GeometryCleanup::removeExistingGeometry(m_uuid);

    auto* shapePtr = new TopoDS_Shape(result.shape);
    auto* object = new GeomShapeObject(m_uuid, shapePtr, getDisplayName());
    manager->appendGeomShapeObject(object);
    GeometryDisplayManager::registerObject(m_uuid, object, false, false);
    GeometryDisplayManager::setVisible(m_uuid, visible, false);

    // 新建空草图在第一次真正生成几何后，模型树应立即变为“已显示”。
    // DXF 导入也走这里，因此不会再出现“树未勾选，但蓝色草图已经显示”的状态。
    if (m_isCurrentPage && !m_hadRenderableDataOnEntry && !m_visibilityExplicitlyChanged)
    {
        if (auto* pre = ServiceRegistry::instance().getService<PreProcessingService>())
        {
            if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
                workbench->setNodeVisible(m_uuid, true);
        }
        m_hadRenderableDataOnEntry = true;
        m_editorPreviewVisible = true;
    }

    if (m_isCurrentPage && m_editorPreviewVisible)
        hideCommittedPresentationForEditing();

    // 草图模式相机由 alignCameraToPlane() 统一管理，几何体注册不应触发 ResetCamera。
    // 此处显式重绘网格以保证新增几何体后网格与当前视口一致。
    m_view.redrawGrid();

    if (!message.isEmpty())
        setStatus(message, true);
}

int Sketch2DDialog::findNearestPointInDocument(const SketchDocument& document, double u, double v) const
{
    const auto& points = document.points();
    if (points.empty())
        return -1;

    const double threshold = std::max(pointSnapTolerance() * 1.5, precisionStep() * 0.08);
    double best = threshold;
    int bestIndex = -1;
    for (int i = 0; i < static_cast<int>(points.size()); ++i)
    {
        const double d = std::hypot(points[static_cast<std::size_t>(i)].u - u,
                                    points[static_cast<std::size_t>(i)].v - v);
        if (d < best)
        {
            best = d;
            bestIndex = i;
        }
    }
    return bestIndex;
}

bool Sketch2DDialog::ensureCommittedObjectInEditMode(int row)
{
    if (row < 0 || row >= static_cast<int>(m_committedDocuments.size()))
        return false;

    if (m_editingCommittedIndex == row && m_document.isRenderable())
        return true;

    if (m_document.isRenderable())
    {
        if (isEditingCommittedObject())
            m_committedDocuments[static_cast<std::size_t>(m_editingCommittedIndex)] = m_document;
        else
            m_committedDocuments.push_back(m_document);
    }

    m_document = m_committedDocuments[static_cast<std::size_t>(row)];
    m_editingCommittedIndex = row;
    if (ui->arcModeCombo && m_document.kind() == SketchDocument::Kind::Arc)
    {
        const QSignalBlocker blocker(ui->arcModeCombo);
        const int mode = m_document.arcKind() == SketchDocument::ArcKind::CenterAngle
                             ? static_cast<int>(StandaloneArcMode::CenterAngle)
                             : static_cast<int>(StandaloneArcMode::ThreePoint);
        const int index = ui->arcModeCombo->findData(mode);
        if (index >= 0)
            ui->arcModeCombo->setCurrentIndex(index);
    }
    clearPendingPoint();
    refreshPointTable();
    refreshObjectTable();
    selectObjectRow(row);
    return true;
}

void Sketch2DDialog::updatePointInActiveDocument(int pointIndex, double u, double v)
{
    if (pointIndex < 0 || pointIndex >= static_cast<int>(m_document.points().size()))
        return;

    std::vector<Point2D> points = m_document.points();
    points[static_cast<std::size_t>(pointIndex)] = {u, v};

    switch (m_document.kind())
    {
    case SketchDocument::Kind::Circle:
        m_document.setCircle(points.front().u, points.front().v, m_document.circleRadius());
        break;
    case SketchDocument::Kind::Arc:
        m_document.setArc(points, m_document.isFinished(), m_document.arcKind());
        break;
    case SketchDocument::Kind::Curve:
        m_document.setCurve(points, m_document.isFinished());
        break;
    case SketchDocument::Kind::Profile:
        m_document.setProfile(points, m_document.isClosed(), m_document.isFinished());
        break;
    default:
        break;
    }
}


void Sketch2DDialog::showSketchContextMenu()
{
    QMenu menu(this);
    const Command activeCommand = currentCommand();
    const bool drawing = activeCommand != Command::Select &&
                         (hasActivePoints() || m_hasPendingPoint || m_profileArcMidPending);
    const QList<int> selectedRows = selectedCommittedIndices();

    if (drawing)
    {
        QAction* finishAction = nullptr;
        QAction* closeAction = nullptr;
        QAction* lineSegmentAction = nullptr;
        QAction* threePointSegmentAction = nullptr;
        QAction* centerAngleSegmentAction = nullptr;
        QAction* tangentSegmentAction = nullptr;

        if (m_document.kind() == SketchDocument::Kind::Profile)
        {
            finishAction = menu.addAction(tr("完成开放轮廓"));
            finishAction->setEnabled(m_document.points().size() >= 2U);
            closeAction = menu.addAction(tr("闭合轮廓"));
            closeAction->setEnabled(m_document.points().size() >= 3U);
            menu.addSeparator();

            lineSegmentAction = menu.addAction(tr("下一段：直线"));
            QMenu* arcSegmentMenu = menu.addMenu(tr("下一段：圆弧"));
            threePointSegmentAction = arcSegmentMenu->addAction(tr("三点圆弧"));
            centerAngleSegmentAction = arcSegmentMenu->addAction(tr("中心角圆弧"));
            tangentSegmentAction = arcSegmentMenu->addAction(tr("切线圆弧"));
            lineSegmentAction->setCheckable(true);
            threePointSegmentAction->setCheckable(true);
            centerAngleSegmentAction->setCheckable(true);
            tangentSegmentAction->setCheckable(true);
            lineSegmentAction->setChecked(m_profileSegmentMode == ProfileSegmentMode::Line);
            threePointSegmentAction->setChecked(m_profileSegmentMode == ProfileSegmentMode::ArcThreePoint);
            centerAngleSegmentAction->setChecked(m_profileSegmentMode == ProfileSegmentMode::ArcCenterAngle);
            tangentSegmentAction->setChecked(m_profileSegmentMode == ProfileSegmentMode::ArcTangentEnd);
            menu.addSeparator();
        }
        else if (m_document.kind() == SketchDocument::Kind::Curve)
        {
            finishAction = menu.addAction(tr("完成样条"));
            finishAction->setEnabled(m_document.points().size() >= 3U);
            menu.addSeparator();
        }

        QAction* undoPointAction = menu.addAction(tr("撤销上一点"));
        undoPointAction->setEnabled(!m_undoStack.empty() && hasActivePoints());
        QAction* restartAction = menu.addAction(tr("重新开始当前图形"));
        QAction* cancelCommandAction = menu.addAction(tr("取消当前命令"));

        QAction* picked = menu.exec(QCursor::pos());
        if (finishAction && picked == finishAction)
            finishOpenProfile();
        else if (closeAction && picked == closeAction)
            closeProfile();
        else if (lineSegmentAction && picked == lineSegmentAction)
            setProfileSegmentMode(ProfileSegmentMode::Line);
        else if (threePointSegmentAction && picked == threePointSegmentAction)
            setProfileSegmentMode(ProfileSegmentMode::ArcThreePoint);
        else if (centerAngleSegmentAction && picked == centerAngleSegmentAction)
            setProfileSegmentMode(ProfileSegmentMode::ArcCenterAngle);
        else if (tangentSegmentAction && picked == tangentSegmentAction)
            setProfileSegmentMode(ProfileSegmentMode::ArcTangentEnd);
        else if (picked == undoPointAction)
            undoLast();
        else if (picked == restartAction)
        {
            clearPendingPoint();
            m_document.clear();
            m_editingCommittedIndex = -1;
            resetProfileSegmentState();
            refreshPointTable();
            updatePreview();
            setCommand(activeCommand);
            setStatus(tr("已重新开始当前图形。"), true);
        }
        else if (picked == cancelCommandAction)
        {
            cancelCurrentCommand();
        }
        return;
    }

    if (!selectedRows.isEmpty())
    {
        QAction* editPointAction = nullptr;
        if (m_selectedPointObjectIndex >= 0 && m_selectedPointIndex >= 0)
            editPointAction = menu.addAction(tr("编辑点坐标"));

        QAction* editAction = menu.addAction(tr("编辑控制点"));
        editAction->setEnabled(selectedRows.size() == 1);

        QAction* continueAction = nullptr;
        QAction* closeAction = nullptr;
        if (selectedRows.size() == 1)
        {
            const SketchDocument& selected = documentForCommittedRow(selectedRows.front());
            if ((selected.kind() == SketchDocument::Kind::Profile && !selected.isClosed()) ||
                selected.kind() == SketchDocument::Kind::Curve)
            {
                continueAction = menu.addAction(selected.kind() == SketchDocument::Kind::Curve
                                                    ? tr("继续添加样条点")
                                                    : tr("继续绘制轮廓"));
            }
            if (selected.kind() == SketchDocument::Kind::Profile && !selected.isClosed() &&
                selected.points().size() >= 3U)
            {
                closeAction = menu.addAction(tr("闭合轮廓"));
            }
        }

        menu.addSeparator();
        bool allConstruction = true;
        for (int row : selectedRows)
        {
            if (row < 0 || row >= static_cast<int>(m_committedDocuments.size()) ||
                !m_committedDocuments[static_cast<std::size_t>(row)].isConstruction())
            {
                allConstruction = false;
                break;
            }
        }
        QAction* constructionAction = menu.addAction(allConstruction ? tr("转为普通线") : tr("设为构造线"));
        QAction* copyAction = menu.addAction(tr("复制"));
        QAction* deleteAction = menu.addAction(tr("删除"));
        QAction* clearSelectionAction = menu.addAction(tr("取消选择"));

        QAction* picked = menu.exec(QCursor::pos());
        if (editPointAction && picked == editPointAction)
        {
            if (ui->pointCoordinateGroup)
                ui->pointCoordinateGroup->setVisible(true);
            if (ui->pointUSpin)
            {
                ui->pointUSpin->setFocus();
                ui->pointUSpin->selectAll();
            }
        }
        else if (picked == editAction)
        {
            editSelectedObject();
        }
        else if (continueAction && picked == continueAction)
        {
            const int row = selectedRows.front();
            if (ensureCommittedObjectInEditMode(row))
            {
                m_document.setFinished(false);
                if (m_document.kind() == SketchDocument::Kind::Curve)
                {
                    setCommand(Command::Curve);
                    setStatus(tr("继续添加样条控制点。"), true);
                }
                else
                {
                    m_document.setClosed(false);
                    setCommand(Command::Polyline);
                    setProfileSegmentMode(ProfileSegmentMode::Line);
                    setStatus(tr("继续绘制轮廓。"), true);
                }
                updatePreview();
            }
        }
        else if (closeAction && picked == closeAction)
        {
            const int row = selectedRows.front();
            if (ensureCommittedObjectInEditMode(row))
                closeProfile();
        }
        else if (picked == constructionAction)
            toggleSelectedConstruction();
        else if (picked == copyAction)
            copySelectedObjects();
        else if (picked == deleteAction)
            deleteSelectedObject();
        else if (picked == clearSelectionAction)
            clearSketchSelection();
        return;
    }

    QAction* selectAction = menu.addAction(tr("选择 / 框选"));
    QAction* profileAction = menu.addAction(tr("折线轮廓"));
    QAction* rectAction = menu.addAction(tr("矩形"));
    QAction* circleAction = menu.addAction(tr("圆"));
    QMenu* arcMenu = menu.addMenu(tr("圆弧"));
    QAction* arcThreeAction = arcMenu->addAction(tr("三点圆弧"));
    QAction* arcCenterAction = arcMenu->addAction(tr("中心角圆弧"));
    QAction* curveAction = menu.addAction(tr("平滑样条"));
    QAction* pointAction = menu.addAction(tr("辅助点"));

    for (QAction* action : {selectAction, profileAction, rectAction, circleAction,
                            arcThreeAction, arcCenterAction, curveAction, pointAction})
        action->setCheckable(true);
    selectAction->setChecked(activeCommand == Command::Select);
    profileAction->setChecked(activeCommand == Command::Polyline);
    rectAction->setChecked(activeCommand == Command::Rectangle);
    circleAction->setChecked(activeCommand == Command::Circle);
    arcThreeAction->setChecked(activeCommand == Command::Arc &&
                               currentArcKind() == SketchDocument::ArcKind::Through);
    arcCenterAction->setChecked(activeCommand == Command::Arc &&
                                currentArcKind() == SketchDocument::ArcKind::CenterAngle);
    curveAction->setChecked(activeCommand == Command::Curve);
    pointAction->setChecked(activeCommand == Command::Point);

    QAction* picked = menu.exec(QCursor::pos());
    if (picked == selectAction)
    {
        setCommand(Command::Select);
    }
    else if (picked == profileAction)
    {
        setCommand(Command::Polyline);
        setProfileSegmentMode(ProfileSegmentMode::Line);
    }
    else if (picked == rectAction)
    {
        setCommand(Command::Rectangle);
    }
    else if (picked == circleAction)
    {
        setCommand(Command::Circle);
    }
    else if (picked == arcThreeAction || picked == arcCenterAction)
    {
        if (ui->arcModeCombo)
        {
            const int mode = picked == arcCenterAction
                                 ? static_cast<int>(StandaloneArcMode::CenterAngle)
                                 : static_cast<int>(StandaloneArcMode::ThreePoint);
            const int index = ui->arcModeCombo->findData(mode);
            if (index >= 0)
                ui->arcModeCombo->setCurrentIndex(index);
        }
        setCommand(Command::Arc);
    }
    else if (picked == curveAction)
    {
        setCommand(Command::Curve);
    }
    else if (picked == pointAction)
    {
        setCommand(Command::Point);
    }
}

void Sketch2DDialog::setProfileSegmentMode(ProfileSegmentMode mode)
{
    if (m_profileSegmentMode != mode)
        m_profileArcMidPending = false;
    m_profileSegmentMode = mode;

    QString modeName;
    switch (mode)
    {
    case ProfileSegmentMode::ArcThreePoint:
        modeName = tr("三点弧");
        setStatus(modeName, true);
        break;
    case ProfileSegmentMode::ArcCenterAngle:
        modeName = tr("中心角弧");
        setStatus(modeName, true);
        break;
    case ProfileSegmentMode::ArcTangentEnd:
        modeName = tr("切弧");
        setStatus(modeName, true);
        break;
    case ProfileSegmentMode::Line:
    default:
        modeName = tr("直线");
        setStatus(tr("直线。"), true);
        break;
    }
    updateInfoPanel();
}

void Sketch2DDialog::resetProfileSegmentState()
{
    m_profileSegmentMode = ProfileSegmentMode::Line;
    m_profileArcMidPending = false;
    resetCenterAngleTracking();
}

bool Sketch2DDialog::appendSampledProfileSegment(const std::vector<Point2D>& sampled,
                                                  const std::vector<Point2D>& displayPointsToAppend,
                                                  const QString& message)
{
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().empty())
        return false;
    if (sampled.size() < 2)
        return false;

    pushUndo();
    std::vector<Point2D> geometryPoints = m_document.points();
    for (std::size_t i = 1; i < sampled.size(); ++i)
    {
        const auto& pnt = sampled[i];
        if (geometryPoints.empty() || std::hypot(geometryPoints.back().u - pnt.u, geometryPoints.back().v - pnt.v) > geometryTolerance())
            geometryPoints.push_back(pnt);
    }

    std::vector<Point2D> displayPoints = m_document.displayPoints();
    if (displayPoints.empty())
        displayPoints = m_document.points();
    for (const auto& pnt : displayPointsToAppend)
    {
        if (displayPoints.empty() || std::hypot(displayPoints.back().u - pnt.u, displayPoints.back().v - pnt.v) > geometryTolerance())
            displayPoints.push_back(pnt);
    }

    m_document.setProfileWithDisplayPoints(geometryPoints, displayPoints, false, false);
    m_profileArcMidPending = false;
    refreshPointTable();
    if (ui->pointsTable && !m_document.displayPoints().empty())
        ui->pointsTable->selectRow(static_cast<int>(m_document.displayPoints().size()) - 1);
    updatePreview();
    setStatus(message, true);
    return true;
}

bool Sketch2DDialog::appendTangentArcSegment(const Point2D& tangentPoint, const Point2D& end)
{
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().empty())
    {
        setStatus(tr("切线圆弧需要先有轮廓起点。"), false);
        return false;
    }

    const Point2D startPoint = m_document.points().back();
    Point2D finalEnd = end;
    const bool closesProfile = endpointClosesActiveProfile(end);
    if (closesProfile)
        finalEnd = m_document.points().front();

    if (std::hypot(startPoint.u - tangentPoint.u, startPoint.v - tangentPoint.v) <= geometryTolerance())
    {
        setStatus(tr("切线点太近。"), false);
        return false;
    }
    if (std::hypot(startPoint.u - finalEnd.u, startPoint.v - finalEnd.v) <= geometryTolerance())
    {
        setStatus(tr("终点太近。"), false);
        return false;
    }

    const auto guide = tangentArcGuide(startPoint, tangentPoint, finalEnd);
    if (guide.size() < 3)
    {
        setStatus(tr("切弧失败：方向与终点近共线。"), false);
        return false;
    }

    pushUndo();
    m_document.appendProfileArcTangent(tangentPoint, finalEnd);
    m_profileArcMidPending = false;

    if (closesProfile)
        return commitClosedActiveProfile();

    refreshPointTable();
    if (ui->pointsTable && !m_document.displayPoints().empty())
        ui->pointsTable->selectRow(static_cast<int>(m_document.displayPoints().size()) - 1);
    updatePreview();
    setStatus(tr("切弧"), true);
    return true;
}

bool Sketch2DDialog::endpointClosesActiveProfile(const Point2D& end) const
{
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 3)
        return false;

    const auto& first = m_document.points().front();
    return std::hypot(first.u - end.u, first.v - end.v) <= autoCloseTolerance();
}

bool Sketch2DDialog::commitClosedActiveProfile()
{
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 3)
        return false;

    resetProfileSegmentState();
    m_document.setClosed(true);
    return commitActiveDocument(outputAsEntity()
                                    ? tr("实体已生成。")
                                    : tr("闭合曲线已生成。"));
}


void Sketch2DDialog::handleSketchClick(double u, double v, bool rightButton)
{
    if (rightButton)
    {
        // 选择模式下，右键落在对象或端点上时先把它设为当前选择，
        // 再生成上下文菜单。这样无需先左键选择一次。
        if (currentCommand() == Command::Select)
        {
            int objectIndex = -1;
            int pointIndex = -1;
            Point2D point;
            if (findNearestEndpoint(u, v, objectIndex, pointIndex, point))
            {
                selectEndpoint(objectIndex, pointIndex, point);
            }
            else
            {
                const int row = findNearestCommittedObject(u, v);
                if (row >= 0)
                {
                    clearEndpointSelection();
                    selectObjectRow(row);
                }
            }
        }
        showSketchContextMenu();
        return;
    }

    const Command command = currentCommand();
    if (command == Command::Select)
    {
        int objectIndex = -1;
        int pointIndex = -1;
        Point2D point;
        if (findNearestEndpoint(u, v, objectIndex, pointIndex, point))
        {
            selectEndpoint(objectIndex, pointIndex, point);
            setStatus(tr("已选择端点。"), true);
            return;
        }

        const int row = findNearestCommittedObject(u, v);
        if (row >= 0)
        {
            clearEndpointSelection();
            selectObjectRow(row);
            setStatus(tr("已选择：%1").arg(documentTypeText(documentForCommittedRow(row))), true);
        }
        else
        {
            clearSketchSelection();
            setStatus(tr("未选中"), true);
        }
        return;
    }

    int snappedIndex = -1;
    const bool snapped = snap(u, v, &snappedIndex);
    Q_UNUSED(snapped);

    if (snappedIndex < 0)
    {
        if (((command == Command::Polyline && m_profileSegmentMode == ProfileSegmentMode::Line) || command == Command::Arc) && !m_document.points().empty())
            applyOrthoFrom(m_document.points().back(), u, v);
        else if ((command == Command::Rectangle || command == Command::Circle) && m_hasPendingPoint)
            applyOrthoFrom(m_pendingPoint, u, v);
    }
    if (command == Command::Polyline)
    {
        bool startedNewProfile = false;
        bool undoAlreadyPushed = false;
        if (m_document.kind() != SketchDocument::Kind::Profile)
        {
            if (!m_document.points().empty())
            {
                pushUndo();
                undoAlreadyPushed = true;
            }
            m_document.clear();
            startedNewProfile = true;
        }

        if (m_document.points().empty())
        {
            pushUndo();
            m_document.appendProfilePoint(u, v);
            appendPointToTable(u, v);
            updatePreview();
            switch (m_profileSegmentMode)
            {
            case ProfileSegmentMode::ArcThreePoint:
                setStatus(tr("三点弧：经过点"), true);
                break;
            case ProfileSegmentMode::ArcCenterAngle:
                setStatus(tr("中心角弧：中心"), true);
                break;
            case ProfileSegmentMode::ArcTangentEnd:
                setStatus(tr("切弧：切线"), true);
                break;
            case ProfileSegmentMode::Line:
            default:
                setStatus(tr("P1"), true);
                break;
            }
            return;
        }

        if (m_profileSegmentMode == ProfileSegmentMode::ArcThreePoint)
        {
            const auto& startPoint = m_document.points().back();
            if (!m_profileArcMidPending)
            {
                if (std::hypot(startPoint.u - u, startPoint.v - v) <= geometryTolerance())
                    return;
                m_profileArcMid = {u, v};
                m_profileArcMidPending = true;
                resetCenterAngleTracking();
                setStatus(tr("三点弧：终点"), true);
                updateRubberLine(u, v);
                return;
            }

            Point2D endPoint{u, v};
            const bool closesProfile = endpointClosesActiveProfile(endPoint);
            if (closesProfile)
                endPoint = m_document.points().front();
            if (isDegenerateArc(startPoint, m_profileArcMid, endPoint))
            {
                setStatus(tr("三点圆弧无效：起点、中点、终点过近或共线。"), false);
                return;
            }
            pushUndo();
            m_document.appendProfileArcThrough(m_profileArcMid, endPoint);
            m_profileArcMidPending = false;
            if (closesProfile)
            {
                commitClosedActiveProfile();
                return;
            }
            refreshPointTable();
            if (ui->pointsTable && !m_document.displayPoints().empty())
                ui->pointsTable->selectRow(static_cast<int>(m_document.displayPoints().size()) - 1);
            updatePreview();
            setStatus(tr("三点弧"), true);
            return;
        }

        if (m_profileSegmentMode == ProfileSegmentMode::ArcCenterAngle)
        {
            const auto& startPoint = m_document.points().back();
            if (!m_profileArcMidPending)
            {
                if (std::hypot(startPoint.u - u, startPoint.v - v) <= geometryTolerance())
                {
                    setStatus(tr("中心过近。"), false);
                    return;
                }
                m_profileArcMid = {u, v};
                m_profileArcMidPending = true;
                resetCenterAngleTracking();
                updateArcAngleDisplay(0.0, true);
                setStatus(tr("中心角弧：角度"), true);
                updateRubberLine(u, v);
                return;
            }

            Point2D midPoint;
            Point2D endPoint;
            const double arcSweep = updateCenterAngleTracking(startPoint, m_profileArcMid, {u, v});
            if (!centerAngleArcThroughPoints(startPoint, m_profileArcMid, {u, v}, midPoint, endPoint, arcSweep))
            {
                setStatus(tr("中心角弧无效。"), false);
                return;
            }

            const bool closesProfile = endpointClosesActiveProfile(endPoint);
            if (closesProfile)
                endPoint = m_document.points().front();

            if (isDegenerateArc(startPoint, midPoint, endPoint))
            {
                setStatus(tr("中心角弧无效。"), false);
                return;
            }

            pushUndo();
            m_document.appendProfileArcThrough(midPoint, endPoint);
            m_profileArcMidPending = false;
            resetCenterAngleTracking();
            if (closesProfile)
            {
                commitClosedActiveProfile();
                return;
            }
            refreshPointTable();
            if (ui->pointsTable && !m_document.displayPoints().empty())
                ui->pointsTable->selectRow(static_cast<int>(m_document.displayPoints().size()) - 1);
            updatePreview();
            setStatus(tr("中心角弧"), true);
            return;
        }

        if (m_profileSegmentMode == ProfileSegmentMode::ArcTangentEnd)
        {
            if (!m_profileArcMidPending)
            {
                const Point2D startPoint = m_document.points().back();
                if (std::hypot(startPoint.u - u, startPoint.v - v) <= geometryTolerance())
                {
                    setStatus(tr("切线点太近。"), false);
                    return;
                }
                m_profileArcMid = {u, v};
                m_profileArcMidPending = true;
                resetCenterAngleTracking();
                updateRubberLine(u, v);
                setStatus(tr("切弧：终点"), true);
                return;
            }

            appendTangentArcSegment(m_profileArcMid, {u, v});
            return;
        }

        if (isNearFirstPoint(u, v) || snappedIndex == 0)
        {
            resetProfileSegmentState();
            closeProfile();
            return;
        }

        if (!m_document.points().empty())
        {
            const auto& last = m_document.points().back();
            if (std::hypot(last.u - u, last.v - v) <= geometryTolerance())
                return;
        }

        if (snappedIndex >= 0 && snappedIndex == static_cast<int>(m_document.points().size()) - 1)
            return;

        if (!startedNewProfile)
            pushUndo();
        else if (m_document.points().empty() && !undoAlreadyPushed)
            pushUndo();
        m_document.appendProfilePoint(u, v);
        appendPointToTable(u, v);
        updatePreview();
        setStatus(tr("P%1").arg(static_cast<int>(m_document.displayPoints().size())), true);
        return;
    }

    if (command == Command::Rectangle)
    {
        if (!m_hasPendingPoint)
        {
            finishActiveDocumentBeforeNewObject();
            m_pendingPoint = {u, v};
            m_hasPendingPoint = true;
            setStatus(tr("矩形：对角点"), true);
            updateRubberLine(u, v);
            return;
        }

        const Point2D opposite{u, v};
        if (isDegenerateRectangle(m_pendingPoint, opposite))
        {
            setStatus(tr("矩形过小。"), false);
            return;
        }

        pushUndo();
        m_document.setRectangle(m_pendingPoint.u, m_pendingPoint.v, opposite.u, opposite.v);
        clearPendingPoint();
        commitActiveDocument(outputAsEntity()
                                 ? tr("矩形实体已生成。")
                                 : tr("矩形曲线已生成。"));
        return;
    }

    if (command == Command::Circle)
    {
        if (!m_hasPendingPoint)
        {
            finishActiveDocumentBeforeNewObject();
            m_pendingPoint = {u, v};
            m_hasPendingPoint = true;
            setStatus(tr("圆：半径"), true);
            return;
        }

        const double radius = std::hypot(u - m_pendingPoint.u, v - m_pendingPoint.v);
        if (isDegenerateRadius(radius))
        {
            setStatus(tr("半径过小。"), false);
            return;
        }

        pushUndo();
        m_document.setCircle(m_pendingPoint.u, m_pendingPoint.v, radius);
        clearPendingPoint();
        commitActiveDocument(outputAsEntity()
                                 ? tr("圆实体已生成，R=%1。").arg(radius, 0, 'f', 3)
                                 : tr("圆曲线已生成，R=%1。").arg(radius, 0, 'f', 3));
        return;
    }

    if (command == Command::Arc)
    {
        if (m_document.kind() != SketchDocument::Kind::Arc)
        {
            finishActiveDocumentBeforeNewObject();
            m_document.clear();
        }
        const SketchDocument::ArcKind arcKind = m_document.points().empty() ? currentArcKind() : m_document.arcKind();
        if (m_document.points().empty())
            m_document.setArcKind(arcKind);

        double centerAngleSweep = 0.0;
        Point2D committedPoint{u, v};
        if (arcKind == SketchDocument::ArcKind::CenterAngle && m_document.points().size() == 2)
        {
            const auto& points = m_document.points();
            centerAngleSweep = updateCenterAngleTracking(points[0], points[1], {u, v});
            committedPoint = centerAngleEndpointForSweep(points[0], points[1], clampArcSweep(centerAngleSweep));
        }

        if (!m_document.points().empty())
        {
            const auto& last = m_document.points().back();
            if (std::hypot(last.u - committedPoint.u, last.v - committedPoint.v) <= geometryTolerance())
                return;
        }

        pushUndo();
        m_document.appendArcPoint(committedPoint.u, committedPoint.v, arcKind);
        if (arcKind == SketchDocument::ArcKind::CenterAngle && m_document.points().size() == 3)
            m_document.setArcSweep(clampArcSweep(centerAngleSweep));
        refreshPointTable();
        if (ui->pointsTable)
            ui->pointsTable->selectRow(static_cast<int>(m_document.points().size()) - 1);
        updatePreview();

        const int count = static_cast<int>(m_document.points().size());
        if (count < 3)
        {
            if (arcKind == SketchDocument::ArcKind::CenterAngle && count == 2)
            {
                resetCenterAngleTracking();
                updateArcAngleDisplay(0.0, true);
            }
            if (arcKind == SketchDocument::ArcKind::CenterAngle)
                setStatus(count == 1 ? tr("圆弧：中心") : tr("圆弧：角度"), true);
            else
                setStatus(count == 1 ? tr("圆弧：经过点") : tr("圆弧：终点"), true);
            return;
        }

        const auto& p = m_document.points();
        const bool degenerate = arcKind == SketchDocument::ArcKind::CenterAngle
                                    ? isDegenerateCenterAngleArc(p[0], p[1], p[2], m_document.arcSweep())
                                    : isDegenerateArc(p[0], p[1], p[2]);
        if (degenerate)
        {
            auto& points = m_document.editablePoints();
            if (!points.empty())
                points.pop_back();
            m_document.setFinished(false);
            m_document.setArcSweep(0.0);
            refreshPointTable();
            updatePreview();
            setStatus(arcKind == SketchDocument::ArcKind::CenterAngle
                          ? tr("圆弧半径或角度过小。")
                          : tr("圆弧 3 个点过近或共线。"),
                      false);
            return;
        }

        m_document.setFinished(true);
        resetCenterAngleTracking();
        commitActiveDocument(tr("圆弧已生成。"));
        return;
    }

    if (command == Command::Curve)
    {
        if (m_document.kind() != SketchDocument::Kind::Curve)
        {
            finishActiveDocumentBeforeNewObject();
            m_document.clear();
        }

        if (!m_document.points().empty())
        {
            const auto& last = m_document.points().back();
            if (std::hypot(last.u - u, last.v - v) <= geometryTolerance())
                return;
        }

        pushUndo();
        m_document.appendCurvePoint(u, v);
        refreshPointTable();
        if (ui->pointsTable)
            ui->pointsTable->selectRow(static_cast<int>(m_document.points().size()) - 1);
        updatePreview();

        const int count = static_cast<int>(m_document.points().size());
        setStatus(tr("样条：%1 点").arg(count), true);
        return;
    }

    if (command == Command::Point)
    {
        pushUndo();
        m_referencePoints.push_back({u, v});
        refreshPointTable();
        if (ui->pointsTable)
            ui->pointsTable->selectRow(static_cast<int>(m_referencePoints.size()) - 1);
        updatePreview();
        setStatus(tr("辅助点 %1").arg(static_cast<int>(m_referencePoints.size())), true);
        return;
    }
}

void Sketch2DDialog::handleSketchMove(double u, double v)
{
    const Command command = currentCommand();
    if (command == Command::Select)
    {
        updateCursorInfo(u, v, SketchSnapKind::None, false, false);
        m_view.updateCursorText(std::numeric_limits<double>::quiet_NaN(),
                                std::numeric_limits<double>::quiet_NaN(), false);
        int objectIndex = -1;
        int pointIndex = -1;
        Point2D point;
        const bool hoverPoint = findNearestEndpoint(u, v, objectIndex, pointIndex, point);
        m_view.updateHoverPointText(hoverPoint, point);
        m_view.updateRubberShape(false, {}, false);
        m_view.updateSnapIndicator(hoverPoint, point);
        return;
    }

    int snappedIndex = -1;
    SketchSnapKind snapKind = SketchSnapKind::None;
    const bool snapped = snap(u, v, &snappedIndex, &snapKind);

    bool orthoApplied = false;
    if (snappedIndex < 0)
    {
        if (((command == Command::Polyline && m_profileSegmentMode == ProfileSegmentMode::Line) || command == Command::Arc) && !m_document.points().empty())
            orthoApplied = applyOrthoFrom(m_document.points().back(), u, v);
        else if ((command == Command::Rectangle || command == Command::Circle) && m_hasPendingPoint)
            orthoApplied = applyOrthoFrom(m_pendingPoint, u, v);
    }

    const bool nearFirstPoint = command == Command::Polyline && isNearFirstPoint(u, v);
    if (nearFirstPoint && m_document.kind() == SketchDocument::Kind::Profile && !m_document.points().empty())
    {
        const auto& first = m_document.points().front();
        u = first.u;
        v = first.v;
    }
    const bool constrained = snapped || orthoApplied || nearFirstPoint;
    m_view.updateCursorText(u, v, constrained);
    updateCursorInfo(u, v, snapKind, nearFirstPoint, orthoApplied);
    const bool showSnapIndicator = (snapped && snapKind == SketchSnapKind::Point) || nearFirstPoint;
    m_view.updateSnapIndicator(showSnapIndicator, {u, v});
    if (nearFirstPoint)
        setStatus(tr("可闭合。"), true);
    updateRubberLine(u, v);
}

bool Sketch2DDialog::beginControlPointDrag(double u, double v)
{
    if (currentCommand() != Command::Select || m_draggingControlPoint)
        return false;

    int objectIndex = -1;
    int pointIndex = -1;
    Point2D point;
    if (!findNearestEndpoint(u, v, objectIndex, pointIndex, point))
        return false;

    m_draggingControlPoint = true;
    m_controlPointDragMoved = false;
    m_draggedPointObjectIndex = objectIndex;
    m_draggedPointIndex = pointIndex;
    pushUndo();
    selectEndpoint(objectIndex, pointIndex, point);
    setStatus(tr("拖动控制点，松开鼠标完成。"), true);
    return true;
}

void Sketch2DDialog::dragControlPointTo(double u, double v)
{
    if (!m_draggingControlPoint || m_draggedPointObjectIndex < 0 || m_draggedPointIndex < 0)
        return;

    if (!updateCommittedPoint(m_draggedPointObjectIndex, m_draggedPointIndex, u, v))
        return;

    m_controlPointDragMoved = true;
    const Point2D point{u, v};
    m_selectedPointObjectIndex = m_draggedPointObjectIndex;
    m_selectedPointIndex = m_draggedPointIndex;
    m_updatingPointCoordinates = true;
    if (ui->pointUSpin)
        ui->pointUSpin->setValue(u);
    if (ui->pointVSpin)
        ui->pointVSpin->setValue(v);
    m_updatingPointCoordinates = false;
    m_view.updateSelectedPointMarkers({point});
    m_view.updateFixedPointText(true, point);
    updatePreview();
}

void Sketch2DDialog::endControlPointDrag(double u, double v)
{
    if (!m_draggingControlPoint)
        return;

    dragControlPointTo(u, v);
    const int objectIndex = m_draggedPointObjectIndex;
    const int pointIndex = m_draggedPointIndex;
    const bool moved = m_controlPointDragMoved;

    m_draggingControlPoint = false;
    m_controlPointDragMoved = false;
    m_draggedPointObjectIndex = -1;
    m_draggedPointIndex = -1;

    if (!moved)
    {
        if (!m_undoStack.empty())
            m_undoStack.pop_back();
        updateActionButtons();
        setStatus(tr("已选择控制点。"), true);
        return;
    }

    refreshObjectTable();
    selectObjectRow(objectIndex);
    m_selectedPointObjectIndex = objectIndex;
    m_selectedPointIndex = pointIndex;
    const auto& points = documentForCommittedRow(objectIndex).points();
    if (pointIndex >= 0 && pointIndex < static_cast<int>(points.size()))
    {
        const Point2D point = points[static_cast<std::size_t>(pointIndex)];
        m_view.updateSelectedPointMarkers({point});
        m_view.updateFixedPointText(true, point);
    }
    updatePreview();
    updateGeometryFromDocuments(tr("控制点已移动。"), false);
    setStatus(tr("控制点已移动，可继续拖动其他点。"), true);
}

void Sketch2DDialog::handleSketchBoxSelection(double u0, double v0, double u1, double v1,
                                                  bool crossing, bool additive)
{
    const double minU = std::min(u0, u1);
    const double maxU = std::max(u0, u1);
    const double minV = std::min(v0, v1);
    const double maxV = std::max(v0, v1);
    QList<int> rows;
    for (int i = 0; i < static_cast<int>(m_committedDocuments.size()); ++i)
    {
        double objectMinU = 0.0;
        double objectMaxU = 0.0;
        double objectMinV = 0.0;
        double objectMaxV = 0.0;
        if (!documentSelectionBounds(documentForCommittedRow(i), objectMinU, objectMaxU, objectMinV, objectMaxV))
            continue;

        const bool hit = crossing
            ? !(objectMaxU < minU || objectMinU > maxU || objectMaxV < minV || objectMinV > maxV)
            : objectMinU >= minU && objectMaxU <= maxU && objectMinV >= minV && objectMaxV <= maxV;
        if (hit)
            rows.push_back(i);
    }

    clearEndpointSelection();
    selectObjectRows(rows, additive);
    setStatus(rows.isEmpty()
                  ? tr("框选范围内没有草图对象。")
                  : tr("已框选 %1 个对象。").arg(rows.size()),
              true);
}

void Sketch2DDialog::selectEndpoint(int objectIndex, int pointIndex, const Point2D& point)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(m_committedDocuments.size()))
        return;

    selectObjectRow(objectIndex);
    m_selectedPointObjectIndex = objectIndex;
    m_selectedPointIndex = pointIndex;
    m_view.updateSelectedPointMarkers({point});
    m_view.updateFixedPointText(true, point);

    m_updatingPointCoordinates = true;
    if (ui->pointUSpin)
        ui->pointUSpin->setValue(point.u);
    if (ui->pointVSpin)
        ui->pointVSpin->setValue(point.v);
    m_updatingPointCoordinates = false;
    if (ui->pointCoordinateLabel)
        ui->pointCoordinateLabel->setText(tr("对象 %1 · 点 %2").arg(objectIndex + 1).arg(pointIndex + 1));
    if (ui->pointCoordinateGroup)
        ui->pointCoordinateGroup->setVisible(true);
    updateInfoPanel();
}

void Sketch2DDialog::clearEndpointSelection()
{
    m_selectedPointObjectIndex = -1;
    m_selectedPointIndex = -1;
    if (ui->pointCoordinateGroup)
        ui->pointCoordinateGroup->setVisible(false);
    m_view.clearSelectedPointMarkers();
    m_view.updateFixedPointText(false, {});
    updateInfoPanel();
}

bool Sketch2DDialog::updateCommittedPoint(int objectIndex, int pointIndex, double u, double v)
{
    if (objectIndex < 0 || objectIndex >= static_cast<int>(m_committedDocuments.size()))
        return false;

    SketchDocument document = m_committedDocuments[static_cast<std::size_t>(objectIndex)];
    const auto& controlPoints = document.points();
    if (pointIndex < 0 || pointIndex >= static_cast<int>(controlPoints.size()))
        return false;
    const Point2D oldPoint = controlPoints[static_cast<std::size_t>(pointIndex)];
    const Point2D newPoint{u, v};
    if (std::hypot(oldPoint.u - newPoint.u, oldPoint.v - newPoint.v) <= geometryTolerance())
        return false;

    const bool construction = document.isConstruction();
    if (document.kind() == SketchDocument::Kind::Circle)
    {
        document.setCircle(u, v, document.circleRadius());
    }
    else if (document.kind() == SketchDocument::Kind::Arc)
    {
        std::vector<Point2D> points = document.points();
        if (pointIndex >= static_cast<int>(points.size()))
            return false;
        points[static_cast<std::size_t>(pointIndex)] = newPoint;
        const auto arcKind = document.arcKind();
        const double sweep = document.arcSweep();
        document.setArc(points, document.isFinished(), arcKind);
        document.setArcSweep(sweep);
    }
    else if (document.kind() == SketchDocument::Kind::Curve)
    {
        std::vector<Point2D> points = document.points();
        if (pointIndex >= static_cast<int>(points.size()))
            return false;
        points[static_cast<std::size_t>(pointIndex)] = newPoint;
        document.setCurve(points, document.isFinished());
    }
    else if (document.kind() == SketchDocument::Kind::PointsOnly)
    {
        document.editablePoints()[static_cast<std::size_t>(pointIndex)] = newPoint;
    }
    else if (document.kind() == SketchDocument::Kind::Profile)
    {
        std::vector<Point2D> keyPoints = document.points();
        if (pointIndex >= static_cast<int>(keyPoints.size()))
            return false;
        keyPoints[static_cast<std::size_t>(pointIndex)] = newPoint;
        if (document.hasProfileSegments())
        {
            for (Point2D& keyPoint : keyPoints)
            {
                if (std::hypot(keyPoint.u - oldPoint.u,
                               keyPoint.v - oldPoint.v) <= geometryTolerance())
                {
                    keyPoint = newPoint;
                }
            }
            auto segments = document.profileSegments();
            auto replacePoint = [&](Point2D& candidate)
            {
                if (std::hypot(candidate.u - oldPoint.u, candidate.v - oldPoint.v) <= geometryTolerance())
                    candidate = newPoint;
            };
            for (auto& segment : segments)
            {
                replacePoint(segment.start);
                replacePoint(segment.control);
                replacePoint(segment.end);
            }
            document.setProfileSegments(keyPoints, segments, document.isClosed(), document.isFinished());
        }
        else if (document.hasCustomDisplayPoints())
        {
            std::vector<Point2D> display = document.displayPoints();
            display[static_cast<std::size_t>(pointIndex)] = newPoint;
            document.setProfileWithDisplayPoints(keyPoints, display, document.isClosed(), document.isFinished());
        }
        else
        {
            document.setProfile(keyPoints, document.isClosed(), document.isFinished());
        }
    }
    else
    {
        return false;
    }

    document.setConstruction(construction);
    m_committedDocuments[static_cast<std::size_t>(objectIndex)] = document;
    return true;
}

void Sketch2DDialog::applySelectedPointCoordinates()
{
    if (m_updatingPointCoordinates || !ui->pointUSpin || !ui->pointVSpin ||
        m_selectedPointObjectIndex < 0 || m_selectedPointIndex < 0)
        return;

    const int objectIndex = m_selectedPointObjectIndex;
    const int pointIndex = m_selectedPointIndex;
    pushUndo();
    if (!updateCommittedPoint(objectIndex, pointIndex, ui->pointUSpin->value(), ui->pointVSpin->value()))
    {
        if (!m_undoStack.empty())
            m_undoStack.pop_back();
        return;
    }

    const Point2D point{ui->pointUSpin->value(), ui->pointVSpin->value()};
    refreshObjectTable();
    selectObjectRow(objectIndex);
    m_selectedPointObjectIndex = objectIndex;
    m_selectedPointIndex = pointIndex;
    m_view.updateSelectedPointMarkers({point});
    m_view.updateFixedPointText(true, point);
    updatePreview();
    updateGeometryFromDocuments(tr("点坐标已更新。"), false);
    setStatus(tr("点坐标已更新。"), true);
}

void Sketch2DDialog::cancelCurrentCommand()
{
    clearPendingPoint();
    m_document.clear();
    m_editingCommittedIndex = -1;
    resetProfileSegmentState();
    refreshPointTable();
    setCommand(Command::Select);
    updatePreview();
    setStatus(tr("已取消当前命令。"), true);
}

void Sketch2DDialog::finalizeSketchEditing()
{
    if (!m_document.isRenderable())
    {
        m_document.clear();
        m_editingCommittedIndex = -1;
        clearPendingPoint();
        return;
    }

    if (m_document.kind() == SketchDocument::Kind::Curve && m_document.points().size() >= 3)
        m_document.setFinished(true);
    if (m_document.kind() == SketchDocument::Kind::Profile)
    {
        if (m_document.points().size() < 2)
        {
            m_document.clear();
            m_editingCommittedIndex = -1;
            clearPendingPoint();
            return;
        }
        m_document.setClosed(documentEffectivelyClosed(m_document));
        m_document.setFinished(true);
    }
    commitActiveDocument(QString());
}

void Sketch2DDialog::updateRubberLine(double u, double v)
{
    const Command command = currentCommand();
    const bool tangentGuideMode = command == Command::Polyline &&
                                  m_profileSegmentMode == ProfileSegmentMode::ArcTangentEnd &&
                                  m_document.kind() == SketchDocument::Kind::Profile &&
                                  m_document.points().size() >= 2 &&
                                  !m_document.isClosed() && !m_document.isFinished();
    if (!tangentGuideMode)
        m_view.updateTangentGuideLine(false, {}, {});
    if (m_document.isFinished())
    {
        m_view.updateConstructionGuide(false, {});
        m_view.updateRubberShape(false, {}, false);
        return;
    }

    if (command == Command::Rectangle && m_hasPendingPoint)
    {
        m_view.updateConstructionGuide(false, {});
        m_view.updateRubberShape(true, rectangleGuide(m_pendingPoint, {u, v}), true);
        return;
    }

    if (command == Command::Circle && m_hasPendingPoint)
    {
        m_view.updateConstructionGuide(false, {});
        const double radius = std::hypot(u - m_pendingPoint.u, v - m_pendingPoint.v);
        const auto points = circleGuide(m_pendingPoint, radius);
        if (points.size() >= 3)
            m_view.updateRubberShape(true, points, true);
        else
            m_view.updateRubberShape(false, {}, false);
        return;
    }

    if (command == Command::Arc && m_document.kind() == SketchDocument::Kind::Arc && !m_document.points().empty())
    {
        std::vector<Point2D> points = m_document.points();
        points.push_back({u, v});
        if (points.size() == 2)
        {
            if (m_document.arcKind() == SketchDocument::ArcKind::CenterAngle)
                m_view.updateConstructionGuide(true, {{points[0], points[1]}});
            else
                m_view.updateConstructionGuide(false, {});
            m_view.updateRubberLine(true, points[0], points[1]);
            return;
        }
        if (points.size() >= 3)
        {
            const double arcSweep = m_document.arcKind() == SketchDocument::ArcKind::CenterAngle
                                        ? updateCenterAngleTracking(points[0], points[1], points[2])
                                        : 0.0;
            const auto guide = m_document.arcKind() == SketchDocument::ArcKind::CenterAngle
                                   ? centerAngleArcGuide(points[0], points[1], points[2], arcSweep)
                                   : arcGuide(points[0], points[1], points[2]);
            const auto construction = m_document.arcKind() == SketchDocument::ArcKind::CenterAngle
                                          ? centerAngleArcConstructionSegments(points[0], points[1], points[2], arcSweep)
                                          : threePointArcConstructionSegments(points[0], points[1], points[2]);
            m_view.updateConstructionGuide(true, construction);
            m_view.updateRubberShape(true, guide, false);
            return;
        }
    }

    if (command == Command::Curve && m_document.kind() == SketchDocument::Kind::Curve && !m_document.points().empty())
    {
        m_view.updateConstructionGuide(false, {});
        std::vector<Point2D> points = m_document.points();
        points.push_back({u, v});
        const auto guide = curveGuide(points);
        if (guide.size() >= 2)
            m_view.updateRubberShape(true, guide, false);
        else
            m_view.updateRubberShape(false, {}, false);
        return;
    }

    if (command == Command::Polyline && m_document.kind() == SketchDocument::Kind::Profile && !m_document.points().empty())
    {
        if (m_document.isClosed())
        {
            m_view.updateConstructionGuide(false, {});
            m_view.updateRubberShape(false, {}, false);
            return;
        }

        const Point2D cursor{u, v};
        const Point2D start = m_document.points().back();
        if (m_profileSegmentMode == ProfileSegmentMode::ArcThreePoint)
        {
            if (!m_profileArcMidPending)
            {
                m_view.updateConstructionGuide(false, {});
                m_view.updateRubberLine(true, start, cursor);
                return;
            }
            const auto guide = arcGuide(start, m_profileArcMid, cursor);
            m_view.updateConstructionGuide(true, threePointArcConstructionSegments(start, m_profileArcMid, cursor));
            if (guide.size() >= 2)
                m_view.updateRubberShape(true, guide, false);
            else
                m_view.updateRubberLine(true, start, cursor);
            return;
        }

        if (m_profileSegmentMode == ProfileSegmentMode::ArcCenterAngle)
        {
            if (!m_profileArcMidPending)
            {
                m_view.updateConstructionGuide(true, {{start, cursor}});
                m_view.updateRubberLine(true, start, cursor);
                return;
            }
            const double arcSweep = updateCenterAngleTracking(start, m_profileArcMid, cursor);
            const auto guide = centerAngleArcGuide(start, m_profileArcMid, cursor, arcSweep);
            m_view.updateConstructionGuide(true, centerAngleArcConstructionSegments(start, m_profileArcMid, cursor, arcSweep));
            if (guide.size() >= 2)
                m_view.updateRubberShape(true, guide, false);
            else
                m_view.updateRubberLine(true, start, cursor);
            return;
        }

        if (m_profileSegmentMode == ProfileSegmentMode::ArcTangentEnd)
        {
            if (!m_profileArcMidPending)
            {
                // First click after choosing tangent-arc mode defines the tangent direction.
                // Show only the cyan tangent candidate, not an arc yet.
                m_view.updateConstructionGuide(false, {});
                m_view.updateTangentGuideLine(true, start, cursor);
                m_view.updateRubberShape(false, {}, false);
                return;
            }

            m_view.updateTangentGuideLine(true, start, m_profileArcMid);
            const auto guide = tangentArcGuide(start, m_profileArcMid, cursor);
            m_view.updateConstructionGuide(true, tangentArcConstructionSegments(start, m_profileArcMid, cursor));
            if (guide.size() >= 2)
                m_view.updateRubberShape(true, guide, false);
            else
                m_view.updateRubberLine(true, start, cursor);
            return;
        }

        const auto& target = isNearFirstPoint(u, v) ? m_document.points().front() : cursor;
        m_view.updateConstructionGuide(false, {});
        m_view.updateRubberLine(true, start, target);
        return;
    }

    m_view.updateConstructionGuide(false, {});
    m_view.updateRubberShape(false, {}, false);
}

void Sketch2DDialog::updatePreview()
{
    if (m_loadingJson)
    {
        updateActionButtons();
        return;
    }

    syncDocumentFromTable();
    if (!m_isCurrentPage)
    {
        updateActionButtons();
        return;
    }

    if (!m_editorPreviewVisible)
    {
        m_view.clearPreview();
        updateActionButtons();
        return;
    }

    m_view.setObjectPrefix(m_uuid);
    m_view.setPlaneSettings(planeSettings());
    m_view.setBoxSelectionEnabled(currentCommand() == Command::Select);
    m_view.updatePointMarkers(visiblePoints());

    auto documents = visibleDocuments();
    if (currentCommand() == Command::Curve &&
        m_document.kind() == SketchDocument::Kind::Curve &&
        !m_document.isFinished() &&
        m_document.isRenderable() &&
        !documents.empty())
    {
        documents.pop_back();
    }

    std::vector<SketchDocument> normalDocuments;
    std::vector<SketchDocument> constructionDocuments;
    for (const auto& document : documents)
    {
        if (document.isConstruction())
            constructionDocuments.push_back(document);
        else
            normalDocuments.push_back(document);
    }

    if (normalDocuments.empty())
    {
        m_view.updatePreview(TopoDS_Shape());
    }
    else
    {
        const auto result = SketchShapeBuilder::buildMany(normalDocuments, planeSettings());
        if (result.ok)
            m_view.updatePreview(result.shape);
        else
            m_view.updatePreview(TopoDS_Shape());
    }

    if (constructionDocuments.empty())
    {
        m_view.updateConstructionPreview(TopoDS_Shape());
    }
    else
    {
        const auto result = SketchShapeBuilder::buildMany(constructionDocuments, planeSettings());
        m_view.updateConstructionPreview(result.ok ? result.shape : TopoDS_Shape());
    }

    if (documents.empty())
    {
        if (m_referencePoints.empty() && m_document.points().empty())
            setStatus(tr("右键选择绘图命令；拖动空白区域可框选。"), true);
        else if (!m_referencePoints.empty())
            setStatus(tr("只有辅助点。"), true);
        else
            setStatus(tr("点数不足。"), true);
    }

    updateSelectionHighlight();
    updateActionButtons();
}

void Sketch2DDialog::updateSelectionHighlight()
{
    const QList<int> rows = selectedCommittedIndices();
    if (rows.isEmpty())
    {
        m_view.clearSelectionHighlight();
        if (m_selectedPointObjectIndex < 0)
            m_view.clearSelectedPointMarkers();
        updateInfoPanel();
        return;
    }

    std::vector<SketchDocument> selectedDocuments;
    for (int row : rows)
    {
        if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()))
            selectedDocuments.push_back(documentForCommittedRow(row));
    }
    const auto result = SketchShapeBuilder::buildMany(selectedDocuments, planeSettings());
    if (result.ok && !result.shape.IsNull())
        m_view.updateSelectionHighlight(result.shape);
    else
        m_view.clearSelectionHighlight();

    // 单选对象后立即显示真实控制点。之前只有进入“编辑”或已经点中
    // 某个端点后才出现标记，用户很难知道哪里可以点击。
    if (m_selectedPointObjectIndex < 0)
    {
        if (currentCommand() == Command::Select && rows.size() == 1)
        {
            const int row = rows.front();
            if (row >= 0 && row < static_cast<int>(m_committedDocuments.size()))
                m_view.updateSelectedPointMarkers(documentForCommittedRow(row).points());
        }
        else
        {
            m_view.clearSelectedPointMarkers();
        }
    }
    updateInfoPanel();
}

void Sketch2DDialog::generateGeometry()
{
    syncDocumentFromTable();
    if (m_document.isRenderable())
    {
        pushUndo();
        commitActiveDocument(tr("草图已应用。可以继续绘制下一个对象。"));
        return;
    }
    clearPendingPoint();
    updateGeometryFromDocuments(tr("草图已更新。"), true);
}

void Sketch2DDialog::importDxfToSketch()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this, tr("导入 DXF 到草图"), QString(), tr("DXF 文件 (*.dxf)"));
    if (filePath.isEmpty())
        return;

    TopoDS_Shape shape;
    QString parserMessage;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool readOk = ImportGeometryBuilder::readDxfShape(filePath, shape, parserMessage);
    QApplication::restoreOverrideCursor();
    if (!readOk || shape.IsNull())
    {
        QMessageBox::warning(this, tr("DXF 导入失败"),
                             parserMessage.isEmpty() ? tr("未读取到有效 DXF 几何。")
                                                     : parserMessage);
        return;
    }

    DxfSketchConversion conversion;
    try
    {
        conversion = convertDxfShapeToSketch(shape);
    }
    catch (const Standard_Failure& failure)
    {
        const char* message = failure.GetMessageString();
        QMessageBox::warning(this, tr("DXF 转换失败"),
                             message ? QString::fromLocal8Bit(message)
                                     : tr("DXF 几何无法转换为草图。"));
        return;
    }
    catch (const std::exception& error)
    {
        QMessageBox::warning(this, tr("DXF 转换失败"),
                             QString::fromLocal8Bit(error.what()));
        return;
    }

    if (conversion.documents.empty())
    {
        QMessageBox::warning(this, tr("DXF 转换失败"),
                             tr("DXF 已读取，但没有可转换的二维草图轮廓。"));
        return;
    }

    // 先安全收尾用户正在绘制的对象，再把整次导入作为一步可撤销操作。
    syncDocumentFromTable();
    finishActiveDocumentBeforeNewObject();
    pushUndo();

    m_committedDocuments.insert(m_committedDocuments.end(),
                                conversion.documents.begin(), conversion.documents.end());
    m_document.clear();
    m_editingCommittedIndex = -1;
    resetProfileSegmentState();
    clearPendingPoint();
    setCommand(Command::Select);
    refreshPointTable();
    refreshObjectTable();
    clearSketchSelection();

    QString message = tr("已从 %1 导入 %2 个草图元素（%3 个精确直线/圆弧）。")
                          .arg(QFileInfo(filePath).fileName())
                          .arg(conversion.documents.size())
                          .arg(conversion.exactCount);
    if (conversion.approximatedCount > 0)
    {
        message += tr(" %1 个样条/椭圆已转为平滑可编辑曲线。")
                       .arg(conversion.approximatedCount);
    }
    if (conversion.skippedCount > 0)
        message += tr(" %1 条无效边已跳过。").arg(conversion.skippedCount);

    updatePreview();
    updateGeometryFromDocuments(message, false);
    autoFitSketchView();
    setStatus(message, true);
}

void Sketch2DDialog::clearSketch()
{
    if (hasAnySketchData())
        pushUndo();

    m_committedDocuments.clear();
    m_referencePoints.clear();
    m_document.clear();
    m_editingCommittedIndex = -1;
    resetProfileSegmentState();
    clearPendingPoint();
    refreshPointTable();
    refreshObjectTable();
    updatePreview();
    cleanupGeneratedGeometry();
    setStatus(tr("草图已清空。"), true);
}

void Sketch2DDialog::removeSelectedPoint()
{
    if (currentCommand() == Command::Select && !selectedCommittedIndices().isEmpty())
    {
        deleteSelectedObject();
        return;
    }

    if ((!ui->pointsTable || ui->pointsTable->currentRow() < 0) && selectedCommittedIndex() >= 0)
    {
        deleteSelectedObject();
        return;
    }

    if (!ui->pointsTable)
        return;

    const int row = ui->pointsTable->currentRow();
    if (row < 0)
        return;

    if (!tableShowsReferencePoints() &&
        m_document.kind() == SketchDocument::Kind::Profile &&
        m_document.hasProfileSegments())
    {
        setStatus(tr("含圆弧段的轮廓请整体编辑，不能直接删除单点。"), false);
        return;
    }

    pushUndo();
    if (tableShowsReferencePoints())
    {
        if (row >= 0 && row < static_cast<int>(m_referencePoints.size()))
            m_referencePoints.erase(m_referencePoints.begin() + row);
    }
    else
    {
        m_document.removePoint(row);
    }

    clearPendingPoint();
    refreshPointTable();
    updatePreview();
}

void Sketch2DDialog::reverseProfile()
{
    const bool isProfile = m_document.kind() == SketchDocument::Kind::Profile;
    const bool isCurve = m_document.kind() == SketchDocument::Kind::Curve;
    const bool reversibleArc = m_document.kind() == SketchDocument::Kind::Arc &&
                               m_document.arcKind() == SketchDocument::ArcKind::Through;
    if ((!isProfile && !isCurve && !reversibleArc) || m_document.points().size() < 2)
        return;
    if (isProfile && m_document.hasProfileSegments())
    {
        setStatus(tr("圆弧轮廓不能反向。"), false);
        return;
    }

    pushUndo();
    m_document.reverseProfile();
    refreshPointTable();
    updatePreview();
    setStatus(isProfile ? tr("轮廓方向已反转。") : tr("方向已反转。"), true);
}



bool Sketch2DDialog::ensureProfileDocumentForEdit(const QString& actionName)
{
    if (m_document.kind() == SketchDocument::Kind::Profile && !m_document.points().empty())
        return true;

    const int row = selectedCommittedIndex();
    if (row >= 0 && documentForCommittedRow(row).kind() == SketchDocument::Kind::Profile)
    {
        if (ensureCommittedObjectInEditMode(row))
            return true;
    }

    setStatus(tr("%1：未选择轮廓。").arg(actionName), false);
    return false;
}

void Sketch2DDialog::mergeProfileEndpoints()
{
    if (!ensureProfileDocumentForEdit(tr("合并端点")))
        return;

    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 2)
    {
        setStatus(tr("合并：无可处理轮廓。"), false);
        return;
    }

    std::vector<Point2D> points = cleanedProfilePoints(m_document.points(), pointSnapTolerance());
    bool closed = m_document.isClosed();
    bool changed = points.size() != m_document.points().size();
    if (points.size() >= 3 && std::hypot(points.front().u - points.back().u,
                                         points.front().v - points.back().v) <= autoCloseTolerance())
    {
        points.pop_back();
        closed = true;
        changed = true;
    }

    if (!changed && closed == m_document.isClosed())
    {
        setStatus(tr("合并：无需处理。"), true);
        return;
    }

    pushUndo();
    m_document.setProfile(points, closed, m_document.isFinished());
    clearPendingPoint();
    refreshPointTable();
    updatePreview();
    updateGeometryFromDocuments(tr("端点已合并。"), false);
    setStatus(closed ? tr("端点已合并并闭合。") : tr("端点已合并。"), true);
}

void Sketch2DDialog::checkProfileClosure()
{
    if (!ensureProfileDocumentForEdit(tr("闭合检查")))
        return;

    if (m_document.kind() != SketchDocument::Kind::Profile)
    {
        setStatus(tr("检查：不是轮廓。"), false);
        return;
    }

    const auto& points = m_document.points();
    if (points.size() < 2)
    {
        setStatus(tr("检查：点数不足。"), false);
        return;
    }

    const double endpointDistance = std::hypot(points.front().u - points.back().u,
                                               points.front().v - points.back().v);
    const bool selfIntersect = profileHasSelfIntersection(points, m_document.isClosed(), geometryTolerance() * 10.0);
    const double area = std::abs(polygonSignedArea(points));

    if (!m_document.isClosed())
    {
        if (points.size() >= 3 && endpointDistance <= autoCloseTolerance())
            setStatus(tr("首尾距 %1，可合并。")
                          .arg(endpointDistance, 0, 'f', 4), true);
        else
            setStatus(tr("开放轮廓，首尾距 %1。")
                          .arg(endpointDistance, 0, 'f', 4), true);
        return;
    }

    if (selfIntersect)
    {
        setStatus(tr("闭合但自交。"), false);
        return;
    }
    if (area <= geometryTolerance())
    {
        setStatus(tr("闭合但面积过小。"), false);
        return;
    }

    setStatus(tr("闭合正常，面积 %1。") .arg(area, 0, 'f', 4), true);
}

void Sketch2DDialog::closeProfile()
{
    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 3)
    {
        setStatus(tr("至少 3 点。"), false);
        return;
    }

    pushUndo();
    m_document.setClosed(true);
    commitActiveDocument(outputAsEntity()
                             ? tr("实体已生成。")
                             : tr("闭合曲线已生成。"));
}

void Sketch2DDialog::finishOpenProfile()
{
    if (m_document.kind() == SketchDocument::Kind::Curve)
    {
        if (m_document.points().size() < 3)
        {
            clearPendingPoint();
            setStatus(tr("样条至少 3 点。"), false);
            return;
        }

        pushUndo();
        m_document.setFinished(true);
        commitActiveDocument(tr("样条已生成。"));
        return;
    }

    if (m_document.kind() != SketchDocument::Kind::Profile || m_document.points().size() < 2)
    {
        clearPendingPoint();
        setStatus(tr("无可生成曲线。"), true);
        return;
    }

    pushUndo();
    if (documentEffectivelyClosed(m_document))
    {
        m_document.setClosed(true);
        commitActiveDocument(outputAsEntity()
                                 ? tr("实体已生成。")
                                 : tr("闭合曲线已生成。"));
        return;
    }

    m_document.setClosed(false);
    commitActiveDocument(outputAsEntity()
                             ? tr("轮廓未闭合，已按曲线生成。")
                             : tr("曲线已生成。"));
}
