#include "FillStorageCyclePage.h"

#include "ui_FillStorageCyclePage.h"

FillStorageCyclePage::FillStorageCyclePage(QWidget* parent)
    : BasePage("FillStorageCycle"), ui(new Ui::FillStorageCyclePage)
{
    ui->setupUi(this);
}

FillStorageCyclePage::~FillStorageCyclePage()
{
    delete ui;
}

void FillStorageCyclePage::beforeLoad()
{
}

void FillStorageCyclePage::beforeUnload()
{
}
