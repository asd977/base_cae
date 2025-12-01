#ifndef VTKWIDGET_H
#define VTKWIDGET_H

#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkAutoInit.h>
#include <vtkSmartPointer.h>
#include <vtkAxesActor.h>
#include <vtkUnstructuredGrid.h>
#include "PVCameraToolbar.h"

#include <QMainWindow>

#include <QInputDialog>
#include <QDir>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui { class vtkWidget; }
QT_END_NAMESPACE

class vtkWidget : public QMainWindow
{
    Q_OBJECT

public:
    explicit vtkWidget(QWidget *parent = nullptr);
    ~vtkWidget();
    
    vtkSmartPointer<vtkRenderWindow> getrenderWindow();
    vtkSmartPointer<vtkRenderWindowInteractor> getInteractor();
    PVCameraToolbar* getCameraToolbar();
private:
    Ui::vtkWidget *ui;
    void initVTKWidget();
    
    vtkSmartPointer<vtkRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkRenderWindowInteractor> m_renderWindowInteractor;
    
    PVCameraToolbar *m_pvCameraToolbar;

};
#endif // VTKWIDGET_H

