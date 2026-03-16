#include "OptimizationInputWidget.h"

#include "ui_OptimizationInputWidget.h"

OptimizationInputWidget::OptimizationInputWidget(QWidget* parent) : BasePage("OptimizationInputWidget"), ui(new Ui::OptimizationInputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

OptimizationInputWidget::~OptimizationInputWidget()
{
    delete ui;
}
