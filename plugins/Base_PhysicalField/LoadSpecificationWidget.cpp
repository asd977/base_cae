#include "LoadSpecificationWidget.h"
#include "ui_LoadSpecificationWidget.h"

LoadSpecificationWidget::LoadSpecificationWidget(QWidget *parent)
    : BasePage("LoadSpecificationWidget")
    , ui(new Ui::LoadSpecificationWidget)
{
    ui->setupUi(this);
}

LoadSpecificationWidget::~LoadSpecificationWidget()
{
    delete ui;
}

void LoadSpecificationWidget::beforeLoad()
{

}

void LoadSpecificationWidget::beforeUnload()
{

}
