#include "CurveWidget.h"
#include "ui_CurveWidget.h"

CurveWidget::CurveWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CurveWidget)
{
    ui->setupUi(this);
}

CurveWidget::~CurveWidget()
{
    delete ui;
}
