#include "AcousticOutputWidget.h"

#include "ui_AcousticOutputWidget.h"

AcousticOutputWidget::AcousticOutputWidget(QWidget* parent) : BasePage("AcousticOutputWidget"), ui(new Ui::AcousticOutputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticOutputWidget::~AcousticOutputWidget()
{
    delete ui;
}
