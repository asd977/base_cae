#include "SeepageWidget.h"
#include "ui_SeepageWidget.h"

SeepageWidget::SeepageWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SeepageWidget)
{
    ui->setupUi(this);
}

SeepageWidget::~SeepageWidget()
{
    delete ui;
}
