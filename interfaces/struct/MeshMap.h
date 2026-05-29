#ifndef MESH_H
#define MESH_H

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <QSet>

using MeshIdType = qint64;

struct MeshTagKey
{
    QString category;
    int dim = -1;
    int id = 0;

    bool isValid() const
    {
        return !category.trimmed().isEmpty() && dim >= 0 && dim <= 3 && id != 0;
    }

    bool operator==(const MeshTagKey& other) const
    {
        return category == other.category && dim == other.dim && id == other.id;
    }
};

inline uint qHash(const MeshTagKey& key, uint seed = 0)
{
    seed = qHash(key.category, seed);
    seed = qHash(key.dim, seed);
    seed = qHash(key.id, seed);
    return seed;
}

struct MeshTagSet
{
    MeshTagKey key;
    QString name;
    QString source;

    QVector<MeshIdType> pointIds;
    QVector<MeshIdType> cellIds;
    QVector<MeshIdType> edgeKeys;
    QVector<MeshIdType> faceKeys;
};

struct MshPickMap
{
    int meshDim = 3;

    // ----- 兼容字段：PhysicalNames -----
    // 注意：业务层/界面层不要直接依赖这些字段。
    // 统一的分组与边界业务模型是 tagSets。
    QHash<int, QString> physIdToName; // physicalId -> 物理组名字
    QHash<int, int> physIdToDim;      // physicalId -> 维度（0点 1线 2面 3体）

    // ----- Node 映射 -----
    QHash<int, int> nodeIdToPointId; // msh NodeId -> vtk pointId（用 msh 元素找 vtk 点）
    QHash<int, int> pointIdToNodeId; // vtk pointId -> msh NodeId（UI显示 nodeId / 写文件用）

    // ----- 唯一业务分组模型 -----
    // 域、边界点、边界线、边界面都统一落到这里。
    // 页面层应只读取 MeshModuleController::boundaryTagSets()/domainTagSets()，不要直接读 bndXXX。
    QHash<MeshTagKey, MeshTagSet> tagSets;
    QHash<MeshIdType, QVector<MeshTagKey>> cellIdToDomainTags;
    QHash<MeshIdType, QVector<MeshTagKey>> pointIdToBoundaryTags;

    QVector<MeshTagSet> physicalTagSets(int dimFilter = -1) const
    {
        QVector<MeshTagSet> out;
        out.reserve(tagSets.size());
        for (auto it = tagSets.constBegin(); it != tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (set.key.category != QStringLiteral("Physical"))
                continue;
            if (dimFilter >= 0 && set.key.dim != dimFilter)
                continue;
            out.push_back(set);
        }
        std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
                  {
                      if (a.key.dim != b.key.dim)
                          return a.key.dim < b.key.dim;
                      return a.key.id < b.key.id;
                  });
        return out;
    }

    QString physicalName(int physId, int dim = -1) const
    {
        if (physId <= 0)
            return {};
        if (dim >= 0)
        {
            const MeshTagKey key{QStringLiteral("Physical"), dim, physId};
            if (tagSets.contains(key))
                return tagSets.value(key).name.trimmed();
        }
        for (int d = 0; d <= 3; ++d)
        {
            const MeshTagKey key{QStringLiteral("Physical"), d, physId};
            if (!tagSets.contains(key))
                continue;
            const QString n = tagSets.value(key).name.trimmed();
            if (!n.isEmpty())
                return n;
        }
        return {};
    }

    int physicalDim(int physId) const
    {
        if (physId <= 0)
            return -1;
        for (int d = 0; d <= 3; ++d)
        {
            const MeshTagKey key{QStringLiteral("Physical"), d, physId};
            if (tagSets.contains(key))
                return d;
        }
        return -1;
    }

    QVector<int> physicalIds(int dimFilter = -1) const
    {
        QVector<int> ids;
        QSet<int> seen;
        for (auto it = tagSets.constBegin(); it != tagSets.constEnd(); ++it)
        {
            const MeshTagKey& key = it.key();
            if (key.category != QStringLiteral("Physical"))
                continue;
            if (dimFilter >= 0 && key.dim != dimFilter)
                continue;
            if (!seen.contains(key.id))
            {
                seen.insert(key.id);
                ids.push_back(key.id);
            }
        }
        std::sort(ids.begin(), ids.end());
        return ids;
    }

    QVector<int> physicalPointIds(int physId, int dim) const
    {
        if (physId <= 0 || dim < 0 || dim > 2)
            return {};
        const MeshTagKey key{QStringLiteral("Physical"), dim, physId};
        if (!tagSets.contains(key))
            return {};
        QVector<int> out;
        out.reserve(tagSets.value(key).pointIds.size());
        for (MeshIdType v : tagSets.value(key).pointIds)
            out.push_back(static_cast<int>(v));
        return out;
    }

    QVector<qint64> physicalEdgeKeys(int physId) const
    {
        const MeshTagKey key{QStringLiteral("Physical"), 1, physId};
        if (tagSets.contains(key))
            return tagSets.value(key).edgeKeys;
        return {};
    }

    QVector<qint64> physicalFaceKeys(int physId) const
    {
        const MeshTagKey key{QStringLiteral("Physical"), 2, physId};
        if (tagSets.contains(key))
            return tagSets.value(key).faceKeys;
        return {};
    }

    QHash<int, QString> physicalNameMap() const
    {
        QHash<int, QString> out;
        for (const MeshTagSet& set : physicalTagSets())
        {
            if (set.name.trimmed().isEmpty())
                continue;
            out.insert(set.key.id, set.name.trimmed());
        }
        return out;
    }

    QHash<int, int> physicalDimMap() const
    {
        QHash<int, int> out;
        for (const MeshTagSet& set : physicalTagSets())
            out.insert(set.key.id, set.key.dim);
        return out;
    }

    QHash<int, QVector<qint64>> physicalEdgeKeyMap() const
    {
        QHash<int, QVector<qint64>> out;
        for (const MeshTagSet& set : physicalTagSets(1))
        {
            if (!set.edgeKeys.isEmpty())
                out.insert(set.key.id, set.edgeKeys);
        }
        return out;
    }

    QHash<int, QVector<qint64>> physicalFaceKeyMap() const
    {
        QHash<int, QVector<qint64>> out;
        for (const MeshTagSet& set : physicalTagSets(2))
        {
            if (!set.faceKeys.isEmpty())
                out.insert(set.key.id, set.faceKeys);
        }
        return out;
    }
};

Q_DECLARE_METATYPE(MeshTagKey)

#endif // MESH_H
