#include "ParameterOptimizationResultPage.h"

#include "ui_ParameterOptimizationResultPage.h"

ParameterOptimizationResultPage::ParameterOptimizationResultPage(QWidget* parent) : BasePage("ParameterOptimizationResultPage"), ui(new Ui::ParameterOptimizationResultPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

ParameterOptimizationResultPage::~ParameterOptimizationResultPage()
{
    delete ui;
}
