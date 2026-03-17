#include "SolidMechanicsDisplayPage.h"

#include "ui_SolidMechanicsDisplay.h"

SolidMechanicsDisplayPage::SolidMechanicsDisplayPage(QWidget* parent)
    : BasePage("SolidMechanicsDisplayPage"), ui(new Ui::SolidMechanicsDisplay)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidMechanicsDisplayPage::~SolidMechanicsDisplayPage()
{
    delete ui;
}

void SolidMechanicsDisplayPage::beforeLoad()
{
}

void SolidMechanicsDisplayPage::beforeUnload()
{
}
