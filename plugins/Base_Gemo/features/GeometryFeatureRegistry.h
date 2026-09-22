#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

class BasePage;
class FeatureData;
class IGeometryService;

namespace GeometryFeatures
{

struct FeatureDescriptor
{
    QString type;
    QString kind;
    QString label;
    QStringList typeAliases;
    QStringList kindAliases;
    bool primitiveLike = false;
    bool featureLike = false;
    bool sketchProfile = false;
    std::function<BasePage*()> createPage;
    std::function<bool(const FeatureData&, IGeometryService&, QString*)> rebuild;
};

const FeatureDescriptor* descriptorFor(const QString& type, const QString& kind = QString());
const FeatureDescriptor* descriptorForKind(const QString& kind);

BasePage* createPage(const QString& type, const QString& kind = QString());
QString normalizeType(const QString& type, const QString& kind = QString());
QString normalizeKind(const QString& type, const QString& kind = QString());
QString labelForKind(const QString& kind);

bool rebuildFeature(const FeatureData& feature, IGeometryService& service, QString* message = nullptr);
bool isPrimitiveLike(const QString& type, const QString& kind);
bool isFeatureLike(const QString& type, const QString& kind);
bool isSketchProfileKind(const QString& kind);
QStringList consumedInputUuidsForFinalModel(const QString& type, const QString& kind,
                                               const QJsonObject& state,
                                               const QString& selfUuid = QString());

} // namespace GeometryFeatures
