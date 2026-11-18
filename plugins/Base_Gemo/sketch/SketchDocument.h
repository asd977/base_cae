#pragma once

#include "SketchTypes.h"

#include <QJsonObject>
#include <vector>

namespace GeometrySketch
{

class SketchDocument
{
public:
    enum class Kind
    {
        Empty = 0,
        Profile = 1,
        Circle = 2,
        PointsOnly = 3,
        Arc = 4,
        Curve = 5,
        NativeCurve = 6
    };

    enum class ProfileSegmentKind
    {
        Line = 0,
        ArcThrough = 1,
        ArcTangent = 2
    };

    enum class ArcKind
    {
        Through = 0,
        CenterAngle = 1
    };

    struct ProfileSegment
    {
        ProfileSegmentKind kind = ProfileSegmentKind::Line;
        Point2D start;
        Point2D control;  // ArcThrough: midpoint; ArcTangent: tangent point
        Point2D end;
    };

    void clear();
    const QString& nativeBrep() const {return m_nativeBrep;}
    void setNativeCurve(const std::vector<Point2D>& sampledPoints, const QString& brep, bool closed=false);

    Kind kind() const { return m_kind; }
    bool isClosed() const { return m_closed; }
    bool isFinished() const { return m_finished; }
    bool isConstruction() const { return m_construction; }
    double circleRadius() const { return m_circleRadius; }
    ArcKind arcKind() const { return m_arcKind; }
    double arcSweep() const { return m_arcSweep; }
    const std::vector<Point2D>& points() const { return m_points; }
    const std::vector<Point2D>& displayPoints() const;
    bool hasCustomDisplayPoints() const { return !m_displayPoints.empty(); }
    const std::vector<ProfileSegment>& profileSegments() const { return m_profileSegments; }
    bool hasProfileSegments() const { return !m_profileSegments.empty(); }

    std::vector<Point2D>& editablePoints() { return m_points; }

    bool isRenderable() const;
    void setClosed(bool closed);
    void setFinished(bool finished) { m_finished = finished; }
    void setConstruction(bool construction) { m_construction = construction; }

    void setProfile(const std::vector<Point2D>& points, bool closed, bool finished = false);
    void setProfileWithDisplayPoints(const std::vector<Point2D>& geometryPoints,
                                     const std::vector<Point2D>& displayPoints,
                                     bool closed, bool finished = false);
    void setProfileSegments(const std::vector<Point2D>& keyPoints,
                            const std::vector<ProfileSegment>& segments,
                            bool closed, bool finished = false);
    void appendProfileLineTo(double u, double v);
    void appendProfileArcThrough(const Point2D& mid, const Point2D& end);
    void appendProfileArcTangent(const Point2D& tangentPoint, const Point2D& end);
    void setDisplayPoints(const std::vector<Point2D>& points);
    void appendProfilePoint(double u, double v);
    void appendStandalonePoint(double u, double v);
    void removePoint(int index);
    void reverseProfile();

    void setRectangle(double u1, double v1, double u2, double v2);
    void setCircle(double centerU, double centerV, double radius);
    void setArc(const std::vector<Point2D>& points, bool finished = false, ArcKind arcKind = ArcKind::Through);
    void setArcKind(ArcKind arcKind)
    {
        m_arcKind = arcKind;
        if (m_arcKind != ArcKind::CenterAngle)
            m_arcSweep = 0.0;
    }
    void setArcSweep(double sweep);
    void setCurve(const std::vector<Point2D>& points, bool finished = false);
    void appendArcPoint(double u, double v, ArcKind arcKind = ArcKind::Through);
    void appendCurvePoint(double u, double v);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

private:
    Kind m_kind = Kind::Empty;
    QString m_nativeBrep;
    std::vector<Point2D> m_points;
    // Optional editing/key points shown in the UI. Geometry may contain sampled points
    // for arcs/curves, but users should only see meaningful construction points.
    std::vector<Point2D> m_displayPoints;
    // Real topology for continuous hand-drawn profiles. Arcs are kept as arcs instead
    // of being flattened into many straight segments; this prevents many vertical
    // seam lines after extrusion.
    std::vector<ProfileSegment> m_profileSegments;
    bool m_closed = false;
    bool m_finished = false;
    bool m_construction = false;
    double m_circleRadius = 0.0;
    ArcKind m_arcKind = ArcKind::Through;
    double m_arcSweep = 0.0;
};

} // namespace GeometrySketch
