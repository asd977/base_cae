#pragma once
#include <QObject>
#include <vtkSmartPointer.h>
#include <QSet>
#include <QVector>
#include <QString>

#include <services/MeshModuleControllerService.h>

#include "IiinGridModel.h"
#include "MeshGridIO.h"
#include "DisplacementConstraintModel.h"

class ProjectNode;
class VisualWidgetService;
class vtkActor;
class vtkDataSetMapper;
class vtkUnstructuredGrid;
class vtkPolyData;
class GridEditingWidget;

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
    QString currentModelFileName() const override;
    QSet<int> getAvailableMatypes() const override;
    void highlightMatypes(const QSet<int> &matypes,
                          int selectedMatype,
                          int previewMatype,
                          int preDeleteMatype) override;
    bool ensureConstraintModel(QString *errMsg = nullptr) override;
    bool loadConstraintFile(const QString &fileName, QString *errMsg = nullptr) override;
    bool saveConstraintFile(const QString &fileName, QString *errMsg = nullptr) const override;
    bool generateDefaultConstraintFile(const QString &fileName, QString *errMsg = nullptr) override;
    bool setNodeConstraint(int nodeId,
                           const QVector<int> &dofs,
                           QString *errMsg = nullptr) override;
    bool setConstraintsForMatype(int matype, const QVector<int> &dofs) override;
    bool setConstraintsForPlane(double a,
                                double b,
                                double c,
                                double d,
                                double tolerance,
                                const QVector<int> &dofs) override;
    bool currentConstraintState(QVector<QVector<int>> &constraints,
                                int &madof) const override;
    void setConstraintVisibility(bool showUx, bool showUy, bool showUz) override;


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
    void refreshConstraintActors();
    vtkSmartPointer<vtkPolyData> buildConstraintPolyData(int dofIndex) const;
    void ensureConstraintActors();

private:
    ProjectNode* m_gridNode = nullptr;
    IiinGridModel m_currentModel;
    bool m_hasModel = false;
    MeshGridIO m_gridIO;
    VisualWidgetService *m_visualWidgetService = nullptr;
    GridEditingWidget *m_gridEditingWidget = nullptr;
    vtkSmartPointer<vtkDataSetMapper> m_previewMapper;
    vtkSmartPointer<vtkActor> m_previewActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_previewGrid;
    DisplacementConstraintModel m_constraintModel;
    bool m_hasConstraints = false;
    vtkSmartPointer<vtkActor> m_constraintActors[3];
    bool m_constraintVisibility[3] = {true, true, true};
};
