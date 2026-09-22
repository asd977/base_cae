#pragma once
#include <QString>
class PlanarProfileBuilder
{
public:
    static bool recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType);
    static bool recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType);
    static bool recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType);
    static bool recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType);
};
