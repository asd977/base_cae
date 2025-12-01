#include "MetalSettingWidget.h"
#include "ui_MetalSettingWidget.h"

MetalSettingWidget::MetalSettingWidget(QWidget *parent) :
    ui(new Ui::MetalSettingWidget),
    BasePage("MetalSettingWidget")
{
    ui->setupUi(this);
}

MetalSettingWidget::~MetalSettingWidget()
{
    delete ui;
}

void MetalSettingWidget::beforeLoad()
{

}

void MetalSettingWidget::beforeUnload()
{

}
