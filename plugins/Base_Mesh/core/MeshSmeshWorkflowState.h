#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QVector3D>
#include <QSet>
#include <algorithm>

#include "../../../core/PublicVariables.h"

// SMESH mixed-mesh workflow state.
//
// V8.8 rule: every persistent setting belongs to a mesh plan explicitly.
// The project-tree parent is only presentation; planUuid is authoritative.
namespace MeshSmeshWorkflowState
{
inline const QString kPartitionRulesKey = QStringLiteral("mesh.smesh.partition.rules");
inline const QString kSweepTasksKey = QStringLiteral("mesh.smesh.sweep.tasks");
inline const QString kMixedSettingsKey = QStringLiteral("mesh.smesh.mixed.settings");

struct PartitionRule
{
    QString planUuid;
    bool enabled = true;
    int axis = 0; // 0=X,1=Y,2=Z,3=CAD plane
    double coordinate = 0.0;
    QVector3D origin;
    QVector3D normal;
    int targetVolumeUiId = -1; // 仅切这个原始 CAD 体；<=0 表示旧工程兼容：作用于全部体
    int sourceFaceUiId = -1;
    int sourceEdgeUiId1 = -1;
    int sourceEdgeUiId2 = -1;

    QVector3D planeOrigin() const
    {
        if (axis == 3) return origin;
        if (axis == 0) return QVector3D(float(coordinate), 0.0f, 0.0f);
        if (axis == 1) return QVector3D(0.0f, float(coordinate), 0.0f);
        return QVector3D(0.0f, 0.0f, float(coordinate));
    }

    QVector3D planeNormal() const
    {
        if (axis == 3)
        {
            QVector3D n = normal;
            if (n.lengthSquared() > 1.0e-16f) n.normalize();
            return n;
        }
        if (axis == 0) return QVector3D(1.0f, 0.0f, 0.0f);
        if (axis == 1) return QVector3D(0.0f, 1.0f, 0.0f);
        return QVector3D(0.0f, 0.0f, 1.0f);
    }
};

struct SweepTask
{
    QString planUuid;
    bool partitionRegion = false;
    int objectId = -1;       // CAD Volume UI id or temporary Region ordinal
    int sourceFaceId = -1;   // CAD Face UI id or Region-local Face ordinal
    int targetFaceId = -1;
    int layers = 10;
    double sourceSize = 5.0;
    bool quadrangleSource = true;
};

struct MixedSettings
{
    QString planUuid;
    double remainingSize = 5.0;
    bool preferHex = true;
};

inline QVariantMap partitionRuleToVariant(const PartitionRule& r)
{
    QVariantMap m;
    m.insert(QStringLiteral("planUuid"), r.planUuid);
    m.insert(QStringLiteral("enabled"), r.enabled);
    m.insert(QStringLiteral("axis"), r.axis);
    m.insert(QStringLiteral("coordinate"), r.coordinate);
    m.insert(QStringLiteral("origin"), QVariant::fromValue(r.origin));
    m.insert(QStringLiteral("normal"), QVariant::fromValue(r.normal));
    m.insert(QStringLiteral("targetVolumeUiId"), r.targetVolumeUiId);
    m.insert(QStringLiteral("sourceFaceUiId"), r.sourceFaceUiId);
    m.insert(QStringLiteral("sourceEdgeUiId1"), r.sourceEdgeUiId1);
    m.insert(QStringLiteral("sourceEdgeUiId2"), r.sourceEdgeUiId2);
    return m;
}

inline PartitionRule partitionRuleFromVariant(const QVariantMap& m)
{
    PartitionRule r;
    r.planUuid = m.value(QStringLiteral("planUuid")).toString().trimmed();
    r.enabled = m.value(QStringLiteral("enabled"), true).toBool();
    r.axis = m.value(QStringLiteral("axis"), 0).toInt();
    r.coordinate = m.value(QStringLiteral("coordinate"), 0.0).toDouble();
    r.origin = m.value(QStringLiteral("origin")).value<QVector3D>();
    r.normal = m.value(QStringLiteral("normal")).value<QVector3D>();
    r.targetVolumeUiId = m.value(QStringLiteral("targetVolumeUiId"), -1).toInt();
    r.sourceFaceUiId = m.value(QStringLiteral("sourceFaceUiId"), -1).toInt();
    r.sourceEdgeUiId1 = m.value(QStringLiteral("sourceEdgeUiId1"), -1).toInt();
    r.sourceEdgeUiId2 = m.value(QStringLiteral("sourceEdgeUiId2"), -1).toInt();
    return r;
}

inline QVariantMap sweepTaskToVariant(const SweepTask& t)
{
    QVariantMap m;
    m.insert(QStringLiteral("planUuid"), t.planUuid);
    m.insert(QStringLiteral("partitionRegion"), t.partitionRegion);
    m.insert(QStringLiteral("objectId"), t.objectId);
    m.insert(QStringLiteral("sourceFaceId"), t.sourceFaceId);
    m.insert(QStringLiteral("targetFaceId"), t.targetFaceId);
    m.insert(QStringLiteral("layers"), t.layers);
    m.insert(QStringLiteral("sourceSize"), t.sourceSize);
    m.insert(QStringLiteral("quadrangleSource"), t.quadrangleSource);
    return m;
}

inline SweepTask sweepTaskFromVariant(const QVariantMap& m)
{
    SweepTask t;
    t.planUuid = m.value(QStringLiteral("planUuid")).toString().trimmed();
    t.partitionRegion = m.value(QStringLiteral("partitionRegion"), false).toBool();
    t.objectId = m.value(QStringLiteral("objectId"), -1).toInt();
    t.sourceFaceId = m.value(QStringLiteral("sourceFaceId"), -1).toInt();
    t.targetFaceId = m.value(QStringLiteral("targetFaceId"), -1).toInt();
    t.layers = m.value(QStringLiteral("layers"), 10).toInt();
    t.sourceSize = m.value(QStringLiteral("sourceSize"), 5.0).toDouble();
    t.quadrangleSource = m.value(QStringLiteral("quadrangleSource"), true).toBool();
    return t;
}

inline QVariantMap mixedSettingsToVariant(const MixedSettings& s)
{
    QVariantMap m;
    m.insert(QStringLiteral("planUuid"), s.planUuid);
    m.insert(QStringLiteral("remainingSize"), s.remainingSize);
    m.insert(QStringLiteral("preferHex"), s.preferHex);
    return m;
}

inline MixedSettings mixedSettingsFromVariant(const QVariantMap& m)
{
    MixedSettings s;
    s.planUuid = m.value(QStringLiteral("planUuid")).toString().trimmed();
    s.remainingSize = m.value(QStringLiteral("remainingSize"), 5.0).toDouble();
    s.preferHex = m.value(QStringLiteral("preferHex"), true).toBool();
    return s;
}

inline QVector<PartitionRule> partitionRules()
{
    QVector<PartitionRule> out;
    const QVariantList list = PublicVariables::instance().get(kPartitionRulesKey).toList();
    out.reserve(list.size());
    for (const QVariant& v : list) out.push_back(partitionRuleFromVariant(v.toMap()));
    return out;
}

inline void setPartitionRules(const QVector<PartitionRule>& rules)
{
    QVariantList list;
    list.reserve(rules.size());
    for (const PartitionRule& r : rules) list.push_back(partitionRuleToVariant(r));
    PublicVariables::instance().set(kPartitionRulesKey, list, QStringLiteral("mesh"));
}

inline QVector<PartitionRule> partitionRulesForPlan(const QString& planUuid)
{
    QVector<PartitionRule> out;
    const QString owner = planUuid.trimmed();
    if (owner.isEmpty()) return out;
    for (const PartitionRule& r : partitionRules())
        if (r.planUuid.trimmed() == owner) out.push_back(r);
    return out;
}

inline void replacePartitionRulesForPlan(const QString& planUuid, QVector<PartitionRule> rules)
{
    const QString owner = planUuid.trimmed();
    if (owner.isEmpty()) return;
    QVector<PartitionRule> merged;
    for (const PartitionRule& r : partitionRules())
        if (r.planUuid.trimmed() != owner) merged.push_back(r);
    for (PartitionRule& r : rules)
    {
        r.planUuid = owner;
        merged.push_back(r);
    }
    setPartitionRules(merged);
}

inline QVector<SweepTask> sweepTasks()
{
    QVector<SweepTask> out;
    const QVariantList list = PublicVariables::instance().get(kSweepTasksKey).toList();
    out.reserve(list.size());
    for (const QVariant& v : list) out.push_back(sweepTaskFromVariant(v.toMap()));
    return out;
}

inline void setSweepTasks(const QVector<SweepTask>& tasks)
{
    QVariantList list;
    list.reserve(tasks.size());
    for (const SweepTask& t : tasks) list.push_back(sweepTaskToVariant(t));
    PublicVariables::instance().set(kSweepTasksKey, list, QStringLiteral("mesh"));
}

inline QVector<SweepTask> sweepTasksForPlan(const QString& planUuid)
{
    QVector<SweepTask> out;
    const QString owner = planUuid.trimmed();
    if (owner.isEmpty()) return out;
    for (const SweepTask& t : sweepTasks())
        if (t.planUuid.trimmed() == owner) out.push_back(t);
    return out;
}

inline void replaceSweepTasksForPlan(const QString& planUuid, QVector<SweepTask> tasks)
{
    const QString owner = planUuid.trimmed();
    if (owner.isEmpty()) return;
    QVector<SweepTask> merged;
    for (const SweepTask& t : sweepTasks())
        if (t.planUuid.trimmed() != owner) merged.push_back(t);
    for (SweepTask& t : tasks)
    {
        t.planUuid = owner;
        merged.push_back(t);
    }
    setSweepTasks(merged);
}

inline QVector<MixedSettings> mixedSettings()
{
    QVector<MixedSettings> out;
    const QVariantList list = PublicVariables::instance().get(kMixedSettingsKey).toList();
    out.reserve(list.size());
    for (const QVariant& v : list) out.push_back(mixedSettingsFromVariant(v.toMap()));
    return out;
}

inline void setMixedSettings(const QVector<MixedSettings>& settings)
{
    QVariantList list;
    list.reserve(settings.size());
    for (const MixedSettings& s : settings) list.push_back(mixedSettingsToVariant(s));
    PublicVariables::instance().set(kMixedSettingsKey, list, QStringLiteral("mesh"));
}

inline MixedSettings mixedSettingsForPlan(const QString& planUuid)
{
    const QString owner = planUuid.trimmed();
    for (const MixedSettings& s : mixedSettings())
        if (!owner.isEmpty() && s.planUuid.trimmed() == owner) return s;
    MixedSettings fallback;
    fallback.planUuid = owner;
    return fallback;
}

inline void setMixedSettingsForPlan(const MixedSettings& settings)
{
    MixedSettings value = settings;
    value.planUuid = value.planUuid.trimmed();
    if (value.planUuid.isEmpty()) return;
    QVector<MixedSettings> merged;
    bool replaced = false;
    for (const MixedSettings& s : mixedSettings())
    {
        if (s.planUuid.trimmed() == value.planUuid)
        {
            if (!replaced) merged.push_back(value);
            replaced = true;
        }
        else
        {
            merged.push_back(s);
        }
    }
    if (!replaced) merged.push_back(value);
    setMixedSettings(merged);
}

inline QVariantList planePayload(const QVector<PartitionRule>& rules)
{
    QVariantList list;
    for (const PartitionRule& r : rules)
    {
        if (!r.enabled) continue;
        QVector3D n = r.planeNormal();
        if (n.lengthSquared() <= 1.0e-16f) continue;
        QVariantMap m;
        m.insert(QStringLiteral("origin"), QVariant::fromValue(r.planeOrigin()));
        m.insert(QStringLiteral("normal"), QVariant::fromValue(n));
        m.insert(QStringLiteral("targetVolumeUiId"), r.targetVolumeUiId);
        list.push_back(m);
    }
    return list;
}

inline QJsonArray partitionRulesToJson(const QVector<PartitionRule>& rules)
{
    QJsonArray arr;
    for (const PartitionRule& r : rules)
    {
        QJsonObject o;
        o[QStringLiteral("plan_uuid")] = r.planUuid;
        o[QStringLiteral("enabled")] = r.enabled;
        o[QStringLiteral("axis")] = r.axis;
        o[QStringLiteral("coordinate")] = r.coordinate;
        const QVector3D p = r.origin, n = r.normal;
        o[QStringLiteral("origin")] = QJsonArray{p.x(), p.y(), p.z()};
        o[QStringLiteral("normal")] = QJsonArray{n.x(), n.y(), n.z()};
        o[QStringLiteral("target_volume_id")] = r.targetVolumeUiId;
        o[QStringLiteral("source_face_id")] = r.sourceFaceUiId;
        o[QStringLiteral("source_edge_id_1")] = r.sourceEdgeUiId1;
        o[QStringLiteral("source_edge_id_2")] = r.sourceEdgeUiId2;
        arr.push_back(o);
    }
    return arr;
}

inline QVector<PartitionRule> partitionRulesFromJson(const QJsonArray& arr)
{
    QVector<PartitionRule> out;
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        PartitionRule r;
        r.planUuid = o.value(QStringLiteral("plan_uuid")).toString().trimmed();
        r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        r.axis = o.value(QStringLiteral("axis")).toInt(0);
        r.coordinate = o.value(QStringLiteral("coordinate")).toDouble(0.0);
        const QJsonArray p = o.value(QStringLiteral("origin")).toArray();
        const QJsonArray n = o.value(QStringLiteral("normal")).toArray();
        if (p.size() == 3) r.origin = QVector3D(float(p[0].toDouble()), float(p[1].toDouble()), float(p[2].toDouble()));
        if (n.size() == 3) r.normal = QVector3D(float(n[0].toDouble()), float(n[1].toDouble()), float(n[2].toDouble()));
        r.targetVolumeUiId = o.value(QStringLiteral("target_volume_id")).toInt(-1);
        r.sourceFaceUiId = o.value(QStringLiteral("source_face_id")).toInt(-1);
        r.sourceEdgeUiId1 = o.value(QStringLiteral("source_edge_id_1")).toInt(-1);
        r.sourceEdgeUiId2 = o.value(QStringLiteral("source_edge_id_2")).toInt(-1);
        out.push_back(r);
    }
    return out;
}

inline QJsonArray sweepTasksToJson(const QVector<SweepTask>& tasks)
{
    QJsonArray arr;
    for (const SweepTask& t : tasks)
    {
        QJsonObject o;
        o[QStringLiteral("plan_uuid")] = t.planUuid;
        o[QStringLiteral("partition_region")] = t.partitionRegion;
        o[QStringLiteral("object_id")] = t.objectId;
        o[QStringLiteral("source_face_id")] = t.sourceFaceId;
        o[QStringLiteral("target_face_id")] = t.targetFaceId;
        o[QStringLiteral("layers")] = t.layers;
        o[QStringLiteral("source_size")] = t.sourceSize;
        o[QStringLiteral("quadrangle_source")] = t.quadrangleSource;
        arr.push_back(o);
    }
    return arr;
}

inline QVector<SweepTask> sweepTasksFromJson(const QJsonArray& arr)
{
    QVector<SweepTask> out;
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        SweepTask t;
        t.planUuid = o.value(QStringLiteral("plan_uuid")).toString().trimmed();
        t.partitionRegion = o.value(QStringLiteral("partition_region")).toBool(false);
        t.objectId = o.value(QStringLiteral("object_id")).toInt(-1);
        t.sourceFaceId = o.value(QStringLiteral("source_face_id")).toInt(-1);
        t.targetFaceId = o.value(QStringLiteral("target_face_id")).toInt(-1);
        t.layers = o.value(QStringLiteral("layers")).toInt(10);
        t.sourceSize = o.value(QStringLiteral("source_size")).toDouble(5.0);
        t.quadrangleSource = o.value(QStringLiteral("quadrangle_source")).toBool(true);
        out.push_back(t);
    }
    return out;
}

inline QJsonArray mixedSettingsToJson(const QVector<MixedSettings>& settings)
{
    QJsonArray arr;
    for (const MixedSettings& s : settings)
    {
        QJsonObject o;
        o[QStringLiteral("plan_uuid")] = s.planUuid;
        o[QStringLiteral("remaining_size")] = s.remainingSize;
        o[QStringLiteral("prefer_hex")] = s.preferHex;
        arr.push_back(o);
    }
    return arr;
}

inline QVector<MixedSettings> mixedSettingsFromJson(const QJsonArray& arr)
{
    QVector<MixedSettings> out;
    for (const QJsonValue& v : arr)
    {
        const QJsonObject o = v.toObject();
        MixedSettings s;
        s.planUuid = o.value(QStringLiteral("plan_uuid")).toString().trimmed();
        s.remainingSize = o.value(QStringLiteral("remaining_size")).toDouble(5.0);
        s.preferHex = o.value(QStringLiteral("prefer_hex")).toBool(true);
        out.push_back(s);
    }
    return out;
}

inline void reset()
{
    setPartitionRules({});
    setSweepTasks({});
    setMixedSettings({});
}
} // namespace MeshSmeshWorkflowState
