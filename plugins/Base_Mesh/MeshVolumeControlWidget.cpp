#include "MeshVolumeControlWidget.h"
#include "ui_MeshVolumeControlWidget.h"

MeshVolumeControlWidget::MeshVolumeControlWidget(QWidget *parent)
    : BasePage("MeshVolumeControlWidget")
    , ui(new Ui::MeshVolumeControlWidget)
{
    ui->setupUi(this);
}

MeshVolumeControlWidget::~MeshVolumeControlWidget()
{
    delete ui;
}
