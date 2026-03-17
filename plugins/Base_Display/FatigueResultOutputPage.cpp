#include "FatigueResultOutputPage.h"

#include "ui_FatigueResultOutputPage.h"

FatigueResultOutputPage::FatigueResultOutputPage(QWidget* parent) : BasePage("FatigueResultOutputPage"), ui(new Ui::FatigueResultOutputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

FatigueResultOutputPage::~FatigueResultOutputPage()
{
    delete ui;
}
