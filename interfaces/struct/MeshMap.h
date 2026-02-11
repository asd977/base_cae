#ifndef MESH_H
#define MESH_H
#include <QHash.h>

struct MshPickMap
{
    // ----- PhysicalNames -----
    QHash<int, QString> physIdToName;   // physicalId -> 物理组名字（如 "face_1"）
    QHash<int, int>     physIdToDim;    // physicalId -> 维度（0点 1线 2面 3体）

    // ----- Node 映射 -----
    QHash<int, int> nodeIdToPointId;    // msh NodeId -> vtk pointId（用 msh 元素找 vtk 点）
    QHash<int, int> pointIdToNodeId;    // vtk pointId -> msh NodeId（UI显示 nodeId / 写文件用）

    // ----- 体单元分组（下拉框“选体”常用） -----
    QHash<int, QVector<qint64>> volPhysToCellIds; // physicalId(dim=3) -> vtk cellId 列表（qint64存）

    // ----- 边界：用于“选点” -----
    QHash<int, QVector<int>> bndPointPhysToPointIds; // physicalId(dim=0) -> vtk pointId 列表
    QHash<int, QVector<int>> bndEdgePhysToPointIds;  // physicalId(dim=1) -> vtk pointId 列表（线的端点集合）
    QHash<int, QVector<int>> bndFacePhysToPointIds;  // physicalId(dim=2) -> vtk pointId 列表（面的角点集合）

    // ----- 边界：用于“高亮线/面” -----
    QHash<int, QVector<qint64>> bndEdgePhysToEdgeKeys; // physicalId(dim=1) -> edgeKey 列表
    QHash<int, QVector<qint64>> bndFacePhysToFaceKeys; // physicalId(dim=2) -> faceKey 列表
};



#endif // MESH_H
