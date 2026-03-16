#include "SolverTimeDefinitionPage.h"

#include "ui_SolverTimeDefinitionPage.h"

SolverTimeDefinitionPage::SolverTimeDefinitionPage(QWidget* parent) : BasePage("SolverTimeDefinitionPage"), ui(new Ui::SolverTimeDefinitionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverTimeDefinitionPage::~SolverTimeDefinitionPage()
{
    delete ui;
}
