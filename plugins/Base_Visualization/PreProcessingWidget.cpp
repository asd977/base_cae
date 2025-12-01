#include "PreProcessingWidget.h"
#include "ui_PreProcessingWidget.h"

PreProcessingWidget::PreProcessingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PreProcessingWidget)
{
    ui->setupUi(this);
}

PreProcessingWidget::~PreProcessingWidget()
{
    delete ui;
}
