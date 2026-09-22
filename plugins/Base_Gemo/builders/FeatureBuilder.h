#pragma once
#include <QString>
#include <QStringList>
#include <CadTypes.h>
#include <TopoDS_Shape.hxx>
class FeatureBuilder
{
public:
    static bool analyzeExtrudeProfile(const QString& profileUuid, int* faceCount = nullptr,
                                      int* suggestedDirection = nullptr, QString* message = nullptr,
                                      int* openEdgeCount = nullptr);
    static bool buildExtrudeShape(const QString& profileUuid,
                                  const QVector<CadFaceRef>& faceRefs,
                                  const ExtrudeParameters& parameters,
                                  TopoDS_Shape& result, int* faceCount = nullptr,
                                  QString* message = nullptr);
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

    // Compatibility overloads for old callers and old project data.
    static bool buildExtrudeShape(const QString& profileUuid, double height, int direction,
                                  TopoDS_Shape& result, int* faceCount = nullptr,
                                  QString* message = nullptr);
    static bool recreateExtrude(const QString& uuid, const QString& name,
                                const QString& profileUuid, double height,
                                int direction = 4);
    static bool buildRevolveShape(const QString& profileUuid,
                                  const RevolveParameters& parameters,
                                  TopoDS_Shape& result, QString* message = nullptr);
    static bool recreateRevolve(const QString& uuid, const QString& name,
                                const QString& profileUuid,
                                const RevolveParameters& parameters);
    static bool buildRevolveShape(const QString& profileUuid, double angleDeg, int axisType,
                                  double ox, double oy, double oz, TopoDS_Shape& result,
                                  QString* message = nullptr);
    static bool recreateRevolve(const QString& uuid, const QString& name, const QString& profileUuid, double angleDeg, int axisType, double ox, double oy, double oz);
    static bool analyzeSweepProfile(const QString& profileUuid, QString* message = nullptr);
    static bool analyzeSweepPath(const QString& pathUuid, QString* message = nullptr);
    static bool analyzeLoftSection(const QString& profileUuid, QString* message = nullptr);
    static bool buildSweepShape(const QString& profileUuid, const QString& pathUuid, TopoDS_Shape& result, QString* message = nullptr);
    static bool recreateSweep(const QString& uuid, const QString& name, const QString& profileUuid, const QString& pathUuid);
    static bool buildLoftShape(const QStringList& profileUuids, bool makeSolid, bool ruled, TopoDS_Shape& result, QString* message = nullptr);
    static bool recreateLoft(const QString& uuid, const QString& name, const QStringList& profileUuids, bool makeSolid = true, bool ruled = false);
    static bool buildCutExtrudeShape(const QString& targetUuid, const QString& profileUuid, double depth, int direction, TopoDS_Shape& result, QString* message = nullptr);
    static bool recreateCutExtrude(const QString& uuid, const QString& name, const QString& targetUuid, const QString& profileUuid, double depth, int direction = 4);
};
