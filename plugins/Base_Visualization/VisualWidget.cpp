#include "VisualWidget.h"
#include "ui_VisualWidget.h"
#include <vtkWindowToImageFilter.h>
#include <vtkJPEGWriter.h>
#include <vtkPNGWriter.h>
#include "PVCameraToolbar.h"
#include <ServiceRegistry.h>
#include <vtkScalarBarRepresentation.h>

VisualWidget::VisualWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::VisualWidget)
{
    ui->setupUi(this);

}

void VisualWidget::initVTKWidget()
{
    m_renderer = vtkSmartPointer<vtkRenderer>::New();

    m_renderer->SetBackground(231.0 / 255.0, 242.0 / 255.0, 255.0 / 255.0);
    m_renderer->SetBackground2(1.0,1.0,1.0);
    m_renderer->SetGradientBackground(true);
    
    vtkSmartPointer<vtkRenderWindowInteractor> getInteractor();

    m_renderWindow =  m_vtkWidget->getrenderWindow();
    m_renderWindow->AddRenderer(m_renderer);

    m_renderWindowInteractor = m_vtkWidget->getInteractor();
    m_renderWindowInteractor->SetRenderWindow(m_renderWindow);

    // 创建坐标轴
    // 创建一个坐标轴actor
    this->createOrientationMarkerWidget();
    this->createScalarBarWidgetWidget();

    if(m_mainActor != nullptr) m_renderer->RemoveActor(m_mainActor);
}

void VisualWidget::createScalarBarWidgetWidget()
{
    vtkSmartPointer<vtkLookupTable> lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetTableRange(-999, 999);
    lut->SetHueRange(0.67, 0.0);
    lut->Build();
    m_scalarbar->SetLookupTable(lut);
    
    m_scalarBarWidget->SetInteractor(m_renderWindowInteractor);
    m_scalarBarWidget->SetScalarBarActor(m_scalarbar);
    
    m_scalarBarWidget->SelectableOff();
    m_scalarBarWidget->ResizableOff();

    m_scalarBarWidget->RepositionableOn();
    m_scalarBarWidget->GetScalarBarRepresentation()->PickableOn();
    m_scalarBarWidget->GetScalarBarRepresentation()->SetOrientation(1);
    m_scalarBarWidget->GetScalarBarRepresentation()->SetPosition(0.9, 0.2);
    m_scalarBarWidget->GetScalarBarRepresentation()->SetPosition2(0.06, 0.7);
    m_scalarBarWidget->GetScalarBarRepresentation()->SetScalarBarActor(m_scalarbar);
    m_scalarBarWidget->Off();
}

void VisualWidget::setScalarBarWidgetWidgetOnOrOff(int i_value)
{
    if(0==i_value)
    {
        m_scalarBarWidget->Off();
    }
    else
    {
        m_scalarBarWidget->On();
    }
}

void VisualWidget::changeShowType(int show_type)
{
    ui->stackedWidget->setCurrentIndex(show_type);
}

void VisualWidget::setChart(chartCurveUseTimes *chart_use)
{
    // 获取当前图表
    QChart *currentChart = ui->graphicsView_curve->chart();

    delete currentChart;
    currentChart = nullptr; // 将指针设置为 nullptr

    ui->graphicsView_curve->setChart((QChart*)chart_use);
    ui->graphicsView_curve->setRenderHint(QPainter::Antialiasing);
}

void VisualWidget::createOrientationMarkerWidget()
{
    vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
    vtkOrientationMarkerWidget *orientationMarkerWidget =
            vtkOrientationMarkerWidget::New();
    orientationMarkerWidget->SetOutlineColor( 0.9300, 0.5700, 0.1300 );
    orientationMarkerWidget->SetOrientationMarker(axes);
    orientationMarkerWidget->SetInteractor(m_renderWindowInteractor);
    orientationMarkerWidget->SetViewport( 0.0, 0.0, 0.2, 0.2 ); // 调整大小和位置
    orientationMarkerWidget->SetEnabled(true);
    orientationMarkerWidget->InteractiveOff();
    //orientationMarkerWidget->InteractiveOff();
}

void VisualWidget::initCameraToolBarAction()
{
    connect(m_vtkWidget->getCameraToolbar()->ui->actionResetCamera, &QAction::triggered, this, &VisualWidget::resetCamera);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionPositiveX, &QAction::triggered, this, &VisualWidget::PositiveX);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionNegativeX, &QAction::triggered, this, &VisualWidget::NegativeX);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionPositiveY, &QAction::triggered, this, &VisualWidget::PositiveY);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionNegativeY, &QAction::triggered, this, &VisualWidget::NegativeY);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionPositiveZ, &QAction::triggered, this, &VisualWidget::PositiveZ);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionNegativeZ, &QAction::triggered, this, &VisualWidget::NegativeZ);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionRotate90degCW, &QAction::triggered, this, &VisualWidget::rotateCamera_cw);
    connect(m_vtkWidget->getCameraToolbar()->ui->actionRotate90degCCW, &QAction::triggered, this, &VisualWidget::rotateCamera_ccw);
    connect(m_vtkWidget->getCameraToolbar()->ui->action_Isometric, &QAction::triggered, this, &VisualWidget::resetCameraToSideView);
}

vtkSmartPointer<vtkRenderer> VisualWidget::getRender()
{
    return m_renderer;
}

vtkSmartPointer<vtkRenderWindow> VisualWidget::getrenderWindow()
{
    return m_renderWindow;
}

vtkSmartPointer<vtkRenderWindowInteractor> VisualWidget::getInteractor()
{
    return m_renderWindowInteractor;
}

void VisualWidget::saveImage(QString file_name,int i_type)
{
    if(ui->stackedWidget->currentIndex()==0)
        return;
    vtkWindowToImageFilter *wif = vtkWindowToImageFilter::New();
    wif->SetInput(m_renderWindow);
    wif->Update();

    // vtkPNGWriter writer = new vtkPNGWriter;
    if(i_type==1)
    {
        vtkJPEGWriter *writer_jpeg = vtkJPEGWriter::New();
        writer_jpeg->SetFileName(file_name.toStdString().c_str());
        writer_jpeg->SetInputConnection(wif->GetOutputPort());
        writer_jpeg->Write();
    }
    else if(i_type==0)
    {
        vtkPNGWriter *writer_png = vtkPNGWriter::New();
        writer_png->SetFileName(file_name.toStdString().c_str());
        writer_png->SetInputConnection(wif->GetOutputPort());
        writer_png->Write();
    }
    return;
}

//读标量文件
//void VisualWidget::nephogramTypeIndexChanged(int i)
//{
//    std::cout<<i<<std::endl;
//    m_vtkScalar->change(i);
//    m_renderWindow->Render();
//}

void VisualWidget::addActor(vtkSmartPointer<vtkActor> actor)
{
    m_renderer->AddActor(actor);
    m_tmpActor.append(actor);
    //    m_renderer->ResetCamera();
    //    m_renderWindow->Render();
}

void VisualWidget::addActor2D(vtkSmartPointer<vtkActor2D> actor)
{
    m_renderer->AddActor2D(actor);
    m_scalarBar_actor = actor;
    m_renderWindow->Render();
}

void VisualWidget::addActor2D2(vtkSmartPointer<vtkScalarBarActor> actor)
{
    m_scalarbar = actor;
    m_scalarBarWidget->SetScalarBarActor(m_scalarbar);
    m_renderer->AddActor2D(actor);
    //m_scalarBar_actor = actor;
    m_renderWindow->Render();
}

void VisualWidget::setScalarBar(vtkSmartPointer<vtkScalarBarActor> scalarBar)
{
    m_scalarbar = scalarBar;
    m_scalarBarWidget->SetScalarBarActor(m_scalarbar);
    m_scalarBarWidget->GetScalarBarRepresentation()->SetScalarBarActor(m_scalarbar);
}

void VisualWidget::removeActor(vtkSmartPointer<vtkActor> actor)
{
    m_renderer->RemoveActor(actor);
}

void VisualWidget:: removeAllActor()
{
    for(int i=0; i<m_tmpActor.size(); i++)
    {
        m_renderer->RemoveActor(m_tmpActor[i]);
    }
    if(m_scalarBar_actor!=nullptr) m_renderer->RemoveActor2D(m_scalarBar_actor);
    m_scalarBar_actor = nullptr;
    m_tmpActor.clear();
    m_renderWindow->Render();
}

void VisualWidget::removeAllActor2()
{
    m_renderer->RemoveAllViewProps();
    if(m_scalarBar_actor!=nullptr) m_renderer->RemoveActor2D(m_scalarBar_actor);
    m_scalarBar_actor = nullptr;
    m_tmpActor.clear();
    m_renderWindow->Render();
}

void VisualWidget::removeGridActor(vtkSmartPointer<vtkActor> actor)
{
    if(m_gridActor != nullptr)
    {

    }
    m_gridActor = nullptr;
}

void VisualWidget::reFreash()
{
    m_renderWindow->Render();
}

void VisualWidget::reFresh()
{
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

//camera
void VisualWidget::resetViewDir(float look_x, float look_y, float look_z, float up_x, float up_y, float up_z)
{
    vtkCamera *activeCamera = vtkCamera::New();
    activeCamera->SetPosition(0, 0, 0);
    activeCamera->SetFocalPoint(look_x, look_y, look_z);
    activeCamera->SetViewUp(up_x, up_y, up_z);
    m_renderer->SetActiveCamera(activeCamera);
    //resetCamera();
    //m_renderer->ResetCamera();
    //m_renderWindow->Render();
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VisualWidget::resetCamera()
{   
    m_renderer->ResetCamera();
    m_renderWindowInteractor->Initialize();
    m_renderWindow->Render();
}

void VisualWidget::PositiveX()
{
    resetViewDir(1, 0, 0, 0, 0, 1);
}

void VisualWidget::NegativeX()
{
    resetViewDir(-1, 0, 0, 0, 0, 1);
}

void VisualWidget::PositiveY()
{
    resetViewDir(0, 1, 0, 0, 0, 1);
}

void VisualWidget::NegativeY()
{
    resetViewDir(0, -1, 0, 0, 0, 1);
}

void VisualWidget::PositiveZ()
{
    resetViewDir(0, 0, 1, 0, 1, 0);
}

void VisualWidget::NegativeZ()
{
    resetViewDir(0, 0, -1, 0, 1, 0);
}

void VisualWidget::rotateCamera_cw()
{
    m_renderer->GetActiveCamera()->Roll(-90);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VisualWidget::rotateCamera_ccw()
{
    m_renderer->GetActiveCamera()->Roll(90);
    m_renderer->ResetCamera();
    m_renderWindow->Render();
}

void VisualWidget::resetCameraToSideView()
{
    resetViewDir(-1.732, -1.732, -2, -0.707, -0.707, 1.732);
}

VisualWidget::~VisualWidget()
{
    delete ui;
    delete m_vtkWidget;
}

