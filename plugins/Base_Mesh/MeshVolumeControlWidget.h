#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>
#include <vtkSmartPointer.h>

namespace Ui {
class MeshVolumeControlWidget;
}

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
    void onApplyControls();

private:
    void refreshGroupCombo();
    int findRowByVolumeId(int volumeId) const;
    void updateSummary();

private:
    Ui::MeshVolumeControlWidget *ui;
    vtkSmartPointer<vtkUnstructuredGrid> m_cachedGrid;
};

#endif // MESHVOLUMECONTROLWIDGET_H
