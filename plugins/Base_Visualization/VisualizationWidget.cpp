#include "VisualizationWidget.h"

#include <QSurfaceFormat>
#include <QFrame>
#include <QToolButton>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QResizeEvent>

#include <cmath>

#include <services/UiFrameService.h>
#include <ServiceRegistry.h>

// VTK
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkLookupTable.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkProp.h>
#include <vtkProp3D.h>
#include <vtkTransform.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkMapper.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkCamera.h>

VisualizationWidget::VisualizationWidget(QWidget* parent)
    : QWidget(parent)
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    // 注册服务：this 同时是 QWidget + VisualWidgetService
    ServiceRegistry::instance().registerService<VisualWidgetService>(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    qDebug()<<"test1";
    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    qDebug()<<"test2";
    layout->addWidget(m_vtkWidget);

    qDebug()<<"test3";
    initVtk();
    initViewToolbar();
    qDebug()<<"test4";

    UiFrameService* uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    if (uiFrameService) {
        uiFrameService->addWidget(this, "可视化");
    }
}

VisualizationWidget::~VisualizationWidget() = default;

void VisualizationWidget::initVtk()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetBackground(231.0 / 255.0, 242.0 / 255.0, 255.0 / 255.0);
    m_renderer->SetBackground2(1.0, 1.0, 1.0);
    m_renderer->SetGradientBackground(true);
    m_renderWindow->AddRenderer(m_renderer);

    // ---------- LUT ----------
    m_lut = vtkSmartPointer<vtkLookupTable>::New();
    m_lut->SetNumberOfTableValues(256);
    m_lut->Build();

    // ---------- ScalarBar ----------
    m_scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_scalarBar->SetLookupTable(m_lut);
    m_scalarBar->SetTitle(" ");
    m_scalarBar->SetNumberOfLabels(5);
    m_scalarBar->SetVisibility(false);

    m_scalarBar->GetTitleTextProperty()->SetFontSize(14);
    m_scalarBar->GetLabelTextProperty()->SetFontSize(12);

    m_renderer->AddActor2D(m_scalarBar);

    // ---------- Orientation marker ----------
    m_axes = vtkSmartPointer<vtkAxesActor>::New();
    m_omw  = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    m_omw->SetOrientationMarker(m_axes);
    m_omw->SetInteractor(m_vtkWidget->renderWindow()->GetInteractor());
    m_omw->SetEnabled(1);
    m_omw->InteractiveOff();
    m_omw->SetViewport(0.0, 0.0, 0.15, 0.15);

    resetCamera(false);
    render();
}

void VisualizationWidget::initViewToolbar()
{
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName("visualizationViewToolbar");
    m_toolbar->setStyleSheet(
        "QFrame#visualizationViewToolbar {"
        "background: rgba(255, 255, 255, 215);"
        "border: 1px solid rgba(120, 120, 120, 120);"
        "border-radius: 8px;"
        "}"
        "QToolButton {"
        "min-width: 28px; min-height: 28px;"
        "border: none; border-radius: 4px;"
        "padding: 2px;"
        "}"
        "QToolButton:hover { background: rgba(64, 158, 255, 45); }"
        "QToolButton:pressed { background: rgba(64, 158, 255, 75); }"
    );

    auto* layout = new QVBoxLayout(m_toolbar);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(4);

    auto makeBtn = [this, layout](const QString& iconPath, const QString& tip, auto fn) {
        auto* btn = new QToolButton(m_toolbar);
        btn->setIcon(QIcon(iconPath));
        btn->setIconSize(QSize(18, 18));
        btn->setToolTip(tip);
        connect(btn, &QToolButton::clicked, this, fn);
        layout->addWidget(btn);
        return btn;
    };

    makeBtn(":/resources/icon/view_fit.svg", tr("重置并自适应视图"), [this]() { resetCamera(true); });
    makeBtn(":/resources/icon/view_front.png", tr("前视图"), [this]() { setCameraToAxis(0.0, -1.0, 0.0); });
    makeBtn(":/resources/icon/view_back.png", tr("后视图"), [this]() { setCameraToAxis(0.0, 1.0, 0.0); });
    makeBtn(":/resources/icon/view_left.png", tr("左视图"), [this]() { setCameraToAxis(-1.0, 0.0, 0.0); });
    makeBtn(":/resources/icon/view_right.png", tr("右视图"), [this]() { setCameraToAxis(1.0, 0.0, 0.0); });
    makeBtn(":/resources/icon/view_top.png", tr("顶视图"), [this]() { setCameraToAxis(0.0, 0.0, 1.0); });
    makeBtn(":/resources/icon/view_bottom.png", tr("底视图"), [this]() { setCameraToAxis(0.0, 0.0, -1.0); });
    makeBtn(":/resources/icon/view_rotate_left.svg", tr("逆时针旋转"), [this]() { rotateCamera(-12.0, 0.0); });
    makeBtn(":/resources/icon/view_rotate_right.svg", tr("顺时针旋转"), [this]() { rotateCamera(12.0, 0.0); });
    makeBtn(":/resources/icon/view_zoom_in.svg", tr("放大"), [this]() { zoomCamera(1.15); });
    makeBtn(":/resources/icon/view_zoom_out.svg", tr("缩小"), [this]() { zoomCamera(1.0 / 1.15); });

    m_projectionButton = makeBtn(QString(), tr("切换透视/正交"), [this]() {
        m_isParallelProjection = !m_isParallelProjection;
        if (m_renderer && m_renderer->GetActiveCamera()) {
            m_renderer->GetActiveCamera()->SetParallelProjection(m_isParallelProjection ? 1 : 0);
        }
        updateProjectionButtonIcon();
        render();
    });
    updateProjectionButtonIcon();

    auto* opacityWrap = new QWidget(m_toolbar);
    auto* opacityLayout = new QHBoxLayout(opacityWrap);
    opacityLayout->setContentsMargins(0, 4, 0, 0);
    opacityLayout->setSpacing(6);

    auto* opacityIcon = new QLabel(opacityWrap);
    opacityIcon->setPixmap(QIcon(":/resources/icon/view_opacity.svg").pixmap(16, 16));
    opacityLayout->addWidget(opacityIcon);

    m_globalOpacitySlider = new QSlider(Qt::Horizontal, opacityWrap);
    m_globalOpacitySlider->setRange(15, 100);
    m_globalOpacitySlider->setValue(100);
    m_globalOpacitySlider->setToolTip(tr("整体透明度"));
    connect(m_globalOpacitySlider, &QSlider::valueChanged, this, [this](int value) {
        applyGlobalOpacity(static_cast<double>(value) / 100.0);
    });
    opacityLayout->addWidget(m_globalOpacitySlider, 1);
    layout->addWidget(opacityWrap);

    m_toolbar->setMinimumWidth(210);
    m_toolbar->show();
}

void VisualizationWidget::render()
{
    if (m_renderWindow) m_renderWindow->Render();
}

void VisualizationWidget::resetCamera(bool renderNow)
{
    if (m_renderer) m_renderer->ResetCamera();
    if (renderNow) render();
}

vtkRenderer* VisualizationWidget::renderer() const
{
    return m_renderer;
}

vtkRenderWindow* VisualizationWidget::renderWindow() const
{
    return m_renderWindow;
}

vtkRenderWindowInteractor* VisualizationWidget::interactor() const
{
    return m_vtkWidget ? m_vtkWidget->renderWindow()->GetInteractor() : nullptr;
}

// ---------------- Actor registry ----------------

bool VisualizationWidget::addProp(const QString& id,
                                  vtkSmartPointer<vtkProp> prop,
                                  const QString& group,
                                  const QString& name,
                                  bool resetCam,
                                  bool renderNow)
{
    if (id.isEmpty() || !prop) return false;
    if (m_registry.contains(id)) return false;

    Item it;
    it.id = id;
    it.name = name;
    it.group = group;
    it.prop = prop;
    it.visible = true;

    m_renderer->AddViewProp(prop.GetPointer());
    m_registry.insert(id, it);

    if (resetCam) resetCamera(false);
    if (renderNow) render();
    return true;
}

bool VisualizationWidget::removeProp(const QString& id, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    if (it.value().prop)
        m_renderer->RemoveViewProp(it.value().prop);

    m_registry.erase(it);

    if (renderNow) render();
    return true;
}

void VisualizationWidget::clearAll(bool renderNow)
{
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        if (it.value().prop)
            m_renderer->RemoveViewProp(it.value().prop);
    }
    m_registry.clear();

    if (renderNow) render();
}

vtkProp* VisualizationWidget::getProp(const QString& id) const
{
    auto it = m_registry.find(id);
    return (it == m_registry.end()) ? nullptr : it.value().prop;
}

QList<QString> VisualizationWidget::allIds() const
{
    return m_registry.keys();
}

bool VisualizationWidget::setVisible(const QString& id, bool on, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    it.value().visible = on;
    if (it.value().prop)
        it.value().prop->SetVisibility(on ? 1 : 0);

    if (renderNow) render();
    return true;
}

void VisualizationWidget::setGroupVisible(const QString& group, bool on, bool renderNow)
{
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        if (it.value().group == group && it.value().prop)
        {
            it.value().visible = on;
            it.value().prop->SetVisibility(on ? 1 : 0);
        }
    }
    if (renderNow) render();
}

bool VisualizationWidget::setOpacity(const QString& id, double alpha, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor) return false;

    actor->GetProperty()->SetOpacity(alpha);
    if (renderNow) render();
    return true;
}

void VisualizationWidget::applyGlobalOpacity(double alpha, bool renderNow)
{
    const double clamped = qBound(0.05, alpha, 1.0);
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        auto* actor = vtkActor::SafeDownCast(it.value().prop);
        if (!actor) continue;
        actor->GetProperty()->SetOpacity(clamped);
    }
    if (renderNow) render();
}

bool VisualizationWidget::setTransform(const QString& id, vtkTransform* tr, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    auto* p3d = vtkProp3D::SafeDownCast(it.value().prop);
    if (!p3d) return false;

    p3d->SetUserTransform(tr);
    if (renderNow) render();
    return true;
}

// ---------------- LUT & ScalarBar ----------------

void VisualizationWidget::setLookupTable(vtkLookupTable* lut,
                                         bool applyToAllActors,
                                         bool renderNow)
{
    if (!lut) return;

    m_lut = lut;
    m_scalarBar->SetLookupTable(m_lut);

    if (applyToAllActors)
    {
        for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
        {
            vtkMapper* mp = actorMapper(it.value().prop);
            if (mp) mp->SetLookupTable(m_lut);
        }
    }

    if (renderNow) render();
}

vtkLookupTable* VisualizationWidget::lookupTable() const
{
    return m_lut;
}

vtkScalarBarActor* VisualizationWidget::scalarBarActor() const
{
    return m_scalarBar;
}

void VisualizationWidget::showScalarBar(bool on, bool renderNow)
{
    if (m_scalarBar)
        m_scalarBar->SetVisibility(on ? 1 : 0);
    if (renderNow) render();
}

bool VisualizationWidget::isScalarBarVisible() const
{
    return m_scalarBar && m_scalarBar->GetVisibility() != 0;
}

void VisualizationWidget::setScalarBarTitle(const QString& title, bool renderNow)
{
    if (m_scalarBar)
        m_scalarBar->SetTitle(title.toUtf8().constData());
    if (renderNow) render();
}

void VisualizationWidget::setScalarBarNumberOfLabels(int n, bool renderNow)
{
    if (m_scalarBar)
        m_scalarBar->SetNumberOfLabels(n);
    if (renderNow) render();
}

vtkMapper* VisualizationWidget::actorMapper(vtkProp* prop)
{
    auto* actor = vtkActor::SafeDownCast(prop);
    return actor ? actor->GetMapper() : nullptr;
}

bool VisualizationWidget::applyLookupTableToActor(const QString& id,
                                                  const QString& scalarArrayName,
                                                  double rangeMin,
                                                  double rangeMax,
                                                  bool useRange,
                                                  bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor) return false;

    vtkMapper* mp = actor->GetMapper();
    if (!mp) return false;

    mp->SetLookupTable(m_lut);

    if (!scalarArrayName.isEmpty())
    {
        mp->SelectColorArray(scalarArrayName.toUtf8().constData());
        mp->SetScalarModeToUsePointFieldData(); // 如需 cell data：SetScalarModeToUseCellFieldData()
        mp->ScalarVisibilityOn();
    }

    if (useRange)
    {
        mp->SetScalarRange(rangeMin, rangeMax);
    }

    mp->SetColorModeToMapScalars();

    if (renderNow) render();
    return true;
}

bool VisualizationWidget::updateActorPolyData(const QString& id, vtkPolyData* poly, bool renderNow)
{
    if (!poly) return false;

    auto it = m_registry.find(id);
    if (it == m_registry.end()) return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor) return false;

    auto* pdMapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    if (!pdMapper) return false;

    pdMapper->SetInputData(poly);
    pdMapper->Update();

    if (renderNow) render();
    return true;
}

// ---------------- Orientation marker ----------------

void VisualizationWidget::showOrientationMarker(bool on)
{
    if (!m_omw) return;
    m_omw->SetEnabled(on ? 1 : 0);
    render();
}

void VisualizationWidget::setCameraToAxis(double x, double y, double z)
{
    if (!m_renderer || !m_renderer->GetActiveCamera()) return;

    m_renderer->ResetCamera();
    vtkCamera* camera = m_renderer->GetActiveCamera();

    double fp[3] = {0.0, 0.0, 0.0};
    double pos[3] = {0.0, 0.0, 1.0};
    camera->GetFocalPoint(fp);
    camera->GetPosition(pos);

    const double distance = std::sqrt((pos[0] - fp[0]) * (pos[0] - fp[0])
                                    + (pos[1] - fp[1]) * (pos[1] - fp[1])
                                    + (pos[2] - fp[2]) * (pos[2] - fp[2]));

    camera->SetPosition(fp[0] + x * distance, fp[1] + y * distance, fp[2] + z * distance);
    camera->SetViewUp(0.0, 0.0, 1.0);
    if (z != 0.0) {
        camera->SetViewUp(0.0, 1.0, 0.0);
    }

    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::rotateCamera(double azimuthDeg, double elevationDeg)
{
    if (!m_renderer || !m_renderer->GetActiveCamera()) return;
    vtkCamera* camera = m_renderer->GetActiveCamera();
    camera->Azimuth(azimuthDeg);
    camera->Elevation(elevationDeg);
    camera->OrthogonalizeViewUp();
    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::zoomCamera(double factor)
{
    if (!m_renderer || !m_renderer->GetActiveCamera()) return;
    vtkCamera* camera = m_renderer->GetActiveCamera();
    camera->Dolly(factor);
    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::updateProjectionButtonIcon()
{
    if (!m_projectionButton) return;
    const QString icon = m_isParallelProjection
        ? QStringLiteral(":/resources/icon/parallel.png")
        : QStringLiteral(":/resources/icon/perspective.png");
    m_projectionButton->setIcon(QIcon(icon));
    m_projectionButton->setIconSize(QSize(18, 18));
}

void VisualizationWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_toolbar) {
        const int margin = 12;
        m_toolbar->move(width() - m_toolbar->width() - margin, margin);
    }
}
