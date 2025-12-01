#include "ElectrolyteSettingWidget.h"
#include "ui_ElectrolyteSettingWidget.h"

ElectrolyteSettingWidget::ElectrolyteSettingWidget(QWidget *parent) :
    ui(new Ui::ElectrolyteSettingWidget),
    BasePage("ElectrolyteSettingWidget")
{
    ui->setupUi(this);
}

ElectrolyteSettingWidget::~ElectrolyteSettingWidget()
{
    delete ui;
}

void ElectrolyteSettingWidget::beforeLoad()
{

}

void ElectrolyteSettingWidget::beforeUnload()
{

}
