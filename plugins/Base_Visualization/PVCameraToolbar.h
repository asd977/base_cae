#ifndef PVCAMERATOOLBAR_H
#define PVCAMERATOOLBAR_H

#include "ui_PVCameraToolbar.h"

#include <QToolBar>

#include <QInputDialog>
#include <QDir>
#include <QString>
#include <QWidget>

class PVCameraToolbar : public QToolBar
{
    Q_OBJECT

public:
    PVCameraToolbar(QWidget *parent = nullptr);
    ~PVCameraToolbar();

//private slots:
public:
    Ui::PVCameraToolbar *ui;
    //PVCameraToolbar *pvCameraToolbar3;
};
#endif // PVCAMERATOOLBAR_
