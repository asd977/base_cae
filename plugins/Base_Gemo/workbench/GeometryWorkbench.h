#ifndef GEOMETRYWORKBENCH_H
#define GEOMETRYWORKBENCH_H

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <services/PreProcessingService.h>
#include <services/UiFrameService.h>

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QPair>
#include <QTreeWidget>
#include <QVector3D>
#include <QVariantMap>
#include <QVariant>
#include <functional>

#include <TopoDS_Shape.hxx>

#include "ProjectNode.h"

class QAction;
class QWidget;

class GeometryWorkbench : public QObject, public PreProcessingService
{
    Q_OBJECT
  public:
    GeometryWorkbench();
    ~GeometryWorkbench() override;
    void newProject() override;
    bool saveProject() override;
    bool loadProject() override;
    void closeProject() override;

    void generateStepFile() override;
    bool prepareMeshingGeometry(int dimension, QString& outputPath,
                                QString& diagnostic) override;
    QStringList listEntityIds(int dim) override;
    bool entityCenter(int dim, int id, QVector3D& center) override;
    bool entityMeasure(int dim, int id, double& measure) override;
    QStringList entitySubshapeIds(int parentDim, int parentId, int childDim) override;
    bool highlightEntity(int dim, int id) override;
    bool highlightEntities(int dim, const QList<int>& ids, bool hoverPreview) override;
    void clearEntityHoverHighlight() override;
    void clearEntityHighlight() override;
    std::shared_ptr<const NativeCadSnapshot> nativeCadSnapshot() override;
    Q_INVOKABLE bool showExternalMeshScopeSolids(const QVariantList& selectedSolidIds);
    Q_INVOKABLE void clearExternalMeshScopeSolids();

    // Loose-coupled viewport sub-entity picking for other preprocessing modules.
    // The receiver gets QEvent::User+421 and can read the dynamic properties
    // _geometryExternalPickDim / _geometryExternalPickId.
    Q_INVOKABLE bool beginExternalEntityPick(QObject* receiver, int dim);
    Q_INVOKABLE QVariantList meshingGeometryBounds(int dimension, const QVariantList& ids);
    // 批量 Sweep：从模型树 Ctrl/Shift 多选的几何叶节点中返回全局 Volume UI ID。
    Q_INVOKABLE QVariantList externalSelectedSolidIds() const;
    // 批量选择对话框使用：返回活动 CAD Volume 的 {id,name,visible,selected}。
    Q_INVOKABLE QVariantList externalSolidEntries() const;
    // 当 Sweep 已经选定一个 CAD Volume 时，只显示/拾取该体自己的边界面，
    // 避免大型装配体中 Source/Target 被其它零件遮挡或误选。
    Q_INVOKABLE bool beginExternalSolidFacePick(QObject* receiver, int volumeUiId);
    // Mesh-only OCC Regions are temporary meshing solids, not project CAD solids.
    // These helpers expose an explicit viewport picker/preview for Region 1..N.
    Q_INVOKABLE bool beginExternalPartitionRegionPick(QObject* receiver, const QVariantList& planes);
    Q_INVOKABLE bool highlightExternalPartitionRegion(const QVariantList& planes, int regionOrdinal);
    // 分块显示控制：普通预览保留原 CAD 作为空间参照，并叠加醒目的黄色切面和分块轮廓；
    // 只有进入实际 Region/Face 拾取时才临时隔离几何，退出拾取会完整恢复原显示状态。
    Q_INVOKABLE bool showExternalPartitionCutters(const QVariantList& planes);
    Q_INVOKABLE bool showExternalPartitionRegions(const QVariantList& planes,
                                                   const QVariantList& visibleRegionOrdinals,
                                                   int selectedRegionOrdinal);
    Q_INVOKABLE int externalPartitionRegionCount(const QVariantList& planes);
    // Temporary region metadata for the partition list and explicit topology selection.
    Q_INVOKABLE QVariantMap externalPartitionRegionInfo(const QVariantList& planes, int regionOrdinal);
    // 选中 Region 后，只显示并拾取该 Region 自己的边界面。返回的 Face ID 是
    // Region 内稳定局部序号 1..M，不是原 CAD Face ID，也不是临时 SMESH tag。
    // 局部尺寸/边界层直接拾取：用户无需先选择 Region。内部仍保存 Region:Face，
    // Region 归属由临时 OCC Partition 自动解析；不改变正式 CAD/网格拓扑。
    Q_INVOKABLE bool beginExternalPartitionFaceScenePick(QObject* receiver,
                                                           const QVariantList& planes,
                                                           bool externalOnly,
                                                           const QString& selectedRefs);
    Q_INVOKABLE bool showExternalPartitionFaceRefs(const QVariantList& planes, const QString& refs);
    Q_INVOKABLE bool beginExternalPartitionFacePick(QObject* receiver, const QVariantList& planes, int regionOrdinal);
    // Sweep 专用面拾取：固定面会持续高亮，用户只需在当前 Region 上点击另一面。
    Q_INVOKABLE bool beginExternalPartitionSweepFacePick(QObject* receiver,
                                                          const QVariantList& planes,
                                                          int regionOrdinal,
                                                          int fixedFaceOrdinal,
                                                          bool selectingSource);
    // Sweep 编辑态持续显示当前 Region + Source/Target，避免选完一个面后高亮立即消失。
    Q_INVOKABLE bool showExternalPartitionSweepSelection(const QVariantList& planes,
                                                          int regionOrdinal,
                                                          int sourceFaceOrdinal,
                                                          int targetFaceOrdinal);
    Q_INVOKABLE bool highlightExternalPartitionFace(const QVariantList& planes, int regionOrdinal, int faceOrdinal);
    Q_INVOKABLE QVariantMap externalPartitionFaceInfo(const QVariantList& planes, int regionOrdinal, int faceOrdinal);
    // 局部尺寸控制使用：与分块 Face 一样，Region 内 Edge 使用稳定局部序号 1..N。
    Q_INVOKABLE bool beginExternalPartitionEdgeScenePick(QObject* receiver,
                                                           const QVariantList& planes,
                                                           const QString& selectedRefs);
    Q_INVOKABLE bool showExternalPartitionEdgeRefs(const QVariantList& planes, const QString& refs);
    Q_INVOKABLE bool beginExternalPartitionEdgePick(QObject* receiver, const QVariantList& planes, int regionOrdinal);
    Q_INVOKABLE bool highlightExternalPartitionEdge(const QVariantList& planes, int regionOrdinal, int edgeOrdinal);
    Q_INVOKABLE QVariantMap externalPartitionEdgeInfo(const QVariantList& planes, int regionOrdinal, int edgeOrdinal);
    Q_INVOKABLE void clearExternalPartitionRegionPreview();
    Q_INVOKABLE void stopExternalEntityPick(QObject* receiver);
    Q_INVOKABLE int externalFaceDominantAxis(int globalFaceId) const;
    // Hex Blocking 使用：返回真实 CAD 平面 {valid, origin, normal}。
    // 非平面 Face 返回 valid=false，不把曲面中心切平面伪装成 Blocking 平面。
    Q_INVOKABLE QVariantMap externalFacePlane(int globalFaceId) const;
    // Hex Blocking 手动“两边定面”：仅直线 Edge 有效，返回 {valid, origin, direction}。
    Q_INVOKABLE QVariantMap externalEdgeLine(int globalEdgeId) const;
    // Hex Blocking 半自动分析：直线边相邻面的平面/曲面拓扑特征。
    Q_INVOKABLE QVariantMap externalEdgeBlockingInfo(int globalEdgeId) const;
    // OCC Sweep-Decomposition: curved/closed feature edge -> candidate local section plane.
    Q_INVOKABLE QVariantMap externalEdgeSweepFeaturePlane(int globalEdgeId) const;
    // OCC Sweep 自动分解：先判断当前单实体是否可直接做严格平移 Pave-Sweep。
    Q_INVOKABLE QVariantMap externalDetectTranslationalPaveSweep() const;
    // Sweep UI 自动分析：针对用户已经选中的 CAD Volume，自动给出严格平移 Sweep 候选。
    // 多候选（如长方体三组相对面）全部返回，由 UI 让用户选择，不在几何层擅自猜。
    Q_INVOKABLE QVariantMap externalAnalyzeSweepVolume(int volumeUiId) const;
    // 大装配批量 Sweep 分析：一次调用分析多个 Volume，避免 UI 逐体同步调用导致卡顿。
    Q_INVOKABLE QVariantList externalAnalyzeSweepVolumes(const QVariantList& volumeUiIds) const;
    // Mesh-only Region 对应版本；Face 编号转换为 Region 内稳定局部序号。
    Q_INVOKABLE QVariantMap externalAnalyzePartitionSweepRegion(const QVariantList& planes, int regionOrdinal);
    // Hex Blocking 半自动建议：在原 CAD 的 OCC 临时副本上真实试切并返回 Sweep 拓扑评分。
    Q_INVOKABLE QVariantMap externalEvaluateHexBlocking(const QVariantList& planes) const;
    // Finite Mesh-only Surface precheck for Polycube Region Blocking.
    Q_INVOKABLE QVariantMap externalEvaluateHexBlockingSurfaces(const QVariantList& surfaces) const;
    // Mesh Polycube analysis bridge: export the original OCC CAD boundary as a
    // welded closed triangle OBJ. Polycube uses it for labeling/block topology only.
    Q_INVOKABLE QVariantMap externalExportPolycubeSurfaceObj(const QString& objPath,
                                                              const QString& faceMapPath) const;
    void refreshTreePresentation(const QString& changedUuid = QString());
    void commitSketchHistory(const QString& uuid);
    bool discardUnappliedGeometryNode(const QString& uuid);

    void newGeometryWidget(const QString& kind, const QString& pageType, const QString& defaultTitle,
                           const QString& initialSketchTool = QString());
    void newExtrudeWidget(const QString& profileUuid = QString());
    void newRevolveWidget(const QString& sourceUuid = QString());
    void newSweepWidget(const QString& profileUuid = QString(), const QString& pathUuid = QString());
    void newLoftWidget(const QString& initialProfileUuid = QString());
    void newCutExtrudeWidget(const QString& profileUuid = QString(), const QString& targetUuid = QString());
    void newPatternWidget(const QString& sourceUuid = QString());
    void newTransformWidget(const QString& sourceUuid = QString());
    void newMirrorWidget(const QString& sourceUuid = QString());
    void newFilletWidget(const QString& sourceUuid = QString());
    void newChamferWidget(const QString& sourceUuid = QString());
    void newSplitWidget(const QString& sourceUuid = QString(), const QString& toolUuid = QString());
    bool exportGeom();

    // 非建模工具：不写入模型树，用于检查、修复和测量。
    QList<QPair<QString, QString>> geometryObjectChoices() const;
    QString selectedGeometryUuidForTools() const;
    void focusGeometryForTools(const QString& uuid);
    void openGeometryCheckTool();
    void openGeometryMeasureTool();
    // 建模面板可在生成派生结果后隐藏来源对象；用户仍可在模型树中重新显示。
    void setNodeVisible(const QString& uuid, bool visible);

  private:
    bool ensureExternalPartitionRegions(const QVariantList& planes, bool forceRebuild, QString* detail = nullptr);
    void beginExternalPartitionGeometryIsolation();
    void restoreExternalPartitionGeometryIsolation(bool renderNow);
    void clearExternalPartitionRegionPreviewImpl(bool renderNow);
    QStringList m_externalPartitionRegionPropIds;
    QHash<QString, bool> m_externalPartitionSavedGeometryVisibility;
    bool m_externalPartitionGeometryIsolated = false;
    QVariantList m_externalPartitionRegionPlanes;
    QStringList m_externalPartitionRegionGeometryUuids;
    QVector<TopoDS_Shape> m_externalPartitionRegionShapes;
    QVector<int> m_externalPartitionRegionOwnerVolumeIds;

    bool writeJsonFile(const QString& filePath, const QJsonObject& obj);
    bool readJsonFile(const QString& filePath, QJsonObject& out);
    bool loadChildProject(ProjectNode* node, const QString& name);
    QJsonArray serializeGeometryItems(bool includeCachedBrep, const QString& changedUuid = QString(),
                                      bool preserveCommittedState = false) const;
    bool loadGeometryItems(const QJsonArray& items,
                           const QHash<QString, TopoDS_Shape>* shapeSnapshots = nullptr);
    BasePage* createPageByType(const QString& type, const QString& title);
    ProjectNode* createGeometryNodeByType(const QString& type, const QString& kind,
                                          const QString& title, const QString& uuid);
    ProjectNode* createOperationNode(BasePage* page, const QString& kind, const QString& defaultTitle,
                                     bool selectNode = true);
    void applyNodeVisualState(ProjectNode* node, const QJsonObject& itemState) const;
    void installGeometryTreeHooks();
    void onGeometryTreeCurrentItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);
    void onGeometryTreeItemChanged(QTreeWidgetItem* item, int column);
    void highlightGeometryNode(const QString& uuid, bool fit = false);
    void editNodeParameters(const QString& uuid, bool fit = false);
    QStringList directDerivedGeometryUuids(const QString& uuid) const;
    void previewDerivedGeometry(const QString& uuid);
    void editFirstDerivedGeometry(const QString& uuid);
    void duplicateNode(const QString& uuid);
    void setCurrentTreeSelection(const QString& uuid);
    void clearTreeSelectionHighlight();
    void handleGeometryTreeVisibilityChange(ProjectNode* node);
    void clearFeaturePreviewForGeometryVisibilityChange(const QString& uuid);
    void setSourcePreviewUuids(const QStringList& uuids);
    void clearSourcePreviewHighlight();
    void ensureGeometryRootGroups();
    ProjectNode* ensureGeometryGroup(const QString& groupKey, const QString& title);
    QString geometryGroupForKind(const QString& kind) const;
    void addGeometryNodeToTree(ProjectNode* node);
    void collectGeometryLeafNodes(ProjectNode* root, QList<ProjectNode*>& out) const;
    ProjectNode* findGeometryNodeByUuid(const QString& uuid) const;
    QString geometryNodeUuid(const ProjectNode* node) const;
    QJsonObject geometryNodeState(ProjectNode* node) const;
    QStringList geometryInputUuids(const QJsonObject& state, const QString& selfUuid = QString()) const;
    QStringList cachedGeometryInputUuids(ProjectNode* node) const;
    void removeGeometryLeafNode(ProjectNode* node);
    void showGeometryInputs(const QString& uuid);
    bool isExtrudableProfileNode(const ProjectNode* node) const;
    QString geometryNodeTypeLabel(const QString& kind) const;
    void updateGeometryNodeDependencyCache(ProjectNode* node);
    QString geometryNodeInputSummary(
        ProjectNode* node,
        const QHash<QString, ProjectNode*>* nodesByUuid = nullptr) const;
    QString geometryNodeDisplayText(ProjectNode* node) const;
    QString geometryNodeTooltip(
        ProjectNode* node,
        const QHash<QString, ProjectNode*>* nodesByUuid = nullptr) const;
    void refreshGeometryNodePresentation();
    void syncGeometryInputReferenceNodes(ProjectNode* node);
    void clearGeometryInputReferenceNodes(ProjectNode* node);
    void deleteNodeTree(ProjectNode* node, bool runBeforeUnload = true);
    void clearGeometryRuntimeState(bool renderNow = true);
    void synchronizeSplitPartNodes(const QString& splitUuid);
    void synchronizeAllSplitPartNodes();
    void synchronizeStepImportPartNodes(const QString& importUuid);
    void synchronizeAllStepImportPartNodes();
    QStringList activeGeometryUuids() const;
    ProjectNode* currentGeometryNode() const;
    QString currentGeometrySelectionUuid() const;
    QStringList currentGeometrySelectionUuids() const;

    struct GeometryHistorySnapshot
    {
        QJsonArray items;
        QHash<QString, TopoDS_Shape> shapes;
    };
    GeometryHistorySnapshot captureGeometryHistorySnapshot(
        const QString& changedUuid = QString(), bool preserveCommittedState = false) const;
    bool restoreGeometryHistorySnapshot(const GeometryHistorySnapshot& snapshot);
    void commitGeometryHistory(const QString& changedUuid = QString());
    void resetGeometryHistory();
    void undoGeometryChange();
    void redoGeometryChange();

    void importGeometryFile();
    void initRibbonActionConn();

  private:
    ProjectNode* m_geomNode = nullptr;
    ProjectNode* m_gridNode = nullptr;
    UiFrameService* m_uiFrameService = nullptr;

    void toggleVisible(const QString& uuid);
    void fitNode(const QString& uuid);
    void removeNode(const QString& uuid);
    void renameNode(const QString& uuid);
    void showNodeInfo(const QString& uuid);

    void initTreeMenu();
    bool writeMultipleShapesStepFile(const QString& filePath);
    bool writeMultipleShapesIgesFile(const QString& filePath);
    bool writeMultipleShapesBrepFile(const QString& filePath);
    QString makeUniqueName(const QString& baseName, const QString& typeKey);

    QHash<QString, int> m_nameCounters;
    struct SavedBrepAsset
    {
        TopoDS_Shape shape;
        QString relativePath;
        qint64 bytes = 0;
        qint64 modifiedMsecs = 0;
    };
    QHash<QString, SavedBrepAsset> m_savedBrepAssets;
    QVector<TopoDS_Shape> m_exportedGeometryShapes;
    qint64 m_exportedGeometryBytes = 0;
    qint64 m_exportedGeometryModifiedMsecs = 0;
    QHash<QString, ProjectNode*> m_geometryGroups;
    QPointer<QTreeWidget> m_hookedGeometryTree;
    bool m_updatingGeometryTree = false;
    bool m_loadingProject = false;
    QString m_currentTreeSelectionUuid;
    QStringList m_sourcePreviewUuids;
    QPointer<QWidget> m_geometryCheckDialog;
    QPointer<QWidget> m_geometryMeasureDialog;
    QPointer<QAction> m_geometryCheckDockAction;
    QPointer<QAction> m_geometryMeasureDockAction;
    QVector<GeometryHistorySnapshot> m_geometryUndoHistory;
    QVector<GeometryHistorySnapshot> m_geometryRedoHistory;
    GeometryHistorySnapshot m_geometryHistoryCurrent;
    bool m_restoringGeometryHistory = false;
};

#endif // GEOMETRYWORKBENCH_H
