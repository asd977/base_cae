#include "VisualizationWidget.h"

#include <QSurfaceFormat>

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
    // 常规色带：低值蓝色 -> 高值红色
    m_lut->SetHueRange(2.0 / 3.0, 0.0);
    m_lut->SetSaturationRange(1.0, 1.0);
    m_lut->SetValueRange(1.0, 1.0);
    m_lut->Build();

    // ---------- ScalarBar ----------
    m_scalarBar = vtkSmartPointer<vtkScalarBarActor>::New();
    m_scalarBar->SetLookupTable(m_lut);
    m_scalarBar->SetTitle(" ");
    m_scalarBar->SetNumberOfLabels(5);
    m_scalarBar->SetVisibility(false);
    // 缩小色度条尺寸，避免遮挡主视图
    m_scalarBar->SetWidth(0.06);
    m_scalarBar->SetHeight(0.45);
    m_scalarBar->SetPosition(0.91, 0.10);

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


void VisualizationWidget::on_horizontalSliderOpacity_valueChanged(int value)
{
//    if (!m_visualWidgetService) return;

//    double alpha = qBound(10, value, 100) / 100.0;

//    const QStringList ids = availableMeshPropIds();
//    if (ids.isEmpty()) return;

//    for (const QString &id : ids) {
//        m_visualWidgetService->setOpacity(id, alpha, false);
//    }
//    m_visualWidgetService->render();
}
