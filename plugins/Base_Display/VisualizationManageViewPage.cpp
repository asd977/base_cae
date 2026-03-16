#include "VisualizationManageViewPage.h"

#include "ui_VisualizationManageViewPage.h"

VisualizationManageViewPage::VisualizationManageViewPage(QWidget* parent) : BasePage("VisualizationManageViewPage"), ui(new Ui::VisualizationManageViewPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

VisualizationManageViewPage::~VisualizationManageViewPage()
{
    delete ui;
}
