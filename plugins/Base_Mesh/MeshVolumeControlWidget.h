#ifndef MESHVOLUMECONTROLWIDGET_H
#define MESHVOLUMECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshVolumeControlWidget;
}

class MeshVolumeControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshVolumeControlWidget(QWidget *parent = nullptr);
    ~MeshVolumeControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAddOrUpdateRule();
    void onRemoveSelectedRule();
    void onClearRules();
    void onResetDefaults();

private:
    int findRowByVolumeId(int volumeId) const;
    void updateSummary();

private:
    Ui::MeshVolumeControlWidget *ui;
};

#endif // MESHVOLUMECONTROLWIDGET_H
