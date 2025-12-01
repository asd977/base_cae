#include "CurveWidget.h"
#include "ui_CurveWidget.h"
#include <Solver.h>

CurveWidget::CurveWidget(Solver *solver, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CurveWidget)
{
    ui->setupUi(this);
    m_solver = solver;
}

CurveWidget::~CurveWidget()
{
    delete ui;
}

void CurveWidget::on_testPushButton_clicked()
{
    m_solver->test();
}

