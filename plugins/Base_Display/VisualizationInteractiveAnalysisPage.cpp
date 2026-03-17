#include "VisualizationInteractiveAnalysisPage.h"

#include "ui_VisualizationInteractiveAnalysisPage.h"

VisualizationInteractiveAnalysisPage::VisualizationInteractiveAnalysisPage(QWidget* parent) : BasePage("VisualizationInteractiveAnalysisPage"), ui(new Ui::VisualizationInteractiveAnalysisPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

VisualizationInteractiveAnalysisPage::~VisualizationInteractiveAnalysisPage()
{
    delete ui;
}
