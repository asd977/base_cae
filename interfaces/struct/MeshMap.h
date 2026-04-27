#ifndef MESH_H
#define MESH_H

#include <QHash>
#include <QMetaType>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <algorithm>

using MeshIdType = qint64;

struct MeshTagKey
{
    QString category; // MATYPE / LAYER / Physical / User...
    int dim = -1;     // 0 point, 1 edge, 2 face, 3 volume
    int id = 0;

    bool isValid() const
    {
        return !category.trimmed().isEmpty() && dim >= 0 && dim <= 3 && id > 0;
    }

    bool isDomain() const
    {
        return isValid() && dim == 3;
    }

    bool isBoundary() const
    {
        return isValid() && dim >= 0 && dim <= 2;
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

    QString displayName() const
    {
        const QString n = name.trimmed();
        if (!n.isEmpty())
            return n;
        return QStringLiteral("%1=%2").arg(key.category).arg(key.id);
    }
};

struct MshPickMap
{
    int meshDim = 3;

           // msh node id <-> vtk point id. This is not a grouping model.
    QHash<int, MeshIdType> nodeIdToPointId;
    QHash<MeshIdType, int> pointIdToNodeId;

           // Unified grouping model. Domains and boundaries are both MeshTagSet.
    QHash<MeshTagKey, MeshTagSet> tagSets;

           // Reverse indexes for fast query/highlight.
    QHash<MeshIdType, QVector<MeshTagKey>> cellIdToDomainTags;
    QHash<MeshIdType, QVector<MeshTagKey>> pointIdToBoundaryTags;
    QHash<MeshIdType, QVector<MeshTagKey>> edgeKeyToBoundaryTags;
    QHash<MeshIdType, QVector<MeshTagKey>> faceKeyToBoundaryTags;

    void clear()
    {
        meshDim = 3;
        nodeIdToPointId.clear();
        pointIdToNodeId.clear();
        tagSets.clear();
        cellIdToDomainTags.clear();
        pointIdToBoundaryTags.clear();
        edgeKeyToBoundaryTags.clear();
        faceKeyToBoundaryTags.clear();
    }

    MeshTagSet* ensureTagSet(const MeshTagKey& key,
                             const QString& name = QString(),
                             const QString& source = QString())
    {
        if (!key.isValid())
            return nullptr;

        if (!tagSets.contains(key))
        {
            MeshTagSet set;
            set.key = key;
            set.name = name;
            set.source = source;
            tagSets.insert(key, set);
        }
        else
        {
            MeshTagSet& set = tagSets[key];
            if (set.name.trimmed().isEmpty() && !name.trimmed().isEmpty())
                set.name = name;
            if (set.source.trimmed().isEmpty() && !source.trimmed().isEmpty())
                set.source = source;
        }

        return &tagSets[key];
    }

    QVector<MeshTagSet> domainTagSets(const QString& categoryFilter = QString()) const
    {
        QVector<MeshTagSet> out;
        out.reserve(tagSets.size());

        for (auto it = tagSets.constBegin(); it != tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (!set.key.isDomain())
                continue;
            if (!categoryFilter.isEmpty() && set.key.category != categoryFilter)
                continue;
            out.push_back(set);
        }

        std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
                  {
                      if (a.key.category != b.key.category)
                          return a.key.category < b.key.category;
                      return a.key.id < b.key.id;
                  });

        return out;
    }

    QVector<MeshTagSet> boundaryTagSets(int dimFilter = -1,
                                        const QString& categoryFilter = QString()) const
    {
        QVector<MeshTagSet> out;
        out.reserve(tagSets.size());

        for (auto it = tagSets.constBegin(); it != tagSets.constEnd(); ++it)
        {
            const MeshTagSet& set = it.value();
            if (!set.key.isBoundary())
                continue;
            if (dimFilter >= 0 && set.key.dim != dimFilter)
                continue;
            if (!categoryFilter.isEmpty() && set.key.category != categoryFilter)
                continue;
            out.push_back(set);
        }

        std::sort(out.begin(), out.end(), [](const MeshTagSet& a, const MeshTagSet& b)
                  {
                      if (a.key.dim != b.key.dim)
                          return a.key.dim < b.key.dim;
                      if (a.key.category != b.key.category)
                          return a.key.category < b.key.category;
                      return a.key.id < b.key.id;
                  });

        return out;
    }
};

Q_DECLARE_METATYPE(MeshTagKey)
Q_DECLARE_METATYPE(MeshTagSet)

#endif // MESH_H
