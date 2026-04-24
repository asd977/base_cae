#ifndef MESH_H
#define MESH_H
#include <QHash>
#include <QSet>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <vtkType.h>

enum class MeshGroupKind
{
    Domain = 0,
    BoundaryPoint = 1,
    BoundaryEdge = 2,
    BoundaryFace = 3
};

struct MeshGroupInfo
{
    int id = 0;
    QString name;
    MeshGroupKind kind = MeshGroupKind::Domain;
    QString source;
    int sourceId = 0;
    int dim = -1;
};

struct IiinLegacyTags
{
    bool valid = false;
    QSet<int> matypeValues;
    QSet<int> layerValues;
    QHash<vtkIdType, int> cellIdToMatype;
    QHash<vtkIdType, int> cellIdToLayer;
    QHash<int, QVector<qint64>> matypeToCellIds;
    QHash<int, QVector<qint64>> layerToCellIds;
};

struct MshPickMap
{
    QHash<int, MeshGroupInfo> groupInfos;

    QHash<int, QVector<qint64>> domainGroupToCellIds;

    QHash<int, QVector<int>> pointGroupToPointIds;
    QHash<int, QVector<int>> edgeGroupToPointIds;
    QHash<int, QVector<int>> faceGroupToPointIds;

    QHash<int, QVector<qint64>> edgeGroupToEdgeKeys;
    QHash<int, QVector<qint64>> faceGroupToFaceKeys;

    QHash<int, int> nodeIdToPointId;
    QHash<int, int> pointIdToNodeId;

    IiinLegacyTags iiin;

    QHash<int, QString> physIdToName;
    QHash<int, int> physIdToDim;

    QHash<int, QVector<qint64>> volPhysToCellIds;

    QHash<int, QVector<int>> bndPointPhysToPointIds;
    QHash<int, QVector<int>> bndEdgePhysToPointIds;
    QHash<int, QVector<int>> bndFacePhysToPointIds;

    QHash<int, QVector<qint64>> bndEdgePhysToEdgeKeys;
    QHash<int, QVector<qint64>> bndFacePhysToFaceKeys;

    QHash<vtkIdType, int> cellIdToMatype;
    QHash<vtkIdType, int> cellIdToLayer;
    QHash<vtkIdType, QString> cellIdToGroupName;
    QHash<int, QVector<qint64>> matypeToCellIds;
    QHash<int, QVector<qint64>> layerToCellIds;
};

#endif // MESH_H
