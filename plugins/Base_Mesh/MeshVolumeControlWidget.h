#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>
#include <vtkSmartPointer.h>

namespace Ui {
class MeshVolumeControlWidget;
}

class vtkActor;
class vtkDataSetMapper;
class vtkUnstructuredGrid;

class MeshVolumeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshVolumeControlWidget(QWidget *parent = nullptr);
    ~MeshVolumeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onGroupChanged();
    void onAddOrUpdateRule();
    void onRemoveSelectedRule();
    void onClearRules();
    void onResetDefaults();

private:
    void refreshGroupCombo();
    void updateVolumeHighlight();
    void clearVolumeHighlight(bool renderNow = true);
    int findRowByVolumeId(int volumeId) const;
    void updateSummary();

private:
    Ui::MeshVolumeControlWidget *ui;
    vtkSmartPointer<vtkDataSetMapper> m_highlightMapper;
    vtkSmartPointer<vtkActor> m_highlightActor;
    vtkSmartPointer<vtkUnstructuredGrid> m_cachedGrid;
};

#endif // MESHVOLUMECONTROLWIDGET_H
