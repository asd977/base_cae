#include "OtherSettingsWidget.h"

#include "ui_OtherSettingsWidget.h"

OtherSettingsWidget::OtherSettingsWidget(QWidget* parent) : BasePage("OtherSettingsWidget"), ui(new Ui::OtherSettingsWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

OtherSettingsWidget::~OtherSettingsWidget()
{
    delete ui;
}
