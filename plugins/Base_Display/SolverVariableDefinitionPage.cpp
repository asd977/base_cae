#include "SolverVariableDefinitionPage.h"

#include "ui_SolverVariableDefinitionPage.h"

SolverVariableDefinitionPage::SolverVariableDefinitionPage(QWidget* parent) : BasePage("SolverVariableDefinitionPage"), ui(new Ui::SolverVariableDefinitionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverVariableDefinitionPage::~SolverVariableDefinitionPage()
{
    delete ui;
}
