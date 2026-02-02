#include "PostProcessingWidget.h"
#include "ui_PostProcessingWidget.h"

#include <QToolBar>
#include <vtkRenderWindow.h>
#include <QDebug>
#include <vtkCamera.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkTextProperty.h>
#include <vtkColorTransferFunction.h>

PostProcessingWidget::PostProcessingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PostProcessingWidget)
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
    ui->setupUi(this);


    // m_render = vtkSmartPointer<vtkRenderer>::New();

    // m_render->SetBackground(231.0 / 255.0, 242.0 / 255.0, 255.0 / 255.0);
    // m_render->SetBackground2(1.0,1.0,1.0);
    // m_render->SetGradientBackground(true);

    // vtkSmartPointer<vtkRenderWindowInteractor> getInteractor();

    // m_renderWindow =  ui->vtkWidget->renderWindow();
    // m_renderWindow->AddRenderer(m_render);

    // m_renderWindowInteractor = ui->vtkWidget->interactor();
    // m_renderWindowInteractor->SetRenderWindow(m_renderWindow);

    // QToolBar *m_toolBar = new QToolBar(ui->vtkWidget);
    // m_toolBar->setOrientation(Qt::Vertical);
    // m_toolBar->setIconSize(QSize(30, 30));
    // m_toolBar->setAllowedAreas(Qt::LeftToolBarArea);

    // m_fitView = new QAction(QIcon(":/resources/icon/autofit.png"), tr("FitView"), m_toolBar);
    // m_XplusView = new QAction(QIcon(":/resources/icon/view_right.png"), tr("X+"), m_toolBar);
    // m_XminView = new QAction(QIcon(":/resources/icon/view_left.png"), tr("X-"), m_toolBar);
    // m_YplusView = new QAction(QIcon(":/resources/icon/view_top.png"), tr("Y+"), m_toolBar);
    // m_YminView = new QAction(QIcon(":/resources/icon/view_bottom.png"), tr("Y-"), m_toolBar);
    // m_ZplusView = new QAction(QIcon(":/resources/icon/view_front.png"), tr("Z+"), m_toolBar);
    // m_ZminView = new QAction(QIcon(":/resources/icon/view_back.png"), tr("Z-"), m_toolBar);
    // m_perspective = new QAction(QIcon(":/resources/icon/perspective.png"), tr("Perspective"), m_toolBar);


    // connect(m_fitView, &QAction::triggered, this, [=]() {this->setView(ViewType::FitView); m_render->Render(); });
    // connect(m_XplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::XPlusView); m_render->Render(); });
    // connect(m_XminView, &QAction::triggered, this, [=]() {this->setView(ViewType::XMinusView); m_render->Render(); });
    // connect(m_YplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::YPlusView); m_render->Render(); });
    // connect(m_YminView, &QAction::triggered, this, [=]() {this->setView(ViewType::YMinusView); m_render->Render(); });
    // connect(m_ZplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::ZPlusView); m_render->Render(); });
    // connect(m_ZminView, &QAction::triggered, this, [=]() {this->setView(ViewType::ZMinusView); m_render->Render(); });

    // m_toolBar->addActions(this->getViewActions());

    // initScalarBar();
    // createOrientationMarkerWidget();
}

vtkSmartPointer<vtkScalarBarActor> PostProcessingWidget::getScalarBarActor()
{
    return m_scalarBarActor;
}

vtkSmartPointer<vtkLookupTable> PostProcessingWidget::getLookupTable()
{
    return m_lookupTable;
}

PostProcessingWidget::~PostProcessingWidget()
{
    delete ui;
}

QList<QAction *> PostProcessingWidget::getViewActions() const
{
    return { m_fitView, m_XplusView, m_XminView, m_YplusView, m_YminView, m_ZplusView, m_ZminView , m_perspective,m_configView };
}

void PostProcessingWidget::setView(ViewType type)
{
    switch (type)
    {
    case ViewType::FitView:
        fitView(); break;
    case ViewType::XPlusView:
        setViewValue(-1, 0, 0, 0, 0, 1, 0, 0, 0);
        break;
    case ViewType::XMinusView:
        setViewValue(1, 0, 0, 0, 0, 1, 0, 0, 0);
        break;
    case ViewType::YPlusView:
        setViewValue(0, -1, 0, 0, 0, 1, 0, 0, 0);
        break;
    case ViewType::YMinusView:
        setViewValue(0, 1, 0, 0, 0, 1, 0, 0, 0);
        break;
    case ViewType::ZPlusView:
        setViewValue(0, 0, -1, 0, 1, 0, 0, 0, 0);
        break;
    case ViewType::ZMinusView:
        setViewValue(0, 0, 1, 0, 1, 0, 0, 0, 0);
        break;
    }
}

void PostProcessingWidget::showEvent(QShowEvent *event)
{
    if (m_renderWindowInteractor)
    {
        m_renderWindow->Render();
    }
}

void PostProcessingWidget::fitView()
{
    m_render->ResetCamera();
}

void PostProcessingWidget::addActor(vtkSmartPointer<vtkActor> actor)
{
    m_render->AddActor(actor);
}

void PostProcessingWidget::removeActor(vtkSmartPointer<vtkActor> actor)
{
    m_render->RemoveActor(actor);
    m_render->Render();
}

void PostProcessingWidget::setViewValue(double x1, double x2, double x3, double y1, double y2, double y3, double z1, double z2, double z3)
{
    vtkCamera* camera = m_render->GetActiveCamera();
    camera->SetViewUp(y1, y2, y3);
    camera->SetPosition(z1, z2, z3);
    camera->SetFocalPoint(x1, x2, x3);
    m_render->ResetCamera();
}

void PostProcessingWidget::createOrientationMarkerWidget()
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
}

void PostProcessingWidget::render()
{
    m_render->Render();
}

vtkRenderWindow* PostProcessingWidget::getRenderWindow()
{
    return ui->vtkWidget->renderWindow();
}

vtkRenderer* PostProcessingWidget::getRender()
{
    return m_render;
}

void PostProcessingWidget::initScalarBar()
{
    m_scalarBarActor = vtkSmartPointer<vtkScalarBarActor>::New();
    m_scalarBarActor->GetPositionCoordinate()->SetCoordinateSystemToNormalizedDisplay();
    m_scalarBarActor->GetPositionCoordinate()->SetValue(0.85, 0.05);
    m_scalarBarActor->SetOrientationToVertical();
    m_scalarBarActor->SetWidth(0.15);
    m_scalarBarActor->SetHeight(0.80);

    m_scalarBarActor->SetUnconstrainedFontSize(true);

    vtkTextProperty* titleProp = m_scalarBarActor->GetTitleTextProperty();
    titleProp->SetFontFamilyToArial();
    titleProp->SetBold(true);
    titleProp->SetItalic(false);
    titleProp->SetShadow(false);
    titleProp->SetFontSize(20);
    titleProp->SetColor(0.10, 0.10, 0.10);

    vtkTextProperty* labelProp = m_scalarBarActor->GetLabelTextProperty();
    labelProp->SetFontFamilyToArial();
    titleProp->SetJustificationToCentered();
    labelProp->SetBold(false);
    labelProp->SetItalic(false);
    labelProp->SetShadow(false);
    labelProp->SetFontSize(16);
    labelProp->SetColor(0.15, 0.15, 0.15);

    const int kNumLabels = 5;
    m_scalarBarActor->SetNumberOfLabels(kNumLabels);
    m_scalarBarActor->SetLabelFormat("%.2e");
    m_scalarBarActor->SetBarRatio(0.22);
    m_scalarBarActor->SetTextPositionToSucceedScalarBar();


    m_lookupTable = vtkSmartPointer<vtkLookupTable>::New();
    m_lookupTable->SetTableRange(0.0, 1.0);
    m_lookupTable->SetHueRange(0.667 , 0.0);
    m_lookupTable->SetNumberOfTableValues(256);
    m_lookupTable->Build();

    m_scalarBarActor->SetMaximumNumberOfColors(256);

    m_scalarBarActor->SetLookupTable(m_lookupTable);
    m_scalarBarActor->SetTitle("Value");

    m_render->AddActor(m_scalarBarActor);
}
