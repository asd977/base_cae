#ifndef MESH_H
#define MESH_H

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <algorithm>

using MeshIdType = qint64;

struct MeshTagKey
{
    QString category;
    int dim = -1;
    int id = 0;

    bool isValid() const
    {
        return !category.trimmed().isEmpty() && dim >= 0 && dim <= 3 && id > 0;
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

    // ----- PhysicalNames -----
    QHash<int, QString> physIdToName; // physicalId -> 物理组名字（如 "face_1"）
    QHash<int, int> physIdToDim;      // physicalId -> 维度（0点 1线 2面 3体）

    // ----- Node 映射 -----
    QHash<int, int> nodeIdToPointId; // msh NodeId -> vtk pointId（用 msh 元素找 vtk 点）
    QHash<int, int> pointIdToNodeId; // vtk pointId -> msh NodeId（UI显示 nodeId / 写文件用）

    // ----- 边界：用于“选点” -----
    QHash<int, QVector<int>> bndPointPhysToPointIds; // physicalId(dim=0) -> vtk pointId 列表
    QHash<int, QVector<int>> bndEdgePhysToPointIds;  // physicalId(dim=1) -> vtk pointId
                                                     // 列表（线的端点集合）
    QHash<int, QVector<int>> bndFacePhysToPointIds;  // physicalId(dim=2) -> vtk pointId
                                                     // 列表（面的角点集合）

    // ----- 边界：用于“高亮线/面” -----
    QHash<int, QVector<qint64>> bndEdgePhysToEdgeKeys; // physicalId(dim=1) -> edgeKey 列表
    QHash<int, QVector<qint64>> bndFacePhysToFaceKeys; // physicalId(dim=2) -> faceKey 列表

    // ----- 新统一分组模型 -----
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
};

Q_DECLARE_METATYPE(MeshTagKey)

#endif // MESH_H
