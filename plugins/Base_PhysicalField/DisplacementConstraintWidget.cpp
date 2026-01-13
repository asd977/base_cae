#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget *parent)
    : BasePage("DisplacementConstraintWidget")
    , ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
    delete ui;
}

void DisplacementConstraintWidget::beforeLoad()
{

}

void DisplacementConstraintWidget::beforeUnload()
{

}
