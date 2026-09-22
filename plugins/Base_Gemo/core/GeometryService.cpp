#include "GeometryService.h"
#include "GeometryCreator.h"
#include "TemplateGeometryBuilder.h"

#include <GeometryWorkbench.h>
#include <ServiceRegistry.h>
#include <services/PreProcessingService.h>


namespace
{
bool notifyGeometryChanged(bool ok, const QString& uuid)
{
    if (!ok)
        return false;

    auto* pre = ServiceRegistry::instance().getService<PreProcessingService>();
    if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
        workbench->refreshTreePresentation(uuid);
    return true;
}
}

GeometryService::GeometryService() {}
GeometryService::~GeometryService() {}

bool GeometryService::recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z)
{
    return notifyGeometryChanged(GeometryCreator::recreateBox(uuid, name, length, width, height, x, y, z), uuid);
}

bool GeometryService::recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z)
{
    return notifyGeometryChanged(GeometryCreator::recreateSphere(uuid, name, radius, x, y, z), uuid);
}

bool GeometryService::recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z)
{
    return notifyGeometryChanged(GeometryCreator::recreateCone(uuid, name, bottomRadius, topRadius, height, x, y, z), uuid);
}

bool GeometryService::recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z)
{
    return notifyGeometryChanged(GeometryCreator::recreateCylinder(uuid, name, radius, height, x, y, z), uuid);
}

bool GeometryService::recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z)
{
    return notifyGeometryChanged(GeometryCreator::recreateTorus(uuid, name, majorRadius, minorRadius, x, y, z), uuid);
}

bool GeometryService::recreateGravityDam(const QString& uuid, const QString& name,
                                         double height, double crestWidth,
                                         double upstreamWidth, double downstreamWidth,
                                         double length, int layerCount,
                                         double x, double y, double z)
{
    return notifyGeometryChanged(
        TemplateGeometryBuilder::recreateGravityDam(
            uuid, name, height, crestWidth, upstreamWidth, downstreamWidth,
            length, layerCount, x, y, z),
        uuid);
}

bool GeometryService::recreateEarthRockfillDam(const QString& uuid, const QString& name,
                                               double height, double crestWidth,
                                               double upstreamWidth, double downstreamWidth,
                                               double length, int layerCount,
                                               double coreTopWidth, double coreBottomWidth,
                                               double filterWidth, double coreOffset)
{
    return notifyGeometryChanged(
        TemplateGeometryBuilder::recreateEarthRockfillDam(
            uuid, name, height, crestWidth, upstreamWidth, downstreamWidth,
            length, layerCount, coreTopWidth, coreBottomWidth, filterWidth, coreOffset),
        uuid);
}

bool GeometryService::recreateSlopeTemplate(const QString& uuid, const QString& name,
                                            double height, double benchWidth,
                                            double stageHorizontalWidth, double length,
                                            int benchCount, double topPlatformWidth,
                                            double toeExtensionLength, double baseHeight)
{
    return notifyGeometryChanged(
        TemplateGeometryBuilder::recreateSlopeTemplate(
            uuid, name, height, benchWidth, stageHorizontalWidth, length,
            benchCount, topPlatformWidth, toeExtensionLength, baseHeight),
        uuid);
}

bool GeometryService::recreateCavernTemplate(const QString& uuid, const QString& name,
                                             double span, double wallHeight, double archHeight,
                                             double length, double stageCount,
                                             double sideRock, double topRock,
                                             double bottomRock)
{
    return notifyGeometryChanged(
        TemplateGeometryBuilder::recreateCavernTemplate(
            uuid, name, span, wallHeight, archHeight, length, stageCount,
            sideRock, topRock, bottomRock),
        uuid);
}

bool GeometryService::recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType)
{
    return notifyGeometryChanged(GeometryCreator::recreateCircle(uuid, name, radius, x, y, z, planeType), uuid);
}

bool GeometryService::recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType)
{
    return notifyGeometryChanged(GeometryCreator::recreateEllipse(uuid, name, majorRadius, minorRadius, x, y, z, planeType), uuid);
}

bool GeometryService::recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType)
{
    return notifyGeometryChanged(GeometryCreator::recreateRectangle(uuid, name, length, width, x, y, z, planeType), uuid);
}

bool GeometryService::recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType)
{
    return notifyGeometryChanged(GeometryCreator::recreatePolygon(uuid, name, radius, sides, rotateDeg, x, y, z, planeType), uuid);
}

bool GeometryService::recreateImportGeometry(const QString& uuid, const QString& name, const QString& filePath, QString& errorMsg)
{
    return notifyGeometryChanged(GeometryCreator::recreateImportGeometry(uuid, name, filePath, errorMsg), uuid);
}

bool GeometryService::recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return notifyGeometryChanged(GeometryCreator::recreateBooleanFuse(uuid, name, selectedKeys, operationKeys, savedResultBrep), uuid);
}

bool GeometryService::recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return notifyGeometryChanged(GeometryCreator::recreateBooleanCut(uuid, name, selectedKeys, operationKeys, savedResultBrep), uuid);
}

bool GeometryService::recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return notifyGeometryChanged(GeometryCreator::recreateBooleanCommon(uuid, name, selectedKeys, operationKeys, savedResultBrep), uuid);
}

bool GeometryService::recreateExtrude(const QString& uuid, const QString& name,
                                      const QString& profileUuid,
                                      const QVector<CadFaceRef>& faceRefs,
                                      const ExtrudeParameters& parameters)
{
    return notifyGeometryChanged(GeometryCreator::recreateExtrude(uuid, name, profileUuid, faceRefs, parameters), uuid);
}

bool GeometryService::recreateExtrude(const QString& uuid, const QString& name,
                                      const QString& profileUuid,
                                      const ExtrudeParameters& parameters)
{
    return recreateExtrude(uuid, name, profileUuid, {}, parameters);
}

bool GeometryService::recreateRevolve(const QString& uuid, const QString& name,
                                      const QString& profileUuid,
                                      const RevolveParameters& parameters)
{
    return notifyGeometryChanged(GeometryCreator::recreateRevolve(uuid, name, profileUuid, parameters), uuid);
}

bool GeometryService::recreateRevolve(const QString& uuid, const QString& name, const QString& profileUuid, double angleDeg, int axisType, double ox, double oy, double oz)
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

bool GeometryService::recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid)
{
    return notifyGeometryChanged(GeometryCreator::recreateSweep(uuid, name, profileUuid, pathUuid), uuid);
}

bool GeometryService::recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid, bool ruled)
{
    return notifyGeometryChanged(GeometryCreator::recreateLoft(uuid, name, profileUuids, makeSolid, ruled), uuid);
}

bool GeometryService::recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction)
{
    return notifyGeometryChanged(GeometryCreator::recreateCutExtrude(uuid, name, targetUuid, profileUuid, depth, direction), uuid);
}

bool GeometryService::recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                      const PatternDirection& direction1,
                                      const PatternDirection& direction2,
                                      const PatternDirection& direction3)
{
    return notifyGeometryChanged(GeometryCreator::recreatePattern(uuid, name, sourceUuid,
                                                                  direction1, direction2, direction3), uuid);
}


bool GeometryService::recreatePolarPattern(const QString& uuid, const QString& name,
                                           const QString& sourceUuid,
                                           const PolarPatternParameters& parameters)
{
    return notifyGeometryChanged(
        GeometryCreator::recreatePolarPattern(uuid, name, sourceUuid, parameters), uuid);
}

bool GeometryService::recreateTransform(const QString& uuid, const QString& name, const QString& sourceUuid, double dx, double dy, double dz, double rxDeg, double ryDeg, double rzDeg, int pivotMode, double pivotX, double pivotY, double pivotZ, int orderVersion)
{
    return notifyGeometryChanged(GeometryCreator::recreateTransform(uuid, name, sourceUuid, dx, dy, dz, rxDeg, ryDeg, rzDeg, pivotMode, pivotX, pivotY, pivotZ, orderVersion), uuid);
}

bool GeometryService::recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset)
{
    return notifyGeometryChanged(GeometryCreator::recreateMirror(uuid, name, sourceUuid, planeType, offset), uuid);
}

bool GeometryService::recreateFillet(const QString& uuid, const QString& name, const QString& sourceUuid, double radius, const QString& edgeSelection)
{
    return notifyGeometryChanged(GeometryCreator::recreateFillet(uuid, name, sourceUuid, radius, edgeSelection), uuid);
}

bool GeometryService::recreateChamfer(const QString& uuid, const QString& name, const QString& sourceUuid, double distance, const QString& edgeSelection)
{
    return notifyGeometryChanged(GeometryCreator::recreateChamfer(uuid, name, sourceUuid, distance, edgeSelection), uuid);
}

bool GeometryService::recreateSplit(const QString& uuid, const QString& name, const QString& sourceUuid, const QString& toolUuid)
{
    return notifyGeometryChanged(GeometryCreator::recreateSplit(uuid, name, sourceUuid, toolUuid), uuid);
}


bool GeometryService::recreateSplitPart(const QString& uuid, const QString& name,
                                        const QString& splitUuid, int partIndex)
{
    return notifyGeometryChanged(
        GeometryCreator::recreateSplitPart(uuid, name, splitUuid, partIndex), uuid);
}
