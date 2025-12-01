#include "ElectrolysisSettingWidget.h"
#include "ui_ElectrolysisSettingWidget.h"

ElectrolysisSettingWidget::ElectrolysisSettingWidget(QWidget *parent) :
    ui(new Ui::ElectrolysisSettingWidget),
    BasePage("ElectrolysisSettingWidget")
{
    ui->setupUi(this);
}

ElectrolysisSettingWidget::~ElectrolysisSettingWidget()
{
    delete ui;
}

void ElectrolysisSettingWidget::beforeLoad()
{

}

void ElectrolysisSettingWidget::beforeUnload()
{

}
