#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>
#include <vtkSmartPointer.h>

namespace Ui {
class MeshEdgeControlWidget;
}

class vtkActor;
class vtkPolyDataMapper;
class vtkUnstructuredGrid;

class MeshEdgeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshEdgeControlWidget(QWidget *parent = nullptr);
    ~MeshEdgeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onDimChanged();
    void onGroupChanged();
    void onAddOrUpdateRule();
    void onRemoveSelectedRule();
    void onClearRules();
    void onResetDefaults();

private:
    void refreshGroupCombo();
    void updateGroupHighlight();
    void clearGroupHighlight(bool renderNow = true);
    int findRowByEdgeId(int edgeId) const;
    void updateSummary();

private:
    Ui::MeshEdgeControlWidget *ui;
    vtkSmartPointer<vtkPolyDataMapper> m_highlightMapper;
    vtkSmartPointer<vtkActor> m_highlightActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_cachedGrid;
};

#endif // MESHEDGECONTROLWIDGET_H
