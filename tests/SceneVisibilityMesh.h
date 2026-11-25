#pragma once
#include <services/MeshModuleControllerService.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

// Two-domain, in-memory data source for actual visual-registry/picker tests.
class SceneVisibilityMesh final : public MeshModuleControllerService
{
public:
    vtkSmartPointer<vtkUnstructuredGrid> solid=vtkSmartPointer<vtkUnstructuredGrid>::New();
    void newProject() override {}
    bool saveProject() override {return {};}
    bool loadProject() override {return {};}
    void closeProject() override {}
    QVector<MeshTagSet> domainTagSets() const override {QVector<MeshTagSet> result;for(int id:{1,2}) {MeshTagSet set;set.key={"MATYPE",3,id};set.cellIds={id-1};result.append(set);}return result;}
    QString materialGroupCategory() const override {return "MATYPE";}
    QString excavationGroupCategory() const override {return "MATYPE";}
    QVector<MeshTagSet> boundaryTagSets(int dimFilter = -1) const override {return {};}
    QVector<MeshIdType> cellIdsOfTag(const MeshTagKey& key) const override {return key.category=="MATYPE" && key.dim==3 && key.id>=1 && key.id<=2 ? QVector<MeshIdType>{key.id-1}:QVector<MeshIdType>{};}
    QVector<MeshIdType> pointIdsOfTag(const MeshTagKey& key) const override {return {};}
    QString displayTextOfTag(const MeshTagKey& key) const override {return {};}
    QSet<int> getAvailableMatypes() const override {return {1,2};}
    void highlightDomainMatypes(const QSet<int>& matypes, int selectedMatype, int previewMatype, int preDeleteMatype) override {}
    QVector<DomainKey> availableDomains(const QString& category) const override {return {{category,3,1},{category,3,2}};}
    QSet<MeshIdType> domainCells(const DomainKey& key) const override {return key.category=="MATYPE" && key.dim==3 && key.id>=1 && key.id<=2 ? QSet<MeshIdType>{key.id-1}:QSet<MeshIdType>{};}
    QString domainDisplayText(const DomainKey& key) const override {return {};}
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGrid() override {return solid;}
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridSolid() override {return solid;}
    vtkSmartPointer<vtkUnstructuredGrid> getVtkUnstructuredGridLine() override {return {};}
    QString sourceMeshFilePath() const override {return {};}
    int verticalAxis() const override {return 2;}
    bool exportIiinDatFile(const QString& filePath, QString* errMsg = nullptr) override {return {};}
    bool importGeneratedSmeshMesh(const QString& vtkPath, QString* errMsg = nullptr) override {return {};}
    MshPickMap mshPickMapSolid() override {return {};}
    QString matypePhysicalName(int matype, int dim = 3) const override {return {};}
    void setMshPickMapSolid(const MshPickMap& pickMap) override {}
    bool autoGroupingBySpatialBins(int binCount, const QString& targetArrayName, int startValue, const QString& groupPrefix, QString* errMsg = nullptr) override {return {};}
    bool groupByCoordinateRange(double xmin, double xmax, double ymin, double ymax, double zmin, double zmax, const QString& targetArrayName, int value, const QString& groupName, QString* errMsg = nullptr) override {return {};}
    bool assignSelectedCellsToGroup(const QVector<MeshIdType>& cellIds, const QString& attribute, int groupId, const QString& displayName, QString* errMsg = nullptr) override {return {};}
    bool assignDamConstructionLayers(const QSet<MeshIdType>& foundationCellIds, const QSet<MeshIdType>& damCellIds, int fillLayerCount, QString* errMsg = nullptr) override {return {};}
    bool appendBarElements(const BarElementPlacement& placement, QString* errMsg = nullptr) override {return {};}
    bool appendBarElementsBatch(const QVector<BarElementPlacement>& placements, QString* errMsg = nullptr) override {return {};}
    bool removeBarElementRecord(int recordId, QString* errMsg = nullptr) override {return {};}
    QVector<BarElementRecordInfo> barElementRecords() const override {return {};}
    bool setBarPlacementMatype(const QString& placementUuid, int matype, QString* errMsg = nullptr) override {return {};}
    void setBarPlacementDisplayMode(bool active) override {}
    void setEdgeControlRules(const QVector<EdgeControlRule>& rules) override {}
    void setFaceControlRules(const QVector<FaceControlRule>& rules) override {}
    void setVolumeControlRules(const QVector<VolumeControlRule>& rules) override {}
    QVector<EdgeControlRule> edgeControlRules() const override {return {};}
    QVector<FaceControlRule> faceControlRules() const override {return {};}
    QVector<VolumeControlRule> volumeControlRules() const override {return {};}
};
