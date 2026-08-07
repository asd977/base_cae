#pragma once
#include <services/MeshModuleControllerService.h>
#include <vtkSmartPointer.h>

#include <QObject>
#include <QSet>
#include <QString>

#include "MeshGridIO.h"

class ProjectNode;
class VisualWidgetService;
class vtkActor;
class vtkMapper;
class vtkDataSet;
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

    bool importGeneratedNetgenMesh(const QString& vtkPath, QString* errMsg = nullptr) override;

    MshPickMap mshPickMapSolid();
    QString matypePhysicalName(int matype, int dim = 3) const override;
    void setMshPickMapSolid(const MshPickMap& pickMap) override;
    bool autoGroupingBySpatialBins(int binCount, const QString& targetArrayName, int startValue,
                                   const QString& groupPrefix, QString* errMsg = nullptr) override;
    bool groupByCoordinateRange(double xmin, double xmax, double ymin, double ymax, double zmin,
                                double zmax, const QString& targetArrayName, int value,
                                const QString& groupName, QString* errMsg = nullptr) override;
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
    bool removeBarElementRecord(int recordId, QString* errMsg = nullptr) override;
    QVector<BarElementRecordInfo> barElementRecords() const override;

  private:
    void initAction();

    void onImportGrid();
    void onImportGridFileDialog();
    void importGridFile(const QString& fileName);
    void onExportGrid();

    void onMeshGlobal();
    void onMeshSingle();
    void onMeshMapped();
    void onMeshSweep();

    void onCtrlVolume();
    void onCtrlSurface();
    void onCtrlEdge();
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
    QVector<EdgeControlRule> m_edgeRules;
    QVector<FaceControlRule> m_faceRules;
    QVector<VolumeControlRule> m_volumeRules;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewGrid;
};
