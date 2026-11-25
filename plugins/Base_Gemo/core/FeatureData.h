#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <geometry_global.h>
#include "CadBody.h"

class GEOMETRY_EXPORT FeatureData
{
public:
    QString uuid;
    QString title;
    QString type;
    QString module;
    QString kind;
    bool visible = true;
    QJsonObject state;
    QString cachedBrep;
    QString cachedBrepAsset;
    QStringList inputUuids;
    QStringList missingInputUuids;
    QVector<CadBody::FaceGroup> faceGroups;

    bool isValid() const;
    bool hasMissingInputs() const;

    QJsonObject toJson() const;
    static FeatureData fromJson(const QJsonObject& obj);

    static QStringList collectInputUuids(const QJsonObject& state, const QString& selfUuid = QString());
    static QString parameterSummary(const QString& type, const QString& kind, const QJsonObject& state);
    static QString typeLabel(const QString& type, const QString& kind);
    static bool requiresInputs(const QString& type, const QString& kind);
    static bool isPrimitiveLike(const QString& type, const QString& kind);
    static bool isFeatureLike(const QString& type, const QString& kind);
    static bool canEditParameters(const QString& type, const QString& kind);
    static QString editabilityText(const QString& type, const QString& kind,
                                   const QStringList& inputUuids,
                                   const QStringList& missingInputUuids,
                                   bool hasShape, bool hasCachedBrep);
};
