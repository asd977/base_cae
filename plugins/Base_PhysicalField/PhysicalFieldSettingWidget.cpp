#include "PhysicalFieldSettingWidget.h"
#include "ui_PhysicalFieldSettingWidget.h"

PhysicalFieldSettingWidget::PhysicalFieldSettingWidget(QWidget *parent) :
    ui(new Ui::PhysicalFieldSettingWidget),
    BasePage("PhysicalFieldSettingWidget")
{
    ui->setupUi(this);

}

PhysicalFieldSettingWidget::~PhysicalFieldSettingWidget()
{
    delete ui;
}

void PhysicalFieldSettingWidget::beforeLoad()
{

}

void PhysicalFieldSettingWidget::beforeUnload()
{

}

