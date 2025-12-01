#ifndef MATERIALMANAGERWIDGET_H
#define MATERIALMANAGERWIDGET_H

#include <BasePage.h>
#include <QWidget>

class MaterialManager;
namespace Ui {
class MaterialManagerWidget;
}

class MaterialManagerWidget : public BasePage
{
    Q_OBJECT

public:
    explicit MaterialManagerWidget(MaterialManager *manager,QWidget *parent = nullptr);
    ~MaterialManagerWidget();
    void beforeLoad() override;
    void beforeUnload() override;

private slots:
    void on_addPlanMaterialPushButton_clicked();

private:
    Ui::MaterialManagerWidget *ui;
    MaterialManager *m_manager;
};

#endif // MATERIALMANAGERWIDGET_H
