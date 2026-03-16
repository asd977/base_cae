#include "TopologyObjectiveFunctionPage.h"

#include "ui_TopologyObjectiveFunctionPage.h"

TopologyObjectiveFunctionPage::TopologyObjectiveFunctionPage(QWidget* parent) : BasePage("TopologyObjectiveFunctionPage"), ui(new Ui::TopologyObjectiveFunctionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

TopologyObjectiveFunctionPage::~TopologyObjectiveFunctionPage()
{
    delete ui;
}
