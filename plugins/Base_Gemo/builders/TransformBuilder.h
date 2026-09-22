#pragma once
#include <QString>
#include <QStringList>
#include <CadTypes.h>
class TransformBuilder
{
public:
    static bool recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                const PatternDirection& direction1,
                                const PatternDirection& direction2,
                                const PatternDirection& direction3);
    static bool recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                int countX, int countY, int countZ, double dx, double dy, double dz);
    static bool recreatePolarPattern(const QString& uuid, const QString& name,
                                     const QString& sourceUuid,
                                     const PolarPatternParameters& parameters);
    static bool recreateTransform(const QString& uuid, const QString& name, const QString& sourceUuid, double dx, double dy, double dz, double rxDeg, double ryDeg, double rzDeg, int pivotMode = 1, double pivotX = 0.0, double pivotY = 0.0, double pivotZ = 0.0, int orderVersion = 2);
    static bool recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset);
};
