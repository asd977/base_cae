#ifndef CONSTRAINTSETTINGWIDGET_H
#define CONSTRAINTSETTINGWIDGET_H

#include <BasePage.h>
#include <QHash>
#include <QWidget>

#include <vtkSmartPointer.h>

#include "IiinGridModel.h"

class VisualWidgetService;
class MeshModuleControllerService;
class vtkActor;
class vtkPointPicker;
class vtkRenderer;
class vtkUnstructuredGrid;

namespace Ui {
class ConstraintSettingWidget;
}

struct ConstraintState
{
    int ux = 1;
    int uy = 1;
    int uz = 1;
};

class ConstraintSettingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit ConstraintSettingWidget(QWidget *parent = nullptr);
    ~ConstraintSettingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

    void onPickedPoint(vtkIdType pointId);

private:
    void setupInteractor();
    void teardownInteractor();
    void loadMeshFromFile(const QString &fileName);
    void updateMaterialSetOptions();
    void updateConstraintMarkers();
    void applyConstraintToNode(int nodeId, const ConstraintState &state);
    void applyConstraintToMaterialSet(int matype, const ConstraintState &state);
    void setConstraintDisplayVisible();
    ConstraintState constraintFromUi() const;
    void saveConstraintFile(const QString &fileName);
    bool loadConstraintFile(const QString &fileName);

private slots:
    void on_buttonLoadMesh_clicked();
    void on_buttonUseCurrentMesh_clicked();
    void on_buttonLoadConstraint_clicked();
    void on_buttonSaveConstraint_clicked();
    void on_buttonApplyNode_clicked();
    void on_buttonApplyMaterial_clicked();
    void on_checkShowUx_toggled(bool checked);
    void on_checkShowUy_toggled(bool checked);
    void on_checkShowUz_toggled(bool checked);
    void on_checkEnablePicking_toggled(bool checked);

private:
    Ui::ConstraintSettingWidget *ui;

    VisualWidgetService *m_visualWidgetService = nullptr;
    MeshModuleControllerService *m_meshModuleService = nullptr;
    vtkSmartPointer<vtkRenderer> m_renderer;
    vtkSmartPointer<vtkUnstructuredGrid> m_grid;
    vtkSmartPointer<vtkActor> m_meshActor;
    vtkSmartPointer<vtkPointPicker> m_pointPicker;

    vtkSmartPointer<vtkActor> m_constraintActorX;
    vtkSmartPointer<vtkActor> m_constraintActorY;
    vtkSmartPointer<vtkActor> m_constraintActorZ;

    bool m_enablePicking = false;
    int m_selectedNodeId = -1;

    QString m_meshFileName;
    QString m_constraintFileName;
    IiinGridModel m_model;
    QHash<int, ConstraintState> m_constraints;
};

#endif // CONSTRAINTSETTINGWIDGET_H
