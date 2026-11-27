#pragma once
#include <services/MeshModuleControllerService.h>
#include <vtkSmartPointer.h>

#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
class BasePage;

#include "MeshGridIO.h"
#include <SmeshWorkflowData.h>

class ProjectNode;
class VisualWidgetService;
class vtkActor;
class vtkMapper;
class vtkDataSet;
class vtkPolyData;
class vtkUnstructuredGrid;
class GridEditingWidget;
class BarElementPlacementWidget;
class MeshImportSettingsWidget;
class MeshQualityCheckWidget;

class MeshModuleController : public QObject, public MeshModuleControllerService
{
    Q_OBJECT
  public:
    explicit MeshModuleController(QObject* parent = nullptr);

    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();

    QVector<MeshTagSet> domainTagSets() const override;
    QString materialGroupCategory() const override;
    QString excavationGroupCategory() const override;
    QVector<MeshTagSet> boundaryTagSets(int dimFilter = -1) const override;
    QVector<MeshIdType> cellIdsOfTag(const MeshTagKey& key) const override;
    QVector<MeshIdType> pointIdsOfTag(const MeshTagKey& key) const override;
    QString displayTextOfTag(const MeshTagKey& key) const override;

    QSet<int> getAvailableMatypes() const override;
    void highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype, int previewMatype,
                          int preDeleteMatype) override;
    QVector<DomainKey> availableDomains(const QString& category) const override;
    QSet<MeshIdType> domainCells(const DomainKey& key) const override;
    QString domainDisplayText(const DomainKey& key) const override;

    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGrid();
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridSolid();
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridLine();
    QString sourceMeshFilePath() const override;
    int verticalAxis() const override;
    bool exportIiinDatFile(const QString& filePath, QString* errMsg = nullptr) override;

    bool importGeneratedSmeshMesh(const QString& vtkPath, QString* errMsg = nullptr) override;

    MshPickMap mshPickMapSolid();
    QString matypePhysicalName(int matype, int dim = 3) const override;
    void setMshPickMapSolid(const MshPickMap& pickMap) override;
    bool autoGroupingBySpatialBins(int binCount, const QString& targetArrayName, int startValue,
                                   const QString& groupPrefix, QString* errMsg = nullptr) override;
    bool groupByCoordinateRange(double xmin, double xmax, double ymin, double ymax, double zmin,
                                double zmax, const QString& targetArrayName, int value,
                                const QString& groupName, QString* errMsg = nullptr) override;
    bool assignSelectedCellsToGroup(const QVector<MeshIdType>& cellIds,
                                    const QString& attribute, int groupId,
                                    const QString& displayName,
                                    QString* errMsg = nullptr) override;
    bool assignDamConstructionLayers(const QSet<MeshIdType>& foundationCellIds,
                                     const QSet<MeshIdType>& damCellIds,
                                     int fillLayerCount,
                                     QString* errMsg = nullptr) override;

    void setEdgeControlRules(const QVector<EdgeControlRule>& rules) override;
    void setFaceControlRules(const QVector<FaceControlRule>& rules) override;
    void setVolumeControlRules(const QVector<VolumeControlRule>& rules) override;
    QVector<EdgeControlRule> edgeControlRules() const override;
    QVector<FaceControlRule> faceControlRules() const override;
    QVector<VolumeControlRule> volumeControlRules() const override;
    bool appendBarElements(const BarElementPlacement& placement,
                           QString* errMsg = nullptr) override;
    bool appendBarElementsBatch(const QVector<BarElementPlacement>& placements,
                                QString* errMsg = nullptr) override;
    bool removeBarElementRecord(int recordId, QString* errMsg = nullptr) override;
    QVector<BarElementRecordInfo> barElementRecords() const override;
    bool setBarPlacementMatype(const QString& placementUuid, int matype,
                               QString* errMsg = nullptr) override;
    void setBarPlacementDisplayMode(bool active) override;
    bool collectSmeshWorkflow(const QString& meshUuid, SmeshWorkflowSettings& settings,
                              QString& error) const;
    Q_INVOKABLE void addSmeshSubmesh(int dimension, const QString& parentUuid);
    Q_INVOKABLE void addSmeshSweep();
    Q_INVOKABLE void addSmeshPartition();
    Q_INVOKABLE void addSmeshTet();
    Q_INVOKABLE void addMeshVolumeControl();
    Q_INVOKABLE void addMeshFaceControl();
    Q_INVOKABLE void addMeshEdgeControl();
    Q_INVOKABLE void generateSmeshSubmesh(const QString& uuid);
    Q_INVOKABLE void markMeshControlsChangedForPlan(const QString& planUuid);
    Q_INVOKABLE void refreshMeshUiState();
    Q_INVOKABLE QVariantMap subMeshOccupancyForPlan(const QString& planUuid, const QString& excludeTaskUuid) const;
    Q_INVOKABLE void generateCurrentMeshConfiguration();
    Q_INVOKABLE void generateSmeshTetPreview(const QString& taskUuid, bool surfaceOnly);
    Q_INVOKABLE void restoreOfficialMeshDisplay();
    Q_INVOKABLE void requestStopCurrentMesh();

  private:
    void initAction();
    void initTreeMenu();
    void bindSmeshPage(BasePage* page);
    QStringList availableSubmeshEntities(const QString& uuid, int dimension) const;
    QSet<int> sweptCadVolumeIds() const;
    void refreshVolumeScopeOwnership();
    void removeSmeshNode(const QString& uuid);
    void removeBarPlacementNode(const QString& uuid);

    void onImportGrid();
    void onImportGridFileDialog();
    void importGridFile(const QString& fileName);
    void onExportGrid();

    void onMeshGlobal();
    void onMeshSingle();
    void onMeshLocalFace();
    void onMeshLocalEdge();
    void openLocalMeshSettings(int dimension);
    void onMeshMapped();
    void onMeshSweep();

    void onQualityStd();
    void runQualityCheck();
    void stopQualityCheck();
    void updateQualityCheckContext(bool useSuggestedObject);
    int suggestedQualityObjectType() const;
    void onMeshEdit();
    void onBarElements();
    void onMeshGrouping();
    void openImportSettingsPage(bool setCurrent = true);
    void openGridDisplayControlPage();
    void ensureGridDisplayControlPanel();
    void showGridDisplayControlPanel();

    void displayCurrentGrid(bool fitView = false); // 只显示 m_importedGrid

    void splitGridByDim(vtkUnstructuredGrid* src, vtkSmartPointer<vtkUnstructuredGrid>& outLine,
                        vtkSmartPointer<vtkUnstructuredGrid>& outNonLine);

    vtkSmartPointer<vtkActor> createSurfaceActor(vtkDataSet* grid) const;
    vtkSmartPointer<vtkActor> createLineActor(vtkUnstructuredGrid* grid) const;

  private:
    ProjectNode* m_gridNode = nullptr;

    // 彻底删掉 IiinGridModel，改为只存 vtk
    vtkSmartPointer<vtkUnstructuredGrid> m_importedGrid;
    bool m_hasGrid = false;
    QString m_sourceFileName;

    VisualWidgetService* m_visualWidgetService = nullptr;
    GridEditingWidget* m_gridEditingWidget = nullptr;
    BarElementPlacementWidget* m_barElementWidget = nullptr;
    MeshImportSettingsWidget* m_importSettingsWidget = nullptr;
    MeshQualityCheckWidget* m_qualityCheckWidget = nullptr;
    int m_verticalAxis = 2;

    vtkSmartPointer<vtkMapper> m_previewSurfaceMapper;
    vtkSmartPointer<vtkActor> m_previewSurfaceActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewSurfaceGrid;

    vtkSmartPointer<vtkMapper> m_previewLineMapper;
    vtkSmartPointer<vtkActor> m_previewLineActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewLineGrid;

    vtkSmartPointer<vtkMapper> m_previewMapper;
    vtkSmartPointer<vtkUnstructuredGrid> m_splitCacheSource;
    vtkSmartPointer<vtkUnstructuredGrid> m_splitCacheLine;
    vtkSmartPointer<vtkUnstructuredGrid> m_splitCacheNonLine;

    // 工程恢复时的一次性显示缓存。surface VTP 保存完整 mapped surface
    //（外表面 + MATYPE/LAYER 分界面），只用于首次 displayCurrentGrid()；
    // 网格后续被编辑或筛选后仍走实时重建逻辑。
    vtkSmartPointer<vtkPolyData> m_loadedSurfaceDisplayCache;
    vtkSmartPointer<vtkUnstructuredGrid> m_loadedLineDisplayCache;
    QVector<EdgeControlRule> m_edgeRules;
    QVector<FaceControlRule> m_faceRules;
    QVector<VolumeControlRule> m_volumeRules;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewGrid;
};
