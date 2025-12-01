#include "ElectroPhoresisSettingWidget.h"
#include "ui_ElectroPhoresisSettingWidget.h"

ElectroPhoresisSettingWidget::ElectroPhoresisSettingWidget(QWidget *parent) :
    ui(new Ui::ElectroPhoresisSettingWidget),
    BasePage("ElectroPhoresisSettingWidget")
{
    ui->setupUi(this);
}

ElectroPhoresisSettingWidget::~ElectroPhoresisSettingWidget()
{
    delete ui;
}

void ElectroPhoresisSettingWidget::beforeLoad()
{

}

void ElectroPhoresisSettingWidget::beforeUnload()
{

}
