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

enum class MeshGroupSource
{
    Unknown = 0,

    IiinMatype,
    IiinLayer,

    GmshPhysicalVolume,
    GmshPhysicalSurface,
    GmshPhysicalCurve,
    GmshPhysicalPoint,

    AbaqusElset,
    AbaqusNset,

    FlacZoneGroup,

    User
};

struct MeshGroupInfo
{
    int id = 0;
    QString name;

    MeshGroupKind kind = MeshGroupKind::Domain;
    MeshGroupSource source = MeshGroupSource::Unknown;

           // 点0、线1、面2、体3
    int dim = -1;

           // 原始编号，例如 MATYPE=3、LAYER=2、Gmsh PhysicalId=5
    int sourceId = 0;
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
    // 所有组的元信息
    QHash<int, MeshGroupInfo> groups;

           // 域分组：材料、开挖、体控制都读这个
    QHash<int, QVector<qint64>> domainGroups;

           // 边界分组：约束、荷载读这些
    QHash<int, QVector<int>> pointBoundaryGroups;
    QHash<int, QVector<int>> edgeBoundaryPointGroups;
    QHash<int, QVector<int>> faceBoundaryPointGroups;

    QHash<int, QVector<qint64>> edgeBoundaryKeys;
    QHash<int, QVector<qint64>> faceBoundaryKeys;

           // 节点映射
    QHash<int, int> pointIdToNodeId;
    QHash<int, int> nodeIdToPointId;

    void clear()
    {
        groups.clear();
        domainGroups.clear();

        pointBoundaryGroups.clear();
        edgeBoundaryPointGroups.clear();
        faceBoundaryPointGroups.clear();

        edgeBoundaryKeys.clear();
        faceBoundaryKeys.clear();

        pointIdToNodeId.clear();
        nodeIdToPointId.clear();
    }
};
#endif // MESH_H
