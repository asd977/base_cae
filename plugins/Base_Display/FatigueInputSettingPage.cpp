#include "FatigueInputSettingPage.h"

#include "ui_FatigueInputSettingPage.h"

FatigueInputSettingPage::FatigueInputSettingPage(QWidget* parent) : BasePage("FatigueInputSettingPage"), ui(new Ui::FatigueInputSettingPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

FatigueInputSettingPage::~FatigueInputSettingPage()
{
    delete ui;
}
