#include "MeshEdgeControlWidget.h"
#include "ui_MeshEdgeControlWidget.h"

MeshEdgeControlWidget::MeshEdgeControlWidget(QWidget *parent)
    : BasePage("MeshEdgeControlWidget")
    , ui(new Ui::MeshEdgeControlWidget)
{
    ui->setupUi(this);
}

MeshEdgeControlWidget::~MeshEdgeControlWidget()
{
    delete ui;
}
