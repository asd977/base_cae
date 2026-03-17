#include "SolverMeshSettingPage.h"

#include "ui_SolverMeshSettingPage.h"

SolverMeshSettingPage::SolverMeshSettingPage(QWidget* parent) : BasePage("SolverMeshSettingPage"), ui(new Ui::SolverMeshSettingPage)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolverMeshSettingPage::~SolverMeshSettingPage()
{
    delete ui;
}
