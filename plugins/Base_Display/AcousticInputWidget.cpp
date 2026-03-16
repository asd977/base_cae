#include "AcousticInputWidget.h"

#include "ui_AcousticInputWidget.h"

AcousticInputWidget::AcousticInputWidget(QWidget* parent) : BasePage("AcousticInputWidget"), ui(new Ui::AcousticInputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticInputWidget::~AcousticInputWidget()
{
    delete ui;
}
