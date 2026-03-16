#include "ParameterOptimizationProcessPage.h"

#include "ui_ParameterOptimizationProcessPage.h"

ParameterOptimizationProcessPage::ParameterOptimizationProcessPage(QWidget* parent) : BasePage("ParameterOptimizationProcessPage"), ui(new Ui::ParameterOptimizationProcessPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

ParameterOptimizationProcessPage::~ParameterOptimizationProcessPage()
{
    delete ui;
}
