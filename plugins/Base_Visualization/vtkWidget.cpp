#include "vtkWidget.h"
#include "ui_vtkWidget.h"

#include <QInputDialog>
#include <QDir>
#include <QString>

vtkWidget::vtkWidget(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::vtkWidget)
{
    ui->setupUi(this);
    m_pvCameraToolbar = new PVCameraToolbar;


    this->addToolBar(Qt::TopToolBarArea,m_pvCameraToolbar);
    initVTKWidget();
}

vtkWidget::~vtkWidget()
{
    delete ui;
    ui = NULL;
}

//初始化vtk窗口
void vtkWidget::initVTKWidget()
{
    m_renderWindow = ui->vtkwidget->renderWindow();
    m_renderWindowInteractor = ui->vtkwidget->interactor();
}

vtkSmartPointer<vtkRenderWindow> vtkWidget::getrenderWindow()
{
    return m_renderWindow;
}

vtkSmartPointer<vtkRenderWindowInteractor> vtkWidget::getInteractor()
{
    return m_renderWindowInteractor;
}

PVCameraToolbar* vtkWidget::getCameraToolbar()
{
    return m_pvCameraToolbar;
}















