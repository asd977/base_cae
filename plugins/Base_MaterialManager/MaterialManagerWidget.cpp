#include "MaterialManager.h"
#include "MaterialManagerWidget.h"
#include "ui_MaterialManagerWidget.h"

MaterialManagerWidget::MaterialManagerWidget(MaterialManager *manager , QWidget *parent) :
    ui(new Ui::MaterialManagerWidget),
    BasePage("MaterialManagerWidget")
{
    ui->setupUi(this);
    m_manager = manager;
}

MaterialManagerWidget::~MaterialManagerWidget()
{
    delete ui;
}

void MaterialManagerWidget::beforeLoad()
{

}

void MaterialManagerWidget::beforeUnload()
{

}

void MaterialManagerWidget::on_addPlanMaterialPushButton_clicked()
{
    m_manager->newMaterial();
}
