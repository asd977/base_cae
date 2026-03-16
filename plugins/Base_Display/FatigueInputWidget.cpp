#include "FatigueInputWidget.h"

#include "ui_FatigueInputWidget.h"

FatigueInputWidget::FatigueInputWidget(QWidget* parent) : BasePage("FatigueInputWidget"), ui(new Ui::FatigueInputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

FatigueInputWidget::~FatigueInputWidget()
{
    delete ui;
}
