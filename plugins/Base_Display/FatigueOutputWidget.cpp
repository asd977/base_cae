#include "FatigueOutputWidget.h"

#include "ui_FatigueOutputWidget.h"

FatigueOutputWidget::FatigueOutputWidget(QWidget* parent) : BasePage("FatigueOutputWidget"), ui(new Ui::FatigueOutputWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

FatigueOutputWidget::~FatigueOutputWidget()
{
    delete ui;
}
