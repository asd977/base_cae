#ifndef MESH_H
#define MESH_H
#include <QHash.h>

struct MshPickMap
{
    // =======================
    // 1) Physical 分组元信息（用于下拉框显示）
    // =======================

    // physicalId -> 分组名字（来自 $PhysicalNames；没有则自动补 "phys_<id>"）
    QHash<int, QString> physIdToName;

    // physicalId -> 维度 dim（0点 / 1线 / 2面 / 3体）
    // 优先来自 $PhysicalNames；没有则按元素类型推断
    QHash<int, int> physIdToDim;

    // =======================
    // 2) 节点映射（msh NodeId -> vtk pointId）
    // =======================

    // msh 节点编号 NodeId -> VTK 点编号 pointId（用于从 msh nodeId 快速定位 VTK 点）
    QHash<int, int> nodeIdToPointId;

    // =======================
    // 3) 体单元映射（用于“选体”高亮体单元）
    // =======================

    // physicalId(体分组) -> 该分组下所有体单元的 vtkCellId 列表
    // 直接用于：下拉框选 volume_xxx -> 高亮这些 cell
    QHash<int, QVector<int>> volPhysToCellIds;

    // =======================
    // 4) 边界映射（用于“选面/线/点”高亮点 或 写出边界）
    // =======================

    // ---- dim=0 点分组 ----
    // physicalId(点分组) -> msh NodeId 列表（去重后）
    QHash<int, QVector<int>> bndPointPhysToNodeIds;
    // physicalId(点分组) -> vtk pointId 列表（与上面对应，直接用于高亮点）
    QHash<int, QVector<int>> bndPointPhysToPointIds;

    // ---- dim=1 线分组 ----
    QHash<int, QVector<int>> bndEdgePhysToNodeIds;   // 写出/求解器通常用 msh nodeId
    QHash<int, QVector<int>> bndEdgePhysToPointIds;  // 显示高亮用 vtk pointId

    // ---- dim=2 面分组 ----
    QHash<int, QVector<int>> bndFacePhysToNodeIds;
    QHash<int, QVector<int>> bndFacePhysToPointIds;

    // =======================
    // 5) 体单元属性（可选，但很常用：后续定位/反查用）
    // =======================

    // vtkCellId -> msh elemId（用于“点了一个体单元，知道它原来的 element id”）
    QHash<int, int> cellIdToElemId;

    // vtkCellId -> gmsh elemType（4=tet4, 5=hex8...）
    QHash<int, int> cellIdToGmshType;

    // vtkCellId -> physicalId（它属于哪个 physical 分组）
    QHash<int, int> cellIdToPhysId;

    // vtkCellId -> entityId（msh2.2 tags 第二个一般是 elementary entity）
    QHash<int, int> cellIdToEntityId;

    // =======================
    // 6) 清理用（你也可以不写方法，外面直接 map = {}）
    // =======================

    // （不放方法）需要清理时：MshPickMap tmp; m_lastPickMap = tmp;
};



#endif // MESH_H
