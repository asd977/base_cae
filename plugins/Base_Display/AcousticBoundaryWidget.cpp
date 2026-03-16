#include "AcousticBoundaryWidget.h"

#include "ui_AcousticBoundaryWidget.h"

AcousticBoundaryWidget::AcousticBoundaryWidget(QWidget* parent) : BasePage("AcousticBoundaryWidget"), ui(new Ui::AcousticBoundaryWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

AcousticBoundaryWidget::~AcousticBoundaryWidget()
{
    delete ui;
}
