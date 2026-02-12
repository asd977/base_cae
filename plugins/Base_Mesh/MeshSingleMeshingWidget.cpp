#include "MeshSingleMeshingWidget.h"
#include "ui_MeshSingleMeshingWidget.h"

MeshSingleMeshingWidget::MeshSingleMeshingWidget(QWidget *parent)
    : BasePage("MeshSingleMeshingWidget")
    , ui(new Ui::MeshSingleMeshingWidget)
{
    ui->setupUi(this);
}

MeshSingleMeshingWidget::~MeshSingleMeshingWidget()
{
    delete ui;
}
