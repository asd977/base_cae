#pragma once
#include <QString>
class TemplateGeometryBuilder
{
public:
    static bool recreateGravityDam(const QString& uuid, const QString& name, double height, double crestWidth, double upSlope, double downSlope, double length, int layerCount, double x, double y, double z);
    static bool recreateEarthRockfillDam(const QString& uuid, const QString& name, double height, double crestWidth, double upSlope, double downSlope, double length, int layerCount, double coreTop, double coreBottom, double filter, double coreOffset);
    static bool recreateSlopeTemplate(const QString& uuid, const QString& name, double height, double benchWidth, double slope, double length, int benchCount, double top, double toe, double baseH);
    static bool recreateCavernTemplate(const QString& uuid, const QString& name, double span, double wallH, double archH, double length, double stage, double sideRock, double topRock, double bottomRock);

    static bool recreateSlopeTemplateLinked(const QString& uuid, const QString& name, const QString& targetUuid, double height, double benchWidth, double slope, double length, int benchCount, double top, double toe, double baseH, double posX, double posY, double posZ);
    static bool recreateCavernTemplateLinked(const QString& uuid, const QString& name, const QString& targetUuid, double span, double wallH, double archH, double length, double posX, double posY, double posZ);
    static bool recreateEarthRockfillDamLinked(const QString& uuid, const QString& name, const QString& targetUuid, double height, double crestWidth, double upSlope, double downSlope, double length, double posX, double posY, double posZ);
    static bool recreateGravityDamLinked(const QString& uuid, const QString& name, const QString& targetUuid, double height, double crestWidth, double upSlope, double downSlope, double length, double posX, double posY, double posZ);
};
