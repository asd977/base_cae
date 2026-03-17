#include "TopologyResultOutputPage.h"

#include "ui_TopologyResultOutputPage.h"

TopologyResultOutputPage::TopologyResultOutputPage(QWidget* parent) : BasePage("TopologyResultOutputPage"), ui(new Ui::TopologyResultOutputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

TopologyResultOutputPage::~TopologyResultOutputPage()
{
    delete ui;
}
