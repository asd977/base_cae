#include "MeshFaceControlWidget.h"
#include "ui_MeshFaceControlWidget.h"

MeshFaceControlWidget::MeshFaceControlWidget(QWidget *parent)
    : BasePage("MeshFaceControlWidget")
    , ui(new Ui::MeshFaceControlWidget)
{
    ui->setupUi(this);
}

MeshFaceControlWidget::~MeshFaceControlWidget()
{
    delete ui;
}
