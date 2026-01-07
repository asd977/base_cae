#include "ConstraintSettingWidget.h"
#include "ui_ConstraintSettingWidget.h"

ConstraintSettingWidget::ConstraintSettingWidget(QWidget *parent)
    : ui(new Ui::ConstraintSettingWidget)
    , BasePage("ConstraintSettingWidget")
{
    ui->setupUi(this);
}

ConstraintSettingWidget::~ConstraintSettingWidget()
{
    delete ui;
}

void ConstraintSettingWidget::beforeLoad()
{
}

void ConstraintSettingWidget::beforeUnload()
{
}
