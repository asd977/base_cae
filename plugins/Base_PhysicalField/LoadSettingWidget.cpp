#include "LoadSettingWidget.h"
#include "ui_LoadSettingWidget.h"

LoadSettingWidget::LoadSettingWidget(QWidget *parent)
    : ui(new Ui::LoadSettingWidget)
    , BasePage("LoadSettingWidget")
{
    ui->setupUi(this);
}

LoadSettingWidget::~LoadSettingWidget()
{
    delete ui;
}

void LoadSettingWidget::beforeLoad()
{
}

void LoadSettingWidget::beforeUnload()
{
}
