#include "SolidBoundaryWidget.h"

#include "ui_SolidBoundaryWidget.h"

SolidBoundaryWidget::SolidBoundaryWidget(QWidget* parent) : BasePage("SolidBoundaryWidget"), ui(new Ui::SolidBoundaryWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

SolidBoundaryWidget::~SolidBoundaryWidget()
{
    delete ui;
}
