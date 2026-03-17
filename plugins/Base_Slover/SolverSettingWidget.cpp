#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

SolverSettingWidget::SolverSettingWidget(QWidget *parent) :
    BasePage("display"),
    ui(new Ui::SolverSettingWidget)
{
    ui->setupUi(this);
}

SolverSettingWidget::~SolverSettingWidget()
{
    delete ui;
}

void SolverSettingWidget::beforeLoad()
{

}

void SolverSettingWidget::beforeUnload()
{

}
