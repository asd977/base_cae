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

#include <PublicVariables.h>

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
        if (axis >= 3) return origin;
        if (axis == 0) return QVector3D(float(coordinate), 0.0f, 0.0f);
        if (axis == 1) return QVector3D(0.0f, float(coordinate), 0.0f);
        return QVector3D(0.0f, 0.0f, float(coordinate));
    }

    QVector3D planeNormal() const
    {
        if (axis >= 3)
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

// A user-created Tet operation on the same parent SMESH mesh. The IDs are
// original CAD volume UI IDs, never transient SMESH shape indices.
inline const QString kTetTasksKey = QStringLiteral("mesh.smesh.tet.tasks");
inline const QString kExplicitRegionSelectionKey = QStringLiteral("mesh.smesh.explicit.region.plans");
inline bool explicitRegionSelection(const QString& planUuid)
{
    if (planUuid.isEmpty()) return false;
    return PublicVariables::instance().get(kExplicitRegionSelectionKey).toMap().value(planUuid).toBool();
}
inline void setExplicitRegionSelection(const QString& planUuid, bool enabled)
{
    if (planUuid.isEmpty()) return;
    QVariantMap values = PublicVariables::instance().get(kExplicitRegionSelectionKey).toMap();
    if (enabled) values.insert(planUuid, true);
    else values.remove(planUuid);
    PublicVariables::instance().set(kExplicitRegionSelectionKey, values, QStringLiteral("mesh"));
}

// The Main Shape of the one parent Mesh. This is not a meshing operation:
// selecting geometry here does not request 1D/2D/3D Compute.
inline const QString kMainGeometryScopeKey = QStringLiteral("mesh.smesh.main.geometry.scope");
struct MainGeometryScope
{
    bool all = true; // Current CAD model solids (default)
    bool autoPartition = false; // Explicit opt-in for OCC; native SMESH by default.
    QVector<int> volumeUiIds; // Explicit CAD solid UI IDs (not transient Partition indices)
};
inline MainGeometryScope mainGeometryScopeForPlan(const QString& planUuid)
{
    MainGeometryScope scope;
    if (planUuid.isEmpty()) return scope;
    const QVariantMap record = PublicVariables::instance().get(kMainGeometryScopeKey).toMap()
        .value(planUuid).toMap();
    scope.all = record.value(QStringLiteral("all"), true).toBool();
    scope.autoPartition = record.value(QStringLiteral("autoPartition"), false).toBool();
    const QVariantList ids = record.value(QStringLiteral("volumeUiIds")).toList();
    for (const auto& value : ids) {
        const int id = value.toInt();
        if (id > 0 && !scope.volumeUiIds.contains(id)) scope.volumeUiIds.push_back(id);
    }
    return scope;
}
inline void setMainGeometryScopeForPlan(const QString& planUuid, const MainGeometryScope& scope)
{
    if (planUuid.isEmpty()) return;
    QVariantMap plans = PublicVariables::instance().get(kMainGeometryScopeKey).toMap();
    QVariantMap record;
    record.insert(QStringLiteral("all"), scope.all);
    record.insert(QStringLiteral("autoPartition"), scope.autoPartition);
    QVariantList ids;
    for (int id : scope.volumeUiIds) if (id > 0) ids.push_back(id);
    record.insert(QStringLiteral("volumeUiIds"), ids);
    plans.insert(planUuid, record);
    PublicVariables::instance().set(kMainGeometryScopeKey, plans, QStringLiteral("mesh"));
}
inline QJsonObject mainGeometryScopeToJson(const MainGeometryScope& scope)
{
    QJsonArray ids;
    for (int id : scope.volumeUiIds) if (id > 0) ids.append(id);
    return {{QStringLiteral("all"), scope.all},
            {QStringLiteral("auto_partition"), scope.autoPartition},
            {QStringLiteral("volume_ids"), ids}};
}
inline MainGeometryScope mainGeometryScopeFromJson(const QJsonObject& obj)
{
    MainGeometryScope scope;
    scope.all = obj.value(QStringLiteral("all")).toBool(true);
    scope.autoPartition = obj.value(QStringLiteral("auto_partition")).toBool(false);
    for (const auto& value : obj.value(QStringLiteral("volume_ids")).toArray()) {
        const int id = value.toInt();
        if (id > 0 && !scope.volumeUiIds.contains(id)) scope.volumeUiIds.push_back(id);
    }
    return scope;
}

// MeshRoot is a collection of scoped operations, not an unscoped extra Tet pass.
// The default Tet operation covers either all CAD solids or explicit CAD solid UI IDs.
// Other Tet nodes override the default on their selected solids.
inline const QString kDefaultTetScopeKey = QStringLiteral("mesh.smesh.defaultTetScopes");
struct DefaultTetScope
{
    bool all = true;  // Backwards-compatible default: all solids
    QVector<int> volumeUiIds;
};
inline DefaultTetScope defaultTetScopeForPlan(const QString& planUuid)
{
    DefaultTetScope scope;
    const QVariantMap record = PublicVariables::instance().get(kDefaultTetScopeKey).toMap()
                                    .value(planUuid).toMap();
    scope.all = record.value(QStringLiteral("all"), true).toBool();
    for (const auto& id : record.value(QStringLiteral("volumeUiIds")).toList())
        if (id.toInt() > 0 && !scope.volumeUiIds.contains(id.toInt()))
            scope.volumeUiIds.push_back(id.toInt());
    return scope;
}
inline void setDefaultTetScopeForPlan(const QString& planUuid, const DefaultTetScope& scope)
{
    if (planUuid.isEmpty()) return;
    QVariantMap byPlan = PublicVariables::instance().get(kDefaultTetScopeKey).toMap();
    QVariantMap record;
    record.insert(QStringLiteral("all"), scope.all);
    QVariantList ids;
    for (const int id : scope.volumeUiIds) if (id > 0) ids.push_back(id);
    record.insert(QStringLiteral("volumeUiIds"), ids);
    byPlan.insert(planUuid, record);
    PublicVariables::instance().set(kDefaultTetScopeKey, byPlan, QStringLiteral("mesh"));
}
inline QJsonObject defaultTetScopeToJson(const DefaultTetScope& scope)
{
    QJsonArray ids;
    for (int id : scope.volumeUiIds) if (id > 0) ids.append(id);
    return {{QStringLiteral("all"), scope.all}, {QStringLiteral("volume_ids"), ids}};
}
inline DefaultTetScope defaultTetScopeFromJson(const QJsonObject& obj)
{
    DefaultTetScope scope;
    scope.all = obj.value(QStringLiteral("all")).toBool(true);
    for (const auto& id : obj.value(QStringLiteral("volume_ids")).toArray())
        if (id.toInt() > 0 && !scope.volumeUiIds.contains(id.toInt())) scope.volumeUiIds.push_back(id.toInt());
    return scope;
}

struct TetTask
{
    QString planUuid;
    QString taskUuid;
    QString title;
    QVector<int> volumeUiIds;
    // "all" is a selection mode for this SubMesh, not a separate default Mesh.
    // Explicit selections on other SubMeshes take precedence over this catch-all.
    bool allVolumes = false;
    // <=0: inherit global NETGEN max size. Older projects inherit by default.
    double targetSize = 0.0;
    // 1: native Solid-local surface + volume sizing; 0: legacy volume-only.
    int sizingScope = 1;
    // UI choice only; calculations always consume the resolved explicit sizes.
    // Old project files without this field remain Custom.
    QString densityPreset = QStringLiteral("custom");
    // -1 inherits legacy Mesh 1 settings. New/edited nodes save explicit values.
    int fineness = -1;
    int surfaceCurvature = -1;
    int chordalErrorEnabled = -1; // -1 inherits parent; 0 off; 1 local constraint
    double chordalError = 1.0;
    int optimize = -1;
    int automaticMinSize = -1;
    double manualMinSize = 0.0;
    // Custom per-domain NETGEN controls. A negative value inherits from the parent.
    double growthRate = -1.0;
    double nbSegPerEdge = -1.0;
    double nbSegPerRadius = -1.0;
};

struct ResolvedTetTasks
{
    QVector<TetTask> tasks;
    QString error;
};
// One assignment policy for pure and mixed workflows. A catch-all SubMesh
// retains its own parameters and takes only the domains not explicitly owned.
inline ResolvedTetTasks resolveTetTasks(const QVector<TetTask>& input,
    const QSet<int>& available, const QSet<int>& excluded = {}, bool allowEmptyTasks = false)
{
    ResolvedTetTasks result;
    QSet<int> occupied=excluded;
    const TetTask* fallback=nullptr;
    for(const auto& task:input) {
        if(task.allVolumes) {
            if(fallback) { result.error=QStringLiteral("仅允许一个 SubMesh 使用全部未分配实体。");return result; }
            fallback=&task;continue;
        }
        if(task.volumeUiIds.isEmpty()) {
            // A Sweep operation may own temporary OCC Partition Regions instead
            // of original CAD volume IDs.  An unassigned Tet operation does no
            // work and must not block that valid Sweep-only configuration.
            if (allowEmptyTasks || !excluded.isEmpty()) continue;
            result.error=QStringLiteral("网格 %1 没有选择实体。").arg(task.title);return result;
        }
        for(int id:task.volumeUiIds) {
            if(!available.contains(id) || occupied.contains(id)) {
                result.error=QStringLiteral("实体 %1 不属于父 Mesh，或已分配给其他 SubMesh。").arg(id);return result;
            }
            occupied.insert(id);
        }
        result.tasks.append(task);
    }
    if(fallback) {
        auto task=*fallback;task.volumeUiIds.clear();
        for(int id:available)if(!occupied.contains(id))task.volumeUiIds.append(id);
        std::sort(task.volumeUiIds.begin(),task.volumeUiIds.end());
        if(!task.volumeUiIds.isEmpty())result.tasks.append(task);
    }
    return result;
}

inline QVariantMap tetTaskToVariant(const TetTask& t)
{
    QVariantMap m;
    m.insert(QStringLiteral("planUuid"), t.planUuid);
    m.insert(QStringLiteral("taskUuid"), t.taskUuid);
    m.insert(QStringLiteral("title"), t.title);
    m.insert(QStringLiteral("allVolumes"), t.allVolumes);
    m.insert(QStringLiteral("targetSize"), t.targetSize);
    m.insert(QStringLiteral("sizingScope"), t.sizingScope);
    m.insert(QStringLiteral("densityPreset"), t.densityPreset);
    m.insert(QStringLiteral("fineness"), t.fineness);
    m.insert(QStringLiteral("surfaceCurvature"),t.surfaceCurvature);
    m.insert(QStringLiteral("chordalErrorEnabled"), t.chordalErrorEnabled);
    m.insert(QStringLiteral("chordalError"), t.chordalError);
    m.insert(QStringLiteral("optimize"), t.optimize);
    m.insert(QStringLiteral("automaticMinSize"), t.automaticMinSize);
    m.insert(QStringLiteral("manualMinSize"), t.manualMinSize);
    m.insert(QStringLiteral("growthRate"), t.growthRate);
    m.insert(QStringLiteral("nbSegPerEdge"), t.nbSegPerEdge);
    m.insert(QStringLiteral("nbSegPerRadius"), t.nbSegPerRadius);
    QVariantList ids;
    for (int id : t.volumeUiIds) ids.push_back(id);
    m.insert(QStringLiteral("volumeUiIds"), ids);
    return m;
}
inline TetTask tetTaskFromVariant(const QVariantMap& m)
{
    TetTask t;
    t.planUuid = m.value(QStringLiteral("planUuid")).toString().trimmed();
    t.taskUuid = m.value(QStringLiteral("taskUuid")).toString().trimmed();
    t.title = m.value(QStringLiteral("title")).toString();
    t.allVolumes = m.value(QStringLiteral("allVolumes"), false).toBool();
    t.targetSize = m.value(QStringLiteral("targetSize"), 0.0).toDouble();
    t.sizingScope = m.value(QStringLiteral("sizingScope"), 0).toInt() == 1 ? 1 : 0;
    t.densityPreset = m.value(QStringLiteral("densityPreset"), QStringLiteral("custom")).toString();
    t.fineness = m.value(QStringLiteral("fineness"), -1).toInt();
    t.surfaceCurvature = m.value(QStringLiteral("surfaceCurvature"),-1).toInt();
    t.chordalErrorEnabled = m.value(QStringLiteral("chordalErrorEnabled"), -1).toInt();
    t.chordalError = m.value(QStringLiteral("chordalError"), 1.0).toDouble();
    t.optimize = m.value(QStringLiteral("optimize"), -1).toInt();
    t.automaticMinSize = m.value(QStringLiteral("automaticMinSize"), -1).toInt();
    t.manualMinSize = m.value(QStringLiteral("manualMinSize"), 0.0).toDouble();
    t.growthRate = m.value(QStringLiteral("growthRate"), -1.0).toDouble();
    t.nbSegPerEdge = m.value(QStringLiteral("nbSegPerEdge"), -1.0).toDouble();
    t.nbSegPerRadius = m.value(QStringLiteral("nbSegPerRadius"), -1.0).toDouble();
    for (const QVariant& id : m.value(QStringLiteral("volumeUiIds")).toList())
        if (id.toInt() > 0 && !t.volumeUiIds.contains(id.toInt())) t.volumeUiIds.push_back(id.toInt());
    return t;
}
inline QVector<TetTask> tetTasks()
{
    QVector<TetTask> tasks;
    for (const QVariant& value : PublicVariables::instance().get(kTetTasksKey).toList())
        tasks.push_back(tetTaskFromVariant(value.toMap()));
    return tasks;
}
inline void setTetTasks(const QVector<TetTask>& tasks)
{
    QVariantList list;
    for (const auto& task : tasks) list.push_back(tetTaskToVariant(task));
    PublicVariables::instance().set(kTetTasksKey, list, QStringLiteral("mesh"));
}
inline QVector<TetTask> tetTasksForPlan(const QString& planUuid)
{
    QVector<TetTask> out;
    for (const auto& task : tetTasks())
        if (!planUuid.isEmpty() && task.planUuid == planUuid) out.push_back(task);
    return out;
}
inline void upsertTetTask(const TetTask& task)
{
    if (task.planUuid.isEmpty() || task.taskUuid.isEmpty()) return;
    auto list = tetTasks();
    bool found = false;
    for (auto& item : list)
        if (item.planUuid == task.planUuid && item.taskUuid == task.taskUuid)
        { item = task; found = true; break; }
    if (!found) list.push_back(task);
    setTetTasks(list);
}
inline QJsonArray tetTasksToJson(const QVector<TetTask>& tasks)
{
    QJsonArray out;
    for (const auto& task : tasks)
    {
        QJsonObject obj;
        obj[QStringLiteral("plan_uuid")] = task.planUuid;
        obj[QStringLiteral("task_uuid")] = task.taskUuid;
        obj[QStringLiteral("title")] = task.title;
        obj[QStringLiteral("all_volumes")] = task.allVolumes;
        obj[QStringLiteral("target_size")] = task.targetSize;
        obj[QStringLiteral("sizing_scope")] = task.sizingScope;
        obj[QStringLiteral("density_preset")] = task.densityPreset;
        obj[QStringLiteral("fineness")] = task.fineness;
        obj[QStringLiteral("surface_curvature")] = task.surfaceCurvature;
        obj[QStringLiteral("chordal_error_enabled")] = task.chordalErrorEnabled;
        obj[QStringLiteral("chordal_error")] = task.chordalError;
        obj[QStringLiteral("optimize")] = task.optimize;
        obj[QStringLiteral("automatic_min_size")] = task.automaticMinSize;
        obj[QStringLiteral("manual_min_size")] = task.manualMinSize;
        obj[QStringLiteral("growth_rate")] = task.growthRate;
        obj[QStringLiteral("nb_seg_per_edge")] = task.nbSegPerEdge;
        obj[QStringLiteral("nb_seg_per_radius")] = task.nbSegPerRadius;
        QJsonArray ids;
        for (int id : task.volumeUiIds) ids.push_back(id);
        obj[QStringLiteral("volume_ids")] = ids;
        out.push_back(obj);
    }
    return out;
}
inline QVector<TetTask> tetTasksFromJson(const QJsonArray& array)
{
    QVector<TetTask> out;
    for (const auto& value : array)
    {
        const auto obj = value.toObject();
        TetTask task;
        task.planUuid = obj.value(QStringLiteral("plan_uuid")).toString();
        task.taskUuid = obj.value(QStringLiteral("task_uuid")).toString();
        task.title = obj.value(QStringLiteral("title")).toString();
        task.allVolumes = obj.value(QStringLiteral("all_volumes")).toBool(false);
        task.targetSize = obj.value(QStringLiteral("target_size")).toDouble(0.0);
        task.sizingScope = obj.value(QStringLiteral("sizing_scope")).toInt(0) == 1 ? 1 : 0;
        task.densityPreset = obj.value(QStringLiteral("density_preset")).toString(QStringLiteral("custom"));
        task.fineness = obj.value(QStringLiteral("fineness")).toInt(-1);
        task.surfaceCurvature = obj.value(QStringLiteral("surface_curvature")).toInt(-1);
        task.chordalErrorEnabled = obj.value(QStringLiteral("chordal_error_enabled")).toInt(-1);
        task.chordalError = obj.value(QStringLiteral("chordal_error")).toDouble(1.0);
        task.optimize = obj.value(QStringLiteral("optimize")).toInt(-1);
        task.automaticMinSize = obj.value(QStringLiteral("automatic_min_size")).toInt(-1);
        task.manualMinSize = obj.value(QStringLiteral("manual_min_size")).toDouble(0.0);
        task.growthRate = obj.value(QStringLiteral("growth_rate")).toDouble(-1.0);
        task.nbSegPerEdge = obj.value(QStringLiteral("nb_seg_per_edge")).toDouble(-1.0);
        task.nbSegPerRadius = obj.value(QStringLiteral("nb_seg_per_radius")).toDouble(-1.0);
        for (const auto& id : obj.value(QStringLiteral("volume_ids")).toArray())
            if (id.toInt() > 0 && !task.volumeUiIds.contains(id.toInt())) task.volumeUiIds.push_back(id.toInt());
        if (!task.taskUuid.isEmpty()) out.push_back(task);
    }
    return out;
}

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
        m.insert(QStringLiteral("axis"), r.axis);
        if (r.axis == 4)
            m.insert(QStringLiteral("radius"), r.coordinate);
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
    setTetTasks({});
    PublicVariables::instance().set(kExplicitRegionSelectionKey, QVariantMap{}, QStringLiteral("mesh"));
    setMixedSettings({});
}
} // namespace MeshSmeshWorkflowState
