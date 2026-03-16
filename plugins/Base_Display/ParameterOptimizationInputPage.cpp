#include "ParameterOptimizationInputPage.h"

#include "ui_ParameterOptimizationInputPage.h"

ParameterOptimizationInputPage::ParameterOptimizationInputPage(QWidget* parent) : BasePage("ParameterOptimizationInputPage"), ui(new Ui::ParameterOptimizationInputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

ParameterOptimizationInputPage::~ParameterOptimizationInputPage()
{
    delete ui;
}
