#include "ElectrodeSettingWidget.h"
#include "ui_ElectrodeSettingWidget.h"

ElectrodeSettingWidget::ElectrodeSettingWidget(QWidget *parent) :
    ui(new Ui::ElectrodeSettingWidget),
    BasePage("ElectrodeSettingWidget")
{
    ui->setupUi(this);
}

ElectrodeSettingWidget::~ElectrodeSettingWidget()
{
    delete ui;
}

void ElectrodeSettingWidget::beforeLoad()
{

}

void ElectrodeSettingWidget::beforeUnload()
{

}
