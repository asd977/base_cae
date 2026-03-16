#include "VisualizationStyleAssistPage.h"

#include "ui_VisualizationStyleAssistPage.h"

VisualizationStyleAssistPage::VisualizationStyleAssistPage(QWidget* parent) : BasePage("VisualizationStyleAssistPage"), ui(new Ui::VisualizationStyleAssistPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

VisualizationStyleAssistPage::~VisualizationStyleAssistPage()
{
    delete ui;
}
