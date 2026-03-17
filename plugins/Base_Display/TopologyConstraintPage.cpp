#include "TopologyConstraintPage.h"

#include "ui_TopologyConstraintPage.h"

TopologyConstraintPage::TopologyConstraintPage(QWidget* parent) : BasePage("TopologyConstraintPage"), ui(new Ui::TopologyConstraintPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

TopologyConstraintPage::~TopologyConstraintPage()
{
    delete ui;
}
