#ifndef MESH_H
#define MESH_H

#include <QHash>
#include <QString>
#include <QVector>
#include <QtGlobal>
#include <vtkType.h>

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

    QVector<vtkIdType> pointIds;
    QVector<vtkIdType> cellIds;
    QVector<vtkIdType> edgeKeys;
    QVector<vtkIdType> faceKeys;
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

    // ----- 体单元分组（下拉框“选体”常用） -----
    QHash<int, QVector<qint64>>
        volPhysToCellIds; // physicalId(dim=3) -> vtk cellId 列表（qint64存）

    // ----- 网格分组（统一存储） -----
    QHash<qint64, int> cellIdToMatype;           // vtk cellId -> MATYPE
    QHash<qint64, int> cellIdToLayer;            // vtk cellId -> LAYER
    QHash<qint64, QString> cellIdToGroupName;    // vtk cellId -> GroupName
    QHash<int, QVector<qint64>> matypeToCellIds; // MATYPE -> cellIds
    QHash<int, QVector<qint64>> layerToCellIds;  // LAYER  -> cellIds

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
    QHash<vtkIdType, QVector<MeshTagKey>> cellIdToDomainTags;
    QHash<vtkIdType, QVector<MeshTagKey>> pointIdToBoundaryTags;
};

Q_DECLARE_METATYPE(MeshTagKey)

#endif // MESH_H
