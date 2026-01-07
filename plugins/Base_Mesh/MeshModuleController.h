#pragma once
#include <QObject>
#include <vtkSmartPointer.h>
#include <QSet>

#include <services/MeshModuleControllerService.h>

#include "IiinGridModel.h"
#include "MeshGridIO.h"

class ProjectNode;
class VisualWidgetService;
class vtkActor;
class vtkDataSetMapper;

class MeshModuleController : public QObject, public MeshModuleControllerService
{
    Q_OBJECT
public:
    explicit MeshModuleController(QObject* parent = nullptr);

    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();
    bool currentModel(IiinGridModel &model) const override;
    QSet<int> getAvailableMatypes() const override;
    void highlightMatypes(const QSet<int> &matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype) override;


private:
    void initAction();

    // --- slots (handlers) ---
    void onImportGrid();
    void onExportGrid();

    void onMeshGlobal();
    void onMeshSingle();
    void onMeshMapped();
    void onMeshSweep();

    void onCtrlVolume();
    void onCtrlSurface();
    void onCtrlEdge();
    void onQualityStd();
    void onMeshEdit();
    void displayCurrentModel();
    vtkSmartPointer<vtkUnstructuredGrid> buildGridFromModel(const IiinGridModel &model) const;
    int expectedNodeCountForKetype(int ketype) const;
    int ketypeToVtkCellType(int ketype) const;

private:
    ProjectNode* m_gridNode = nullptr;
    IiinGridModel m_currentModel;
    bool m_hasModel = false;
    MeshGridIO m_gridIO;
    VisualWidgetService *m_visualWidgetService = nullptr;
    vtkSmartPointer<vtkDataSetMapper> m_previewMapper;
    vtkSmartPointer<vtkActor> m_previewActor;
};
