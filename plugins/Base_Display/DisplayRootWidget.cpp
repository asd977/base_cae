#include "DisplayRootWidget.h"

#include "ui_DisplayRootWidget.h"

DisplayRootWidget::DisplayRootWidget(QWidget* parent) : BasePage("DisplayRootWidget"), ui(new Ui::DisplayRootWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
}

DisplayRootWidget::~DisplayRootWidget()
{
    delete ui;
}
