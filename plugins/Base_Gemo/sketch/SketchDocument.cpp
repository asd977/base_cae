#include "SketchDocument.h"

#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace GeometrySketch
{

namespace
{

SketchDocument::Kind safeKindFromInt(int value)
{
    switch (static_cast<SketchDocument::Kind>(value))
    {
    case SketchDocument::Kind::Empty:
    case SketchDocument::Kind::Profile:
    case SketchDocument::Kind::Circle:
    case SketchDocument::Kind::PointsOnly:
    case SketchDocument::Kind::Arc:
    case SketchDocument::Kind::Curve:
        return static_cast<SketchDocument::Kind>(value);
    }
    return SketchDocument::Kind::Empty;
}

SketchDocument::ProfileSegmentKind safeSegmentKindFromInt(int value)
{
    switch (static_cast<SketchDocument::ProfileSegmentKind>(value))
    {
    case SketchDocument::ProfileSegmentKind::Line:
    case SketchDocument::ProfileSegmentKind::ArcThrough:
    case SketchDocument::ProfileSegmentKind::ArcTangent:
        return static_cast<SketchDocument::ProfileSegmentKind>(value);
    }
    return SketchDocument::ProfileSegmentKind::Line;
}

SketchDocument::ArcKind safeArcKindFromInt(int value)
{
    switch (static_cast<SketchDocument::ArcKind>(value))
    {
    case SketchDocument::ArcKind::Through:
    case SketchDocument::ArcKind::CenterAngle:
        return static_cast<SketchDocument::ArcKind>(value);
    }
    return SketchDocument::ArcKind::Through;
}

QJsonObject pointToJson(const Point2D& p)
{
    QJsonObject obj;
    obj[QStringLiteral("u")] = p.u;
    obj[QStringLiteral("v")] = p.v;
    return obj;
}

Point2D pointFromJson(const QJsonObject& obj)
{
    return {obj.value(QStringLiteral("u")).toDouble(),
            obj.value(QStringLiteral("v")).toDouble()};
}

bool samePoints(const std::vector<Point2D>& a, const std::vector<Point2D>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        if (std::abs(a[i].u - b[i].u) > 1.0e-12 ||
            std::abs(a[i].v - b[i].v) > 1.0e-12)
            return false;
    }
    return true;
}

} // namespace

void SketchDocument::clear()
{
    m_kind = Kind::Empty;
    m_points.clear();
    m_displayPoints.clear();
    m_profileSegments.clear();
    m_closed = false;
    m_finished = false;
    m_construction = false;
    m_circleRadius = 0.0;
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

const std::vector<Point2D>& SketchDocument::displayPoints() const
{
    return m_displayPoints.empty() ? m_points : m_displayPoints;
}

void SketchDocument::setDisplayPoints(const std::vector<Point2D>& points)
{
    m_displayPoints = points;
    if (samePoints(m_displayPoints, m_points))
        m_displayPoints.clear();
}

bool SketchDocument::isRenderable() const
{
    if (m_kind == Kind::Circle)
        return m_points.size() == 1 && m_circleRadius > 1.0e-9;
    if (m_kind == Kind::Arc)
        return m_points.size() == 3;
    if (m_kind == Kind::Curve)
        return m_points.size() >= 3;
    if (m_kind == Kind::Profile)
        return m_points.size() >= (m_closed ? 3U : 2U);
    return false;
}

void SketchDocument::setClosed(bool closed)
{
    if (m_kind == Kind::Profile)
        m_closed = closed;
}

void SketchDocument::setProfile(const std::vector<Point2D>& points, bool closed, bool finished)
{
    m_kind = points.empty() ? Kind::Empty : Kind::Profile;
    m_points = points;
    m_displayPoints.clear();
    m_profileSegments.clear();
    m_closed = closed;
    m_finished = finished;
    m_circleRadius = 0.0;
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

void SketchDocument::setProfileWithDisplayPoints(const std::vector<Point2D>& geometryPoints,
                                                 const std::vector<Point2D>& displayPoints,
                                                 bool closed, bool finished)
{
    setProfile(geometryPoints, closed, finished);
    if (!displayPoints.empty() && !samePoints(displayPoints, geometryPoints))
        m_displayPoints = displayPoints;
}

void SketchDocument::setProfileSegments(const std::vector<Point2D>& keyPoints,
                                          const std::vector<ProfileSegment>& segments,
                                          bool closed, bool finished)
{
    m_kind = keyPoints.empty() ? Kind::Empty : Kind::Profile;
    m_points = keyPoints;
    m_displayPoints.clear();
    m_profileSegments = segments;
    m_closed = closed;
    m_finished = finished;
    m_circleRadius = 0.0;
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

void SketchDocument::appendProfileLineTo(double u, double v)
{
    if (m_kind != Kind::Profile || m_points.empty())
    {
        appendProfilePoint(u, v);
        return;
    }

    const Point2D start = m_points.back();
    const Point2D end{u, v};
    if (std::hypot(start.u - end.u, start.v - end.v) <= 1.0e-12)
        return;

    m_profileSegments.push_back({ProfileSegmentKind::Line, start, {}, end});
    m_points.push_back(end);
    if (!m_displayPoints.empty())
        m_displayPoints.push_back(end);
    m_closed = false;
    m_finished = false;
}

void SketchDocument::appendProfileArcThrough(const Point2D& mid, const Point2D& end)
{
    if (m_kind != Kind::Profile || m_points.empty())
        return;

    const Point2D start = m_points.back();
    m_profileSegments.push_back({ProfileSegmentKind::ArcThrough, start, mid, end});
    m_points.push_back(end);
    if (!m_displayPoints.empty())
        m_displayPoints.push_back(end);
    m_closed = false;
    m_finished = false;
}

void SketchDocument::appendProfileArcTangent(const Point2D& tangentPoint, const Point2D& end)
{
    if (m_kind != Kind::Profile || m_points.empty())
        return;

    const Point2D start = m_points.back();
    m_profileSegments.push_back({ProfileSegmentKind::ArcTangent, start, tangentPoint, end});
    m_points.push_back(end);
    if (!m_displayPoints.empty())
        m_displayPoints.push_back(end);
    m_closed = false;
    m_finished = false;
}

void SketchDocument::appendProfilePoint(double u, double v)
{
    if (m_kind != Kind::Profile)
    {
        m_kind = Kind::Profile;
        m_points.clear();
        m_displayPoints.clear();
        m_profileSegments.clear();
        m_closed = false;
        m_circleRadius = 0.0;
        m_arcKind = ArcKind::Through;
        m_arcSweep = 0.0;
    }

    const Point2D p{u, v};
    if (!m_points.empty())
    {
        const Point2D start = m_points.back();
        if (std::hypot(start.u - p.u, start.v - p.v) <= 1.0e-12)
            return;
        m_profileSegments.push_back({ProfileSegmentKind::Line, start, {}, p});
    }

    m_points.push_back(p);
    if (!m_displayPoints.empty())
        m_displayPoints.push_back(p);
    m_finished = false;
}

void SketchDocument::appendStandalonePoint(double u, double v)
{
    if (m_kind != Kind::PointsOnly)
    {
        m_kind = Kind::PointsOnly;
        m_points.clear();
        m_displayPoints.clear();
        m_profileSegments.clear();
        m_closed = false;
        m_circleRadius = 0.0;
        m_arcKind = ArcKind::Through;
        m_arcSweep = 0.0;
    }
    m_points.push_back({u, v});
    m_finished = false;
}

void SketchDocument::setArc(const std::vector<Point2D>& points, bool finished, ArcKind arcKind)
{
    m_kind = points.empty() ? Kind::Empty : Kind::Arc;
    m_points = points;
    m_displayPoints.clear();
    m_profileSegments.clear();
    if (m_points.size() > 3)
        m_points.resize(3);
    m_closed = false;
    m_finished = finished;
    m_circleRadius = 0.0;
    m_arcKind = arcKind;
    if (arcKind != ArcKind::CenterAngle)
        m_arcSweep = 0.0;
}

void SketchDocument::setArcSweep(double sweep)
{
    m_arcSweep = (m_kind == Kind::Arc && m_arcKind == ArcKind::CenterAngle) ? sweep : 0.0;
}

void SketchDocument::setCurve(const std::vector<Point2D>& points, bool finished)
{
    m_kind = points.empty() ? Kind::Empty : Kind::Curve;
    m_points = points;
    m_displayPoints.clear();
    m_profileSegments.clear();
    m_closed = false;
    m_finished = finished;
    m_circleRadius = 0.0;
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

void SketchDocument::appendArcPoint(double u, double v, ArcKind arcKind)
{
    if (m_kind != Kind::Arc)
    {
        m_kind = Kind::Arc;
        m_points.clear();
        m_displayPoints.clear();
        m_profileSegments.clear();
        m_closed = false;
        m_circleRadius = 0.0;
        m_arcSweep = 0.0;
    }
    m_arcKind = arcKind;
    if (arcKind != ArcKind::CenterAngle)
        m_arcSweep = 0.0;
    if (m_points.size() < 3)
        m_points.push_back({u, v});
    m_finished = m_points.size() == 3;
}

void SketchDocument::appendCurvePoint(double u, double v)
{
    if (m_kind != Kind::Curve)
    {
        m_kind = Kind::Curve;
        m_points.clear();
        m_displayPoints.clear();
        m_profileSegments.clear();
        m_closed = false;
        m_circleRadius = 0.0;
        m_arcKind = ArcKind::Through;
        m_arcSweep = 0.0;
    }
    m_points.push_back({u, v});
    m_finished = false;
}

void SketchDocument::removePoint(int index)
{
    if (index < 0 || index >= static_cast<int>(m_points.size()))
        return;

    if (m_kind == Kind::Profile && !m_profileSegments.empty())
        m_profileSegments.clear();

    m_points.erase(m_points.begin() + index);
    if (!m_displayPoints.empty() && index < static_cast<int>(m_displayPoints.size()))
        m_displayPoints.erase(m_displayPoints.begin() + index);
    if (m_points.empty())
    {
        clear();
        return;
    }

    if (m_kind == Kind::Circle)
    {
        clear();
        return;
    }

    if (m_kind == Kind::Arc)
    {
        m_closed = false;
        m_finished = false;
        return;
    }

    if (m_kind == Kind::Curve)
    {
        m_closed = false;
        m_finished = false;
        return;
    }

    if (m_points.size() < 3)
        m_closed = false;
}

void SketchDocument::reverseProfile()
{
    if (m_kind == Kind::Arc && m_arcKind == ArcKind::CenterAngle)
        return;

    if (m_kind == Kind::Profile || m_kind == Kind::Arc || m_kind == Kind::Curve)
    {
        std::reverse(m_points.begin(), m_points.end());
        if (!m_displayPoints.empty())
            std::reverse(m_displayPoints.begin(), m_displayPoints.end());
        if (m_kind == Kind::Profile)
            m_profileSegments.clear();
    }
}

void SketchDocument::setRectangle(double u1, double v1, double u2, double v2)
{
    m_kind = Kind::Profile;
    m_points.clear();
    m_points.push_back({u1, v1});
    m_points.push_back({u2, v1});
    m_points.push_back({u2, v2});
    m_points.push_back({u1, v2});
    m_displayPoints.clear();
    m_profileSegments.clear();
    m_closed = true;
    m_finished = true;
    m_circleRadius = 0.0;
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

void SketchDocument::setCircle(double centerU, double centerV, double radius)
{
    m_kind = Kind::Circle;
    m_points.clear();
    m_displayPoints.clear();
    m_profileSegments.clear();
    m_points.push_back({centerU, centerV});
    m_closed = true;
    m_finished = true;
    m_circleRadius = std::max(0.0, radius);
    m_arcKind = ArcKind::Through;
    m_arcSweep = 0.0;
}

QJsonObject SketchDocument::toJson() const
{
    QJsonObject obj;
    obj["kind"] = static_cast<int>(m_kind);
    obj["closed"] = m_closed;
    obj["finished"] = m_finished;
    obj["construction"] = m_construction;
    obj["circleRadius"] = m_circleRadius;
    if (m_kind == Kind::Arc)
    {
        obj["arcKind"] = static_cast<int>(m_arcKind);
        if (m_arcKind == ArcKind::CenterAngle && std::abs(m_arcSweep) > 1.0e-12)
            obj["arcSweep"] = m_arcSweep;
    }

    QJsonArray arr;
    for (const auto& p : m_points)
    {
        QJsonObject pt;
        pt["u"] = p.u;
        pt["v"] = p.v;
        arr.append(pt);
    }
    obj["points"] = arr;

    if (!m_displayPoints.empty())
    {
        QJsonArray displayArr;
        for (const auto& p : m_displayPoints)
        {
            QJsonObject pt;
            pt["u"] = p.u;
            pt["v"] = p.v;
            displayArr.append(pt);
        }
        obj["displayPoints"] = displayArr;
    }

    if (!m_profileSegments.empty())
    {
        QJsonArray segmentArr;
        for (const auto& segment : m_profileSegments)
        {
            QJsonObject segObj;
            segObj[QStringLiteral("kind")] = static_cast<int>(segment.kind);
            segObj[QStringLiteral("start")] = pointToJson(segment.start);
            segObj[QStringLiteral("control")] = pointToJson(segment.control);
            segObj[QStringLiteral("end")] = pointToJson(segment.end);
            segmentArr.append(segObj);
        }
        obj[QStringLiteral("profileSegments")] = segmentArr;
    }
    return obj;
}

void SketchDocument::fromJson(const QJsonObject& obj)
{
    clear();
    m_kind = safeKindFromInt(obj.value("kind").toInt(static_cast<int>(Kind::Empty)));
    m_closed = obj.value("closed").toBool(false);
    m_finished = obj.value("finished").toBool(false);
    m_construction = obj.value("construction").toBool(false);
    m_circleRadius = obj.value("circleRadius").toDouble(0.0);
    m_arcKind = safeArcKindFromInt(obj.value("arcKind").toInt(static_cast<int>(ArcKind::Through)));
    m_arcSweep = obj.value("arcSweep").toDouble(0.0);

    const QJsonArray arr = obj.value("points").toArray();
    for (const auto& value : arr)
    {
        const QJsonObject pt = value.toObject();
        m_points.push_back({pt.value("u").toDouble(), pt.value("v").toDouble()});
    }

    const QJsonArray displayArr = obj.value("displayPoints").toArray();
    for (const auto& value : displayArr)
    {
        const QJsonObject pt = value.toObject();
        m_displayPoints.push_back({pt.value("u").toDouble(), pt.value("v").toDouble()});
    }

    const QJsonArray segmentArr = obj.value(QStringLiteral("profileSegments")).toArray();
    for (const auto& value : segmentArr)
    {
        const QJsonObject segObj = value.toObject();
        ProfileSegment segment;
        segment.kind = safeSegmentKindFromInt(segObj.value(QStringLiteral("kind")).toInt());
        segment.start = pointFromJson(segObj.value(QStringLiteral("start")).toObject());
        segment.control = pointFromJson(segObj.value(QStringLiteral("control")).toObject());
        segment.end = pointFromJson(segObj.value(QStringLiteral("end")).toObject());
        m_profileSegments.push_back(segment);
    }

    if (m_points.empty())
    {
        clear();
        return;
    }

    if (m_kind == Kind::Circle)
    {
        if (m_points.size() != 1 || m_circleRadius <= 1.0e-9)
            clear();
        return;
    }

    if (m_kind == Kind::Profile)
    {
        if (m_points.size() < 2)
            clear();
        else if (m_points.size() < 3)
            m_closed = false;
        return;
    }

    m_profileSegments.clear();

    if (m_kind == Kind::Arc)
    {
        if (m_points.size() > 3)
            m_points.resize(3);
        m_closed = false;
        m_circleRadius = 0.0;
        if (m_arcKind != ArcKind::CenterAngle)
            m_arcSweep = 0.0;
        if (m_points.size() < 3)
            m_finished = false;
        return;
    }

    if (m_kind == Kind::Curve)
    {
        m_closed = false;
        m_circleRadius = 0.0;
        m_arcSweep = 0.0;
        if (m_points.size() < 3)
            m_finished = false;
        return;
    }

    if (m_kind == Kind::PointsOnly)
    {
        m_closed = false;
        m_finished = false;
        m_circleRadius = 0.0;
        m_arcSweep = 0.0;
    }
}

} // namespace GeometrySketch
