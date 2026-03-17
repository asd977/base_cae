#include "AcousticResultOutputPage.h"

#include "ui_AcousticResultOutputPage.h"

AcousticResultOutputPage::AcousticResultOutputPage(QWidget* parent) : BasePage("AcousticResultOutputPage"), ui(new Ui::AcousticResultOutputPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticResultOutputPage::~AcousticResultOutputPage()
{
    delete ui;
}
