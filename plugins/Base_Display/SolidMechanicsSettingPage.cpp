#include "SolidMechanicsSettingPage.h"

#include "ui_SolidMechanicsSettingPage.h"

SolidMechanicsSettingPage::SolidMechanicsSettingPage(QWidget* parent) : BasePage("SolidMechanicsSettingPage"), ui(new Ui::SolidMechanicsSettingPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidMechanicsSettingPage::~SolidMechanicsSettingPage()
{
    delete ui;
}
