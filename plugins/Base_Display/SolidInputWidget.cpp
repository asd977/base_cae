#include "SolidInputWidget.h"

#include "ui_SolidInputWidget.h"

SolidInputWidget::SolidInputWidget(QWidget* parent) : BasePage("SolidInputWidget"), ui(new Ui::SolidInputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidInputWidget::~SolidInputWidget()
{
    delete ui;
}
