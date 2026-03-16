#include "TopologyOptimizationInputPage.h"

#include "ui_TopologyOptimizationInputPage.h"

TopologyOptimizationInputPage::TopologyOptimizationInputPage(QWidget* parent) : BasePage("TopologyOptimizationInputPage"), ui(new Ui::TopologyOptimizationInputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

TopologyOptimizationInputPage::~TopologyOptimizationInputPage()
{
    delete ui;
}
