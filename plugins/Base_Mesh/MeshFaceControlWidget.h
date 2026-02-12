#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>
#include <vtkSmartPointer.h>

namespace Ui {
class MeshFaceControlWidget;
}

class vtkActor;
class vtkPolyDataMapper;
class vtkUnstructuredGrid;

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshFaceControlWidget(QWidget *parent = nullptr);
    ~MeshFaceControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onDimChanged();
    void onGroupChanged();
    void onAddOrUpdateRule();
    void onRemoveSelectedRule();
    void onClearRules();
    void onResetDefaults();
    void onApplyControls();

private:
    void refreshGroupCombo();
    void updateGroupHighlight();
    void clearGroupHighlight(bool renderNow = true);
    int findRowByFaceId(int faceId) const;
    void updateSummary();

private:
    Ui::MeshFaceControlWidget *ui;
    vtkSmartPointer<vtkPolyDataMapper> m_highlightMapper;
    vtkSmartPointer<vtkActor> m_highlightActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_cachedGrid;
};

#endif // MESHFACECONTROLWIDGET_H
