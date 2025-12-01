
#include <QInputDialog>
#include <QDir>
#include <QString>
#include <iostream>

#include "PVCameraToolbar.h"
#include "ui_PVCameraToolbar.h"
using namespace std; 

PVCameraToolbar::PVCameraToolbar(QWidget *parent)
    :QToolBar(parent)
    , ui(new Ui::PVCameraToolbar)
{
    ui->setupUi(this);
}

PVCameraToolbar::~PVCameraToolbar()
{
    delete ui;
    ui = NULL;
}
