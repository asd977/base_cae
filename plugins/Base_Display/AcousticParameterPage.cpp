#include "AcousticParameterPage.h"

#include "ui_AcousticParameterPage.h"

AcousticParameterPage::AcousticParameterPage(QWidget* parent) : BasePage("AcousticParameterPage"), ui(new Ui::AcousticParameterPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticParameterPage::~AcousticParameterPage()
{
    delete ui;
}
