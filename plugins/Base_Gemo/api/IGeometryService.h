#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector3D>
#include <cmath>
#include "CadTypes.h"

class IGeometryService
{
public:
    virtual ~IGeometryService() = default;

    virtual bool recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z) = 0;
    virtual bool recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z) = 0;
    virtual bool recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z) = 0;
    virtual bool recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z) = 0;
    virtual bool recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z) = 0;
    virtual bool recreateGravityDam(const QString& uuid, const QString& name,
                                    double height, double crestWidth,
                                    double upstreamWidth, double downstreamWidth,
                                    double length, int layerCount,
                                    double x, double y, double z) = 0;
    virtual bool recreateEarthRockfillDam(const QString& uuid, const QString& name,
                                          double height, double crestWidth,
                                          double upstreamWidth, double downstreamWidth,
                                          double length, int layerCount,
                                          double coreTopWidth, double coreBottomWidth,
                                          double filterWidth, double coreOffset) = 0;
    virtual bool recreateSlopeTemplate(const QString& uuid, const QString& name,
                                       double height, double benchWidth,
                                       double stageHorizontalWidth, double length,
                                       int benchCount, double topPlatformWidth,
                                       double toeExtensionLength, double baseHeight) = 0;
    virtual bool recreateCavernTemplate(const QString& uuid, const QString& name,
                                        double span, double wallHeight, double archHeight,
                                        double length, double stageCount,
                                        double sideRock, double topRock,
                                        double bottomRock) = 0;
    
    virtual bool recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType) = 0;
    virtual bool recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType) = 0;
    virtual bool recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType) = 0;
    virtual bool recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType) = 0;
    
    
    virtual bool recreateImportGeometry(const QString& uuid, const QString& name, const QString& filePath, QString& errorMsg) = 0;
    
    virtual bool recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) = 0;
    virtual bool recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) = 0;
    virtual bool recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) = 0;
    virtual bool recreateExtrude(const QString& uuid, const QString& name,
                                  const QString& profileUuid,
                                  const QVector<CadFaceRef>& faceRefs,
                                  const ExtrudeParameters& parameters) = 0;

    virtual bool recreateExtrude(const QString& uuid, const QString& name,
                                  const QString& profileUuid,
                                  const ExtrudeParameters& parameters)
    {
        return recreateExtrude(uuid, name, profileUuid, {}, parameters);
    }

    bool recreateExtrude(const QString& uuid, const QString& name,
                         const QString& profileUuid, double height,
                         int direction = 4)
    {
        ExtrudeParameters parameters;
        parameters.distance = std::abs(height);
        parameters.extentMode = ExtrudeExtentMode::OneSide;
        parameters.resultMode = ExtrudeResultMode::Solid;
        parameters.reverse = height < 0.0;
        switch (direction)
        {
        case 0: parameters.directionMode = ExtrudeDirectionMode::XAxis; break;
        case 1: parameters.directionMode = ExtrudeDirectionMode::XAxis; parameters.reverse = !parameters.reverse; break;
        case 2: parameters.directionMode = ExtrudeDirectionMode::YAxis; break;
        case 3: parameters.directionMode = ExtrudeDirectionMode::YAxis; parameters.reverse = !parameters.reverse; break;
        case 5: parameters.directionMode = ExtrudeDirectionMode::ZAxis; parameters.reverse = !parameters.reverse; break;
        case -1: parameters.directionMode = ExtrudeDirectionMode::ProfileNormal; break;
        case 4:
        default: parameters.directionMode = ExtrudeDirectionMode::ZAxis; break;
        }
        return recreateExtrude(uuid, name, profileUuid, parameters);
    }
    virtual bool recreateRevolve(const QString& uuid, const QString& name,
                                 const QString& profileUuid,
                                 const RevolveParameters& parameters) = 0;

    virtual bool recreateRevolve(const QString& uuid, const QString& name,
                                 const QString& profileUuid, double angleDeg,
                                 int axisType, double ox, double oy, double oz)
    {
        RevolveParameters parameters;
        parameters.angleDeg = angleDeg;
        parameters.axisOrigin = QVector3D(float(ox), float(oy), float(oz));
        switch (axisType)
        {
        case 0: parameters.axisDirection = QVector3D(1.0f, 0.0f, 0.0f); break;
        case 1: parameters.axisDirection = QVector3D(0.0f, 1.0f, 0.0f); break;
        case 2:
        default: parameters.axisDirection = QVector3D(0.0f, 0.0f, 1.0f); break;
        }
        return recreateRevolve(uuid, name, profileUuid, parameters);
    }
    virtual bool recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid) = 0;
    virtual bool recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid = true, bool ruled = false) = 0;
    virtual bool recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction = 4) = 0;
    virtual bool recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                 const PatternDirection& direction1,
                                 const PatternDirection& direction2,
                                 const PatternDirection& direction3) = 0;
    virtual bool recreatePolarPattern(const QString& uuid, const QString& name,
                                      const QString& sourceUuid,
                                      const PolarPatternParameters& parameters) = 0;

    // Compatibility entry for projects/modules that still use the old global X/Y/Z spacing API.
    bool recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                         int countX, int countY, int countZ, double dx, double dy, double dz)
    {
        return recreatePattern(uuid, name, sourceUuid,
                               PatternDirection{countX, QVector3D(float(dx), 0.0f, 0.0f)},
                               PatternDirection{countY, QVector3D(0.0f, float(dy), 0.0f)},
                               PatternDirection{countZ, QVector3D(0.0f, 0.0f, float(dz))});
    }
    virtual bool recreateTransform(const QString& uuid, const QString& name, const QString& sourceUuid, double dx, double dy, double dz, double rxDeg, double ryDeg, double rzDeg, int pivotMode = 1, double pivotX = 0.0, double pivotY = 0.0, double pivotZ = 0.0, int orderVersion = 2) = 0;
    virtual bool recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset) = 0;
    virtual bool recreateFillet(const QString& uuid, const QString& name, const QString& sourceUuid, double radius, const QString& edgeSelection = QString()) = 0;
    virtual bool recreateChamfer(const QString& uuid, const QString& name, const QString& sourceUuid, double distance, const QString& edgeSelection = QString()) = 0;
    virtual bool recreateSplit(const QString& uuid, const QString& name, const QString& sourceUuid, const QString& toolUuid) = 0;
    virtual bool recreateSplitPart(const QString& uuid, const QString& name,
                                   const QString& splitUuid, int partIndex) = 0;
};
