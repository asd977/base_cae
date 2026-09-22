#ifndef GEOMETRYENTITYTOOLS_H
#define GEOMETRYENTITYTOOLS_H

#include <QList>
#include <QStringList>
#include <QVector3D>
#include <QVariantMap>
#include <QVariant>
#include <TopoDS_Shape.hxx>

namespace GeometryEntityTools
{
QStringList listEntityIds(const QStringList& geometryUuids, int dim);
bool entityCenter(const QStringList& geometryUuids, int dim, int id, QVector3D& center);
bool entityNormal(const QStringList& geometryUuids, int dim, int id, QVector3D& normal);
// 仅当目标是直线 Edge 时返回该边的代表点和单位方向。
bool entityLine(const QStringList& geometryUuids, int dim, int id, QVector3D& origin, QVector3D& direction);
// Hex Blocking 半自动分析：返回直线边及其相邻面的拓扑特征。
// 结果包含 valid / transitionPlanarCurved / transitionPlanarPlanar / planarNormals / adjacentFaceIds 等字段。
QVariantMap edgeBlockingInfo(const QStringList& geometryUuids, int id);
// OCC Sweep 分解补充：对圆/椭圆或近似共面的曲线特征边返回其真实载平面。
// 该平面只作为自动 Region 候选，后续仍必须通过 OCC Split 预检；不会直接修改 CAD。
QVariantMap edgeSweepFeaturePlane(const QStringList& geometryUuids, int id);
// OCC Sweep 自动分解第一关：判断当前单实体是否是严格的平移 Sweep。
// 返回 {valid, sweepable, sourceFaceId, targetFaceId, translation, profileVertexCount, message}。
// 这里只接受“原 CAD 边界可由一个端面精确平移得到”的高置信度情况；
// 不做近似拟合，因此命中后 Mesh 可以安全使用 Pave-Sweep 而不改变 CAD 外形。
QVariantMap detectTranslationalPaveSweep(const QStringList& geometryUuids);
// Sweep 交互自动分析：针对一个已经存在的 OCC Solid，枚举可精确平移对应的 Source/Target 候选。
// 返回 candidates；每个候选只包含 source/target、translation、direction、distance。
// 若长方体等存在多组合法方向，会全部返回而不擅自替用户选择。
QVariantMap analyzeTranslationalSweepShape(const TopoDS_Shape& solid);
// 针对全局 CAD Volume UI ID 的包装版本，候选 Face ID 直接映射到当前全局 UI ID 空间。
QVariantMap analyzeTranslationalSweepVolume(const QStringList& geometryUuids, int volumeUiId);
// 批量版本：一次建立全局 Solid/Face 索引，避免上百个 Volume 逐个重复扫描整个装配。
QVariantList analyzeTranslationalSweepVolumes(const QStringList& geometryUuids, const QList<int>& volumeUiIds);
// Hex Blocking 半自动拓扑预检：在 OCC 临时结果中实际应用切面，
// 返回分块后的 Volume 数、每块面数及是否满足 6 面四边 Sweep 拓扑。
QVariantMap evaluateHexBlockingPlanes(const QStringList& geometryUuids, const QVariantList& planes);
// Build the exact temporary OCC child solids produced by the same Mesh-only cut planes.
// Used only for viewport Region preview/picking; it never modifies the project CAD.
QVector<TopoDS_Shape> splitTemporaryPartitionRegions(const QStringList& geometryUuids,
                                              const QVariantList& planes,
                                              QString* message = nullptr,
                                              QVector<int>* ownerVolumeIds = nullptr);
// Polycube Region Blocking finite-surface precheck. Each surface payload contains
// loops (outer + optional holes) in physical CAD coordinates. OCC uses the same
// finite face geometry that Mesh later sends to Gmsh BooleanFragments.
QVariantMap evaluateHexBlockingSurfaces(const QStringList& geometryUuids, const QVariantList& surfaces);
// Export the active OCC CAD boundary as one welded, pure-triangle OBJ for
// Polycube labeling.  The companion face-map stores one global OCC Face id
// per exported triangle so Mesh can keep CAD topology correspondence if needed.
QVariantMap exportPolycubeSurfaceObj(const QStringList& geometryUuids,
                                     const QString& objPath,
                                     const QString& faceMapPath);
// 仅当目标是几何平面 Face 时返回真实平面中心/法向。曲面不退化成“中心切平面”，
// 避免 Hex Blocking 误把圆柱/弯曲侧面当作任意切割面。
bool entityPlane(const QStringList& geometryUuids, int dim, int id, QVector3D& origin, QVector3D& normal);
bool entityMeasure(const QStringList& geometryUuids, int dim, int id, double& measure);
// Convert a viewport pick {geometry uuid, local OCC sub-shape index} into the
// global UI id space exposed through PreProcessingService/listEntityIds().
int globalEntityId(const QStringList& geometryUuids, const QString& sourceUuid, int dim, int localIndex);
// Resolve a picked visible face to the owning solid in the same CAD object, then
// return that solid in the global dim=3 UI id space. This lets other modules
// select a volume by clicking any visible face without requiring a separate solid actor.
int globalSolidIdForFace(const QStringList& geometryUuids, const QString& sourceUuid, int localFaceIndex);
// 将一组几何对象中的子实体映射到当前完整模型的全局 UI ID。用于模型树多选后批量创建网格任务。
QList<int> globalEntityIdsForGeometryUuids(const QStringList& allGeometryUuids,
                                           const QStringList& selectedGeometryUuids, int dim);
// 读取一个全局 UI 子实体对应的 OCC Shape 副本，供只读的临时拾取/预览使用。
bool entityShape(const QStringList& geometryUuids, int dim, int id, TopoDS_Shape& shape);
int globalEntityIdForShape(const QStringList& geometryUuids, int dim, const TopoDS_Shape& shape);

bool highlightEntity(const QStringList& geometryUuids, int dim, int id);
bool highlightEntities(const QStringList& geometryUuids, int dim, const QList<int>& ids, bool hoverPreview);
void clearEntityHoverHighlight();
void clearEntityHighlight();
} // namespace GeometryEntityTools

#endif // GEOMETRYENTITYTOOLS_H
