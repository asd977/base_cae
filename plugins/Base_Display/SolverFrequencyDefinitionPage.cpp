#include "SolverFrequencyDefinitionPage.h"

#include "ui_SolverFrequencyDefinitionPage.h"

SolverFrequencyDefinitionPage::SolverFrequencyDefinitionPage(QWidget* parent) : BasePage("SolverFrequencyDefinitionPage"), ui(new Ui::SolverFrequencyDefinitionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverFrequencyDefinitionPage::~SolverFrequencyDefinitionPage()
{
    delete ui;
}
