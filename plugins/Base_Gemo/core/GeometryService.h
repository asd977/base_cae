#pragma once
#include <IGeometryService.h>
#include <geometry_global.h>

class GEOMETRY_EXPORT GeometryService : public IGeometryService
{
public:
    GeometryService();
    ~GeometryService() override;

    using IGeometryService::recreatePattern;
    using IGeometryService::recreateExtrude;

    bool recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z) override;
    bool recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z) override;
    bool recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z) override;
    bool recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z) override;
    bool recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z) override;
    bool recreateGravityDam(const QString& uuid, const QString& name,
                            double height, double crestWidth,
                            double upstreamWidth, double downstreamWidth,
                            double length, int layerCount,
                            double x, double y, double z) override;
    bool recreateEarthRockfillDam(const QString& uuid, const QString& name,
                                  double height, double crestWidth,
                                  double upstreamWidth, double downstreamWidth,
                                  double length, int layerCount,
                                  double coreTopWidth, double coreBottomWidth,
                                  double filterWidth, double coreOffset) override;
    bool recreateSlopeTemplate(const QString& uuid, const QString& name,
                               double height, double benchWidth,
                               double stageHorizontalWidth, double length,
                               int benchCount, double topPlatformWidth,
                               double toeExtensionLength, double baseHeight) override;
    bool recreateCavernTemplate(const QString& uuid, const QString& name,
                                double span, double wallHeight, double archHeight,
                                double length, double stageCount,
                                double sideRock, double topRock,
                                double bottomRock) override;
    
    bool recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType) override;
    bool recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType) override;
    bool recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType) override;
    bool recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType) override;
    
    
    bool recreateImportGeometry(const QString& uuid, const QString& name, const QString& filePath, QString& errorMsg) override;
    
    bool recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) override;
    bool recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) override;
    bool recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep) override;
    bool recreateExtrude(const QString& uuid, const QString& name,
                         const QString& profileUuid,
                         const QVector<CadFaceRef>& faceRefs,
                         const ExtrudeParameters& parameters) override;
    bool recreateExtrude(const QString& uuid, const QString& name,
                         const QString& profileUuid,
                         const ExtrudeParameters& parameters) override;
    bool recreateRevolve(const QString& uuid, const QString& name,
                         const QString& profileUuid,
                         const RevolveParameters& parameters) override;
    bool recreateRevolve(const QString& uuid, const QString& name, const QString& profileUuid, double angleDeg, int axisType, double ox, double oy, double oz) override;
    bool recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid) override;
    bool recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid = true, bool ruled = false) override;
    bool recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction = 4) override;
    bool recreatePattern(const QString& uuid, const QString& name, const QString& sourceUuid,
                         const PatternDirection& direction1,
                         const PatternDirection& direction2,
                         const PatternDirection& direction3) override;
    bool recreatePolarPattern(const QString& uuid, const QString& name,
                              const QString& sourceUuid,
                              const PolarPatternParameters& parameters) override;
    bool recreateTransform(const QString& uuid, const QString& name, const QString& sourceUuid, double dx, double dy, double dz, double rxDeg, double ryDeg, double rzDeg, int pivotMode = 1, double pivotX = 0.0, double pivotY = 0.0, double pivotZ = 0.0, int orderVersion = 2) override;
    bool recreateMirror(const QString& uuid, const QString& name, const QString& sourceUuid, int planeType, double offset) override;
    bool recreateFillet(const QString& uuid, const QString& name, const QString& sourceUuid, double radius, const QString& edgeSelection = QString()) override;
    bool recreateChamfer(const QString& uuid, const QString& name, const QString& sourceUuid, double distance, const QString& edgeSelection = QString()) override;
    bool recreateSplit(const QString& uuid, const QString& name, const QString& sourceUuid, const QString& toolUuid) override;
    bool recreateSplitPart(const QString& uuid, const QString& name,
                           const QString& splitUuid, int partIndex) override;
};
