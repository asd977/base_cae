#ifndef MESHFACECONTROLWIDGET_H
#define MESHFACECONTROLWIDGET_H

#include <BasePage.h>

namespace Ui {
class MeshFaceControlWidget;
}

class MeshFaceControlWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MeshFaceControlWidget(QWidget *parent = nullptr);
    ~MeshFaceControlWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void onAddOrUpdateRule();
    void onRemoveSelectedRule();
    void onClearRules();
    void onResetDefaults();

private:
    int findRowByFaceId(int faceId) const;
    void updateSummary();

private:
    Ui::MeshFaceControlWidget *ui;
};

#endif // MESHFACECONTROLWIDGET_H
