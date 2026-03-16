#include "SolverTimeWidget.h"

#include "ui_SolverTimeWidget.h"

SolverTimeWidget::SolverTimeWidget(QWidget* parent) : BasePage("SolverTimeWidget"), ui(new Ui::SolverTimeWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverTimeWidget::~SolverTimeWidget()
{
    delete ui;
}
