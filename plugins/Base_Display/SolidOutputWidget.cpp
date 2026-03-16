#include "SolidOutputWidget.h"

#include "ui_SolidOutputWidget.h"

SolidOutputWidget::SolidOutputWidget(QWidget* parent) : BasePage("SolidOutputWidget"), ui(new Ui::SolidOutputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidOutputWidget::~SolidOutputWidget()
{
    delete ui;
}
