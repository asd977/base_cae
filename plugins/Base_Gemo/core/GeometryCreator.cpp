#include "GeometryCreator.h"

#include "GeometryBuildUtils.h"
#include "PrimitiveBuilder.h"
#include "PlanarProfileBuilder.h"
#include "ImportGeometryBuilder.h"
#include "BooleanBuilder.h"
#include "FeatureBuilder.h"
#include "TransformBuilder.h"
#include "ModifyBuilder.h"

void GeometryCreator::registerShape(const QString& uuid, const QString& name, TopoDS_Shape* shape)
{
    GeometryBuildUtils::registerShape(uuid, name, shape);
}

void GeometryCreator::removeGeometryAndDimNode(const QString& key)
{
    GeometryBuildUtils::removeGeometryAndDimNode(key);
}

bool GeometryCreator::recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z)
{
    return PrimitiveBuilder::recreateBox(uuid, name, length, width, height, x, y, z);
}

bool GeometryCreator::recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z)
{
    return PrimitiveBuilder::recreateSphere(uuid, name, radius, x, y, z);
}

bool GeometryCreator::recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z)
{
    return PrimitiveBuilder::recreateCone(uuid, name, bottomRadius, topRadius, height, x, y, z);
}

bool GeometryCreator::recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z)
{
    return PrimitiveBuilder::recreateCylinder(uuid, name, radius, height, x, y, z);
}

bool GeometryCreator::recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z)
{
    return PrimitiveBuilder::recreateTorus(uuid, name, majorRadius, minorRadius, x, y, z);
}

bool GeometryCreator::recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType)
{
    return PlanarProfileBuilder::recreateCircle(uuid, name, radius, x, y, z, planeType);
}

bool GeometryCreator::recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType)
{
    return PlanarProfileBuilder::recreateEllipse(uuid, name, majorRadius, minorRadius, x, y, z, planeType);
}

bool GeometryCreator::recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType)
{
    return PlanarProfileBuilder::recreateRectangle(uuid, name, length, width, x, y, z, planeType);
}

bool GeometryCreator::recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType)
{
    return PlanarProfileBuilder::recreatePolygon(uuid, name, radius, sides, rotateDeg, x, y, z, planeType);
}

bool GeometryCreator::recreateImportGeometry(const QString& uuid, const QString& name, const QString& filePath, QString& errorMsg)
{
    return ImportGeometryBuilder::recreateImportGeometry(uuid, name, filePath, errorMsg);
}

bool GeometryCreator::recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return BooleanBuilder::recreateBooleanFuse(uuid, name, selectedKeys, operationKeys, savedResultBrep);
}

bool GeometryCreator::recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return BooleanBuilder::recreateBooleanCut(uuid, name, selectedKeys, operationKeys, savedResultBrep);
}

bool GeometryCreator::recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep)
{
    return BooleanBuilder::recreateBooleanCommon(uuid, name, selectedKeys, operationKeys, savedResultBrep);
}

bool GeometryCreator::analyzeExtrudeProfile(const QString& profileUuid, int* faceCount,
                                             int* suggestedDirection, QString* message,
                                             int* openEdgeCount)
{
    return FeatureBuilder::analyzeExtrudeProfile(profileUuid, faceCount, suggestedDirection,
                                                 message, openEdgeCount);
}

bool GeometryCreator::buildExtrudeShape(const QString& profileUuid,
                                         const ExtrudeParameters& parameters,
                                         TopoDS_Shape& result, int* faceCount,
                                         QString* message)
{
    return FeatureBuilder::buildExtrudeShape(profileUuid, parameters, result, faceCount, message);
}

bool GeometryCreator::recreateExtrude(const QString& uuid, const QString& name,
                                       const QString& profileUuid,
                                       const QVector<CadFaceRef>& faceRefs,
                                       const ExtrudeParameters& parameters)
{
    return FeatureBuilder::recreateExtrude(uuid, name, profileUuid, faceRefs, parameters);
}

bool GeometryCreator::recreateExtrude(const QString& uuid, const QString& name,
                                       const QString& profileUuid,
                                       const ExtrudeParameters& parameters)
{
    return recreateExtrude(uuid, name, profileUuid, {}, parameters);
}

bool GeometryCreator::buildExtrudeShape(const QString& profileUuid, double height, int direction,
                                         TopoDS_Shape& result, int* faceCount, QString* message)
{
    return FeatureBuilder::buildExtrudeShape(profileUuid, height, direction, result, faceCount, message);
}

bool GeometryCreator::recreateExtrude(const QString& uuid, const QString& name,
                                       const QString& profileUuid, double height, int direction)
{
    return FeatureBuilder::recreateExtrude(uuid, name, profileUuid, height, direction);
}

bool GeometryCreator::recreateRevolve(const QString& uuid, const QString& name,
                                       const QString& profileUuid,
                                       const RevolveParameters& parameters)
{
    return FeatureBuilder::recreateRevolve(uuid, name, profileUuid, parameters);
}

bool GeometryCreator::recreateRevolve(const QString& uuid, const QString& name, const QString& profileUuid, double angleDeg, int axisType, double ox, double oy, double oz)
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

bool GeometryCreator::recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid)
{
    return FeatureBuilder::recreateSweep(uuid, name, profileUuid, pathUuid);
}

bool GeometryCreator::recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid, bool ruled)
{
    return FeatureBuilder::recreateLoft(uuid, name, profileUuids, makeSolid, ruled);
}

bool GeometryCreator::recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction)
{
    return FeatureBuilder::recreateCutExtrude(uuid, name, targetUuid, profileUuid, depth, direction);
}

bool GeometryCreator::recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                      const PatternDirection& direction1,
                                      const PatternDirection& direction2,
                                      const PatternDirection& direction3)
{
    return TransformBuilder::recreatePattern(uuid, name, sourceUuid, direction1, direction2, direction3);
}

bool GeometryCreator::recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                                      int countX, int countY, int countZ, double dx, double dy, double dz)
{
    return TransformBuilder::recreatePattern(uuid, name, sourceUuid, countX, countY, countZ, dx, dy, dz);
}


bool GeometryCreator::recreatePolarPattern(const QString& uuid, const QString& name,
                                           const QString& sourceUuid,
                                           const PolarPatternParameters& parameters)
{
    return TransformBuilder::recreatePolarPattern(uuid, name, sourceUuid, parameters);
}

bool GeometryCreator::recreateTransform(const QString& uuid, const QString& name, const QString& sourceUuid, double dx, double dy, double dz, double rxDeg, double ryDeg, double rzDeg, int pivotMode, double pivotX, double pivotY, double pivotZ, int orderVersion)
{
    return TransformBuilder::recreateTransform(uuid, name, sourceUuid, dx, dy, dz, rxDeg, ryDeg, rzDeg, pivotMode, pivotX, pivotY, pivotZ, orderVersion);
}

bool GeometryCreator::recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset)
{
    return TransformBuilder::recreateMirror(uuid, name, sourceUuid, planeType, offset);
}

bool GeometryCreator::recreateFillet(const QString& uuid, const QString& name, const QString& sourceUuid, double radius, const QString& edgeSelection)
{
    return ModifyBuilder::recreateFillet(uuid, name, sourceUuid, radius, edgeSelection);
}

bool GeometryCreator::recreateChamfer(const QString& uuid, const QString& name, const QString& sourceUuid, double distance, const QString& edgeSelection)
{
    return ModifyBuilder::recreateChamfer(uuid, name, sourceUuid, distance, edgeSelection);
}

bool GeometryCreator::recreateSplit(const QString& uuid, const QString& name, const QString& sourceUuid, const QString& toolUuid)
{
    return ModifyBuilder::recreateSplit(uuid, name, sourceUuid, toolUuid);
}


bool GeometryCreator::recreateSplitPart(const QString& uuid, const QString& name,
                                        const QString& splitUuid, int partIndex)
{
    return ModifyBuilder::recreateSplitPart(uuid, name, splitUuid, partIndex);
}
