#include "VisualizationExpressionPage.h"

#include "ui_VisualizationExpressionPage.h"

VisualizationExpressionPage::VisualizationExpressionPage(QWidget* parent) : BasePage("VisualizationExpressionPage"), ui(new Ui::VisualizationExpressionPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

VisualizationExpressionPage::~VisualizationExpressionPage()
{
    delete ui;
}
