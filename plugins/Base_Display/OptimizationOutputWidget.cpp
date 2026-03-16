#include "OptimizationOutputWidget.h"

#include "ui_OptimizationOutputWidget.h"

OptimizationOutputWidget::OptimizationOutputWidget(QWidget* parent) : BasePage("OptimizationOutputWidget"), ui(new Ui::OptimizationOutputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

OptimizationOutputWidget::~OptimizationOutputWidget()
{
    delete ui;
}
