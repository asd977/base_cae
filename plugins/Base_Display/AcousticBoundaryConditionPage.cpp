#include "AcousticBoundaryConditionPage.h"

#include "ui_AcousticBoundaryConditionPage.h"

AcousticBoundaryConditionPage::AcousticBoundaryConditionPage(QWidget* parent) : BasePage("AcousticBoundaryConditionPage"), ui(new Ui::AcousticBoundaryConditionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticBoundaryConditionPage::~AcousticBoundaryConditionPage()
{
    delete ui;
}
