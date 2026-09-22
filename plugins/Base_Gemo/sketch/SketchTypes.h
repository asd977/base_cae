#pragma once

#include <gp_Pnt.hxx>

namespace GeometrySketch
{

enum class Plane
{
    XY = 0,
    XZ = 1,
    YZ = 2
};

enum class Command
{
    Select = 0,
    Polyline = 1,
    Rectangle = 2,
    Circle = 3,
    Point = 4,
    Arc = 5,
    Curve = 6
};

enum class OutputMode
{
    Entity = 0,
    Curve = 1
};

struct Point2D
{
    double u = 0.0;
    double v = 0.0;
};

struct PlaneSettings
{
    Plane plane = Plane::XY;
    double offset = 0.0;
    double gridStep = 1.0;
    bool showAxes = false;
    OutputMode outputMode = OutputMode::Entity;
};

inline gp_Pnt toWorldPoint(const PlaneSettings& settings, double u, double v)
{
    switch (settings.plane)
    {
    case Plane::XY:
        return gp_Pnt(u, v, settings.offset);
    case Plane::XZ:
        return gp_Pnt(u, settings.offset, v);
    case Plane::YZ:
        return gp_Pnt(settings.offset, u, v);
    }
    return gp_Pnt(u, v, settings.offset);
}

inline void uvFromWorld(const PlaneSettings& settings, const double world[3], double& u, double& v)
{
    switch (settings.plane)
    {
    case Plane::XY:
        u = world[0];
        v = world[1];
        break;
    case Plane::XZ:
        u = world[0];
        v = world[2];
        break;
    case Plane::YZ:
        u = world[1];
        v = world[2];
        break;
    }
}

} // namespace GeometrySketch
