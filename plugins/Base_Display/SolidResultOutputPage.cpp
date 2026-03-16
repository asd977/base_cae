#include "SolidResultOutputPage.h"

#include "ui_SolidResultOutputPage.h"

SolidResultOutputPage::SolidResultOutputPage(QWidget* parent) : BasePage("SolidResultOutputPage"), ui(new Ui::SolidResultOutputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidResultOutputPage::~SolidResultOutputPage()
{
    delete ui;
}
