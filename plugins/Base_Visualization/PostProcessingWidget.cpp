#include "PostProcessingWidget.h"
#include "ui_PostProcessingWidget.h"

PostProcessingWidget::PostProcessingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PostProcessingWidget)
{
    ui->setupUi(this);
}

PostProcessingWidget::~PostProcessingWidget()
{
    delete ui;
}
