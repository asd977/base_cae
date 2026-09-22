#pragma once
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector3D>
#include <CadTypes.h>
#include <TopoDS_Shape.hxx>
#include <geometry_global.h>

class GEOMETRY_EXPORT GeometryCreator
{
public:
    static bool recreateBox(const QString& uuid, const QString& name, double length, double width, double height, double x, double y, double z);
    static bool recreateSphere(const QString& uuid, const QString& name, double radius, double x, double y, double z);
    static bool recreateCone(const QString& uuid, const QString& name, double bottomRadius, double topRadius, double height, double x, double y, double z);
    static bool recreateCylinder(const QString& uuid, const QString& name, double radius, double height, double x, double y, double z);
    static bool recreateTorus(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z);
    
    static bool recreateCircle(const QString& uuid, const QString& name, double radius, double x, double y, double z, int planeType);
    static bool recreateEllipse(const QString& uuid, const QString& name, double majorRadius, double minorRadius, double x, double y, double z, int planeType);
    static bool recreateRectangle(const QString& uuid, const QString& name, double length, double width, double x, double y, double z, int planeType);
    static bool recreatePolygon(const QString& uuid, const QString& name, double radius, int sides, double rotateDeg, double x, double y, double z, int planeType);
    
    
    static bool recreateImportGeometry(const QString& uuid, const QString& name, const QString& filePath, QString& errorMsg);
    
    static bool recreateBooleanFuse(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCut(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);
    static bool recreateBooleanCommon(const QString& uuid, const QString& name, const QStringList& selectedKeys, const QStringList& operationKeys, const QString& savedResultBrep);

    static bool analyzeExtrudeProfile(const QString& profileUuid, int* faceCount = nullptr,
                                      int* suggestedDirection = nullptr, QString* message = nullptr,
                                      int* openEdgeCount = nullptr);
    static bool buildExtrudeShape(const QString& profileUuid,
                                  const ExtrudeParameters& parameters,
                                  TopoDS_Shape& result, int* faceCount = nullptr,
                                  QString* message = nullptr);
    static bool recreateExtrude(const QString& uuid, const QString& name,
                                const QString& profileUuid,
                                const QVector<CadFaceRef>& faceRefs,
                                const ExtrudeParameters& parameters);
    static bool recreateExtrude(const QString& uuid, const QString& name,
                                const QString& profileUuid,
                                const ExtrudeParameters& parameters);
    static bool buildExtrudeShape(const QString& profileUuid, double height, int direction,
                                  TopoDS_Shape& result, int* faceCount = nullptr,
                                  QString* message = nullptr);
    static bool recreateExtrude(const QString& uuid, const QString& name,
                                const QString& profileUuid, double height,
                                int direction = 4);
    static bool recreateRevolve(const QString& uuid, const QString& name,
                                const QString& profileUuid,
                                const RevolveParameters& parameters);
    static bool recreateRevolve(const QString& uuid, const QString& name, const QString& profileUuid, double angleDeg, int axisType, double ox, double oy, double oz);
    static bool recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid);
    static bool recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid = true, bool ruled = false);
    static bool recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction = 4);
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
    static bool recreateFillet(const QString& uuid, const QString& name, const QString& sourceUuid, double radius, const QString& edgeSelection = QString());
    static bool recreateChamfer(const QString& uuid, const QString& name, const QString& sourceUuid, double distance, const QString& edgeSelection = QString());
    static bool recreateSplit(const QString& uuid, const QString& name, const QString& sourceUuid, const QString& toolUuid);
    static bool recreateSplitPart(const QString& uuid, const QString& name,
                                  const QString& splitUuid, int partIndex);

private:
    static void registerShape(const QString& uuid, const QString& name, TopoDS_Shape* shape);
    static void removeGeometryAndDimNode(const QString& key);
};
