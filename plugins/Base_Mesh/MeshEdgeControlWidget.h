#ifndef MESHEDGECONTROLWIDGET_H
#define MESHEDGECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshEdgeControlWidget;
}

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
    int findRowByEdgeId(int edgeId) const;
    void updateSummary();

private:
    Ui::MeshEdgeControlWidget *ui;
};

#endif // MESHEDGECONTROLWIDGET_H
