#include "SolidBoundaryConditionPage.h"

#include "ui_SolidBoundaryConditionPage.h"

SolidBoundaryConditionPage::SolidBoundaryConditionPage(QWidget* parent) : BasePage("SolidBoundaryConditionPage"), ui(new Ui::SolidBoundaryConditionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidBoundaryConditionPage::~SolidBoundaryConditionPage()
{
    delete ui;
}
