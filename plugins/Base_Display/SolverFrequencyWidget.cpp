#include "SolverFrequencyWidget.h"

#include "ui_SolverFrequencyWidget.h"

SolverFrequencyWidget::SolverFrequencyWidget(QWidget* parent) : BasePage("SolverFrequencyWidget"), ui(new Ui::SolverFrequencyWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverFrequencyWidget::~SolverFrequencyWidget()
{
    delete ui;
}
