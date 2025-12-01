#include "ElectroPlatingSettingWidget.h"
#include "ui_ElectroPlatingSettingWidget.h"

ElectroPlatingSettingWidget::ElectroPlatingSettingWidget(QWidget *parent) :
    ui(new Ui::ElectroPlatingSettingWidget),
    BasePage("ElectroPlatingSettingWidget")
{
    ui->setupUi(this);
}

ElectroPlatingSettingWidget::~ElectroPlatingSettingWidget()
{
    delete ui;
}

void ElectroPlatingSettingWidget::beforeLoad()
{

}

void ElectroPlatingSettingWidget::beforeUnload()
{

}
