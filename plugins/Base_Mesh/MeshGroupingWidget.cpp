#include "MeshGroupingWidget.h"
#include "ui_MeshGroupingWidget.h"

MeshGroupingWidget::MeshGroupingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::MeshGroupingWidget)
{
    ui->setupUi(this);
}

MeshGroupingWidget::~MeshGroupingWidget()
{
    delete ui;
}
