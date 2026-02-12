#ifndef MESHGROUPINGWIDGET_H
#define MESHGROUPINGWIDGET_H

#include <BasePage.h>
#include <vtkSmartPointer.h>

class vtkActor;

namespace Ui {
class MeshGroupingWidget;
}

class MeshGroupingWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshGroupingWidget(QWidget *parent = nullptr);
    ~MeshGroupingWidget();

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAutoGrouping();
    void onRangeGrouping();
    void onRefreshGroups();
    void onToggleRangePreview(bool on);
    void onRangeValueChanged();

private:
    void refreshGroupList();
    void updateRangePreview(bool forceOn = false);
    void clearRangePreview(bool renderNow = true);

private:
    Ui::MeshGroupingWidget *ui;
    vtkSmartPointer<vtkActor> m_rangePreviewActor;
};

#endif // MESHGROUPINGWIDGET_H
