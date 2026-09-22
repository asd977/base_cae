#pragma once
#include <QString>
class PrimitiveBuilder
{
public:
    static bool recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z);
    static bool recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z);
    static bool recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z);
    static bool recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z);
    static bool recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z);
};
