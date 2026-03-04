#include "VisualizationWidget.h"

#include <ServiceRegistry.h>
#include <services/UiFrameService.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QResizeEvent>
#include <QSlider>
#include <QSurfaceFormat>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <cmath>

// VTK
#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCubeAxesActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProp.h>
#include <vtkProp3D.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>

VisualizationWidget::VisualizationWidget(QWidget* parent) : QWidget(parent) {
  QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

  // 注册服务：this 同时是 QWidget + VisualWidgetService
  ServiceRegistry::instance().registerService<VisualWidgetService>(this);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_vtkWidget = new QVTKOpenGLNativeWidget(this);
  layout->addWidget(m_vtkWidget);

  initVtk();
  initViewToolbar();

  UiFrameService* uiFrameService =
      ServiceRegistry::instance().getService<UiFrameService>();
  if (uiFrameService) {
    uiFrameService->addWidget(this, "可视化");
  }
}

VisualizationWidget::~VisualizationWidget() = default;

void VisualizationWidget::initVtk() {
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

  // ---------- Cube axes (scale ruler around content) ----------
  m_cubeAxesActor = vtkSmartPointer<vtkCubeAxesActor>::New();
  m_cubeAxesActor->SetCamera(m_renderer->GetActiveCamera());
  m_cubeAxesActor->SetFlyModeToOuterEdges();
  m_cubeAxesActor->DrawXGridlinesOff();
  m_cubeAxesActor->DrawYGridlinesOff();
  m_cubeAxesActor->DrawZGridlinesOff();
  m_cubeAxesActor->SetXTitle("X");
  m_cubeAxesActor->SetYTitle("Y");
  m_cubeAxesActor->SetZTitle("Z");
  m_cubeAxesActor->GetTitleTextProperty(0)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetTitleTextProperty(1)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetTitleTextProperty(2)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetLabelTextProperty(0)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetLabelTextProperty(1)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetLabelTextProperty(2)->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetXAxesLinesProperty()->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetYAxesLinesProperty()->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->GetZAxesLinesProperty()->SetColor(0.0, 0.0, 0.0);
  m_cubeAxesActor->SetVisibility(0);
  m_renderer->AddActor(m_cubeAxesActor);

  // ---------- Orientation marker ----------
  m_axes = vtkSmartPointer<vtkAxesActor>::New();
  m_omw = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
  m_omw->SetOrientationMarker(m_axes);
  m_omw->SetInteractor(m_vtkWidget->renderWindow()->GetInteractor());
  m_omw->SetEnabled(1);
  m_omw->InteractiveOff();
  m_omw->SetViewport(0.0, 0.0, 0.15, 0.15);

  resetCamera(false);
  render();
}

void VisualizationWidget::initViewToolbar() {
  m_toolbar = new QFrame(this);
  m_toolbar->setObjectName("visualizationViewToolbar");
  m_toolbar->setAttribute(Qt::WA_StyledBackground, true);

  auto* root = new QHBoxLayout(m_toolbar);
  root->setContentsMargins(12, 6, 12, 6);
  root->setSpacing(4);

  auto makeIconBtn = [this](const QString& iconPath, const QString& tip,
                            auto fn) {
    auto* btn = new QToolButton(m_toolbar);
    btn->setAutoRaise(true);
    btn->setIcon(QIcon(iconPath));
    btn->setIconSize(QSize(20, 20));
    btn->setToolTip(tip);
    btn->setFixedSize(40, 36);
    connect(btn, &QToolButton::clicked, this, fn);
    return btn;
  };

  auto addSep = [this, root]() {
    auto* sep = new QFrame(m_toolbar);
    sep->setFrameShape(QFrame::VLine);
    sep->setObjectName("visualizationToolbarSeparator");
    sep->setFixedHeight(26);
    root->addWidget(sep);
  };

  root->addWidget(makeIconBtn(":/resources/icon/view_fit.svg",
                              tr("重置并自适应视图"),
                              [this]() { resetCamera(true); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_rotate_left.svg",
                              tr("逆时针旋转"),
                              [this]() { rotateCamera(-12.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_rotate_right.svg",
                              tr("顺时针旋转"),
                              [this]() { rotateCamera(12.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_zoom_in.svg", tr("放大"),
                              [this]() { zoomCamera(1.15); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_zoom_out.svg", tr("缩小"),
                              [this]() { zoomCamera(1.0 / 1.15); }));

  m_projectionButton = makeIconBtn(QString(), tr("切换透视/正交"), [this]() {
    m_isParallelProjection = !m_isParallelProjection;
    if (m_renderer && m_renderer->GetActiveCamera()) {
      m_renderer->GetActiveCamera()->SetParallelProjection(
          m_isParallelProjection ? 1 : 0);
    }
    updateProjectionButtonIcon();
    render();
  });
  updateProjectionButtonIcon();
  root->addWidget(m_projectionButton);

  m_scaleButton = makeIconBtn(QString(), tr("显示/隐藏尺度标尺"), [this]() {
    m_isScaleVisible = !m_isScaleVisible;
    if (m_cubeAxesActor && m_renderer) {
      double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
      m_renderer->ComputeVisiblePropBounds(bounds);
      const bool hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] &&
                             bounds[4] <= bounds[5];
      if (hasBounds) {
        m_cubeAxesActor->SetBounds(bounds);
        m_cubeAxesActor->SetVisibility(m_isScaleVisible ? 1 : 0);
      } else {
        m_cubeAxesActor->SetVisibility(0);
      }
    }
    updateScaleButtonIcon();
    render();
  });
  updateScaleButtonIcon();
  root->addWidget(m_scaleButton);

  addSep();

  root->addWidget(makeIconBtn(":/resources/icon/view_front.png", tr("前视图"),
                              [this]() { setCameraToAxis(0.0, -1.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_back.png", tr("后视图"),
                              [this]() { setCameraToAxis(0.0, 1.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_left.png", tr("左视图"),
                              [this]() { setCameraToAxis(-1.0, 0.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_right.png", tr("右视图"),
                              [this]() { setCameraToAxis(1.0, 0.0, 0.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_top.png", tr("顶视图"),
                              [this]() { setCameraToAxis(0.0, 0.0, 1.0); }));
  root->addWidget(makeIconBtn(":/resources/icon/view_bottom.png", tr("底视图"),
                              [this]() { setCameraToAxis(0.0, 0.0, -1.0); }));

  root->addStretch(1);

  m_opacityButton = new QToolButton(m_toolbar);
  m_opacityButton->setAutoRaise(true);
  m_opacityButton->setToolTip(tr("整体透明度"));
  m_opacityButton->setText(tr("100% ▾"));
  m_opacityButton->setIcon(QIcon(":/resources/icon/view_opacity.svg"));
  m_opacityButton->setIconSize(QSize(16, 16));
  m_opacityButton->setFixedSize(92, 36);
  m_opacityButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  root->addWidget(m_opacityButton);

  m_opacityMenu = new QMenu(m_toolbar);
  m_opacityMenu->setWindowFlags(m_opacityMenu->windowFlags() |
                                Qt::FramelessWindowHint);
  m_opacityMenu->setObjectName("visualizationOpacityMenu");

  auto* popupWidget = new QWidget(m_opacityMenu);
  auto* popupLayout = new QVBoxLayout(popupWidget);
  popupLayout->setContentsMargins(10, 8, 10, 8);
  popupLayout->setSpacing(8);

  m_opacityPercentLabel = new QLabel(tr("100%"), popupWidget);
  m_opacityPercentLabel->setAlignment(Qt::AlignCenter);
  m_opacityPercentLabel->setObjectName("visualizationOpacityPercentLabel");
  popupLayout->addWidget(m_opacityPercentLabel);

  m_globalOpacitySlider = new QSlider(Qt::Vertical, popupWidget);
  m_globalOpacitySlider->setRange(15, 100);
  m_globalOpacitySlider->setFixedHeight(140);
  m_globalOpacitySlider->setValue(100);
  m_globalOpacitySlider->setObjectName("visualizationOpacitySlider");
  popupLayout->addWidget(m_globalOpacitySlider, 1, Qt::AlignHCenter);

  connect(m_globalOpacitySlider, &QSlider::valueChanged, this,
          [this](int value) {
            applyGlobalOpacity(static_cast<double>(value) / 100.0);
            if (m_opacityPercentLabel)
              m_opacityPercentLabel->setText(QString::number(value) + "%");
            if (m_opacityButton)
              m_opacityButton->setText(QString::number(value) + "% ▾");
          });

  auto* action = new QWidgetAction(m_opacityMenu);
  action->setDefaultWidget(popupWidget);
  m_opacityMenu->addAction(action);

  connect(m_opacityButton, &QToolButton::clicked, this, [this]() {
    if (!m_opacityMenu || !m_opacityButton) return;
    const QPoint p =
        m_opacityButton->mapToGlobal(QPoint(0, m_opacityButton->height() + 4));
    m_opacityMenu->popup(p);
  });

  const int toolbarHeight = 48;
  m_toolbar->setGeometry(0, 0, width(), toolbarHeight);
  m_toolbar->show();
}

void VisualizationWidget::render() {
  if (m_renderWindow) m_renderWindow->Render();
}

void VisualizationWidget::resetCamera(bool renderNow) {
  if (m_renderer) m_renderer->ResetCamera();
  if (renderNow) render();
}

vtkRenderer* VisualizationWidget::renderer() const { return m_renderer; }

vtkRenderWindow* VisualizationWidget::renderWindow() const {
  return m_renderWindow;
}

vtkRenderWindowInteractor* VisualizationWidget::interactor() const {
  return m_vtkWidget ? m_vtkWidget->renderWindow()->GetInteractor() : nullptr;
}

// ---------------- Actor registry ----------------

bool VisualizationWidget::addProp(const QString& id,
                                  vtkSmartPointer<vtkProp> prop,
                                  const QString& group, const QString& name,
                                  bool resetCam, bool renderNow) {
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

  if (m_cubeAxesActor && m_renderer && m_isScaleVisible) {
    double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
    m_renderer->ComputeVisiblePropBounds(bounds);
    const bool hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] &&
                           bounds[4] <= bounds[5];
    m_cubeAxesActor->SetVisibility(hasBounds ? 1 : 0);
    if (hasBounds) m_cubeAxesActor->SetBounds(bounds);
  }

  if (resetCam) resetCamera(false);
  if (renderNow) render();
  return true;
}

bool VisualizationWidget::removeProp(const QString& id, bool renderNow) {
  auto it = m_registry.find(id);
  if (it == m_registry.end()) return false;

  if (it.value().prop) m_renderer->RemoveViewProp(it.value().prop);

  m_registry.erase(it);

  if (m_cubeAxesActor && m_renderer) {
    double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
    m_renderer->ComputeVisiblePropBounds(bounds);
    const bool hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] &&
                           bounds[4] <= bounds[5];
    m_cubeAxesActor->SetVisibility((m_isScaleVisible && hasBounds) ? 1 : 0);
    if (hasBounds) m_cubeAxesActor->SetBounds(bounds);
  }

  if (renderNow) render();
  return true;
}

void VisualizationWidget::clearAll(bool renderNow) {
  for (auto it = m_registry.begin(); it != m_registry.end(); ++it) {
    if (it.value().prop) m_renderer->RemoveViewProp(it.value().prop);
  }
  m_registry.clear();

  if (m_cubeAxesActor) m_cubeAxesActor->SetVisibility(0);

  if (renderNow) render();
}

vtkProp* VisualizationWidget::getProp(const QString& id) const {
  auto it = m_registry.find(id);
  return (it == m_registry.end()) ? nullptr : it.value().prop;
}

QList<QString> VisualizationWidget::allIds() const { return m_registry.keys(); }

bool VisualizationWidget::setVisible(const QString& id, bool on,
                                     bool renderNow) {
  auto it = m_registry.find(id);
  if (it == m_registry.end()) return false;

  it.value().visible = on;
  if (it.value().prop) it.value().prop->SetVisibility(on ? 1 : 0);

  if (m_cubeAxesActor && m_renderer) {
    double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
    m_renderer->ComputeVisiblePropBounds(bounds);
    const bool hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] &&
                           bounds[4] <= bounds[5];
    m_cubeAxesActor->SetVisibility((m_isScaleVisible && hasBounds) ? 1 : 0);
    if (hasBounds) m_cubeAxesActor->SetBounds(bounds);
  }

  if (renderNow) render();
  return true;
}

void VisualizationWidget::setGroupVisible(const QString& group, bool on,
                                          bool renderNow) {
  for (auto it = m_registry.begin(); it != m_registry.end(); ++it) {
    if (it.value().group == group && it.value().prop) {
      it.value().visible = on;
      it.value().prop->SetVisibility(on ? 1 : 0);
    }
  }

  if (m_cubeAxesActor && m_renderer) {
    double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
    m_renderer->ComputeVisiblePropBounds(bounds);
    const bool hasBounds = bounds[0] <= bounds[1] && bounds[2] <= bounds[3] &&
                           bounds[4] <= bounds[5];
    m_cubeAxesActor->SetVisibility((m_isScaleVisible && hasBounds) ? 1 : 0);
    if (hasBounds) m_cubeAxesActor->SetBounds(bounds);
  }
  if (renderNow) render();
}

bool VisualizationWidget::setOpacity(const QString& id, double alpha,
                                     bool renderNow) {
  auto it = m_registry.find(id);
  if (it == m_registry.end()) return false;

  auto* actor = vtkActor::SafeDownCast(it.value().prop);
  if (!actor) return false;

  actor->GetProperty()->SetOpacity(alpha);
  if (renderNow) render();
  return true;
}

void VisualizationWidget::applyGlobalOpacity(double alpha, bool renderNow) {
  const double clamped = qBound(0.05, alpha, 1.0);
  for (auto it = m_registry.begin(); it != m_registry.end(); ++it) {
    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor) continue;
    actor->GetProperty()->SetOpacity(clamped);
  }
  if (renderNow) render();
}

bool VisualizationWidget::setTransform(const QString& id, vtkTransform* tr,
                                       bool renderNow) {
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
                                         bool renderNow) {
  if (!lut) return;

  m_lut = lut;
  m_scalarBar->SetLookupTable(m_lut);

  if (applyToAllActors) {
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it) {
      vtkMapper* mp = actorMapper(it.value().prop);
      if (mp) mp->SetLookupTable(m_lut);
    }
  }

  if (renderNow) render();
}

vtkLookupTable* VisualizationWidget::lookupTable() const { return m_lut; }

vtkScalarBarActor* VisualizationWidget::scalarBarActor() const {
  return m_scalarBar;
}

void VisualizationWidget::showScalarBar(bool on, bool renderNow) {
  if (m_scalarBar) m_scalarBar->SetVisibility(on ? 1 : 0);
  if (renderNow) render();
}

bool VisualizationWidget::isScalarBarVisible() const {
  return m_scalarBar && m_scalarBar->GetVisibility() != 0;
}

void VisualizationWidget::setScalarBarTitle(const QString& title,
                                            bool renderNow) {
  if (m_scalarBar) m_scalarBar->SetTitle(title.toUtf8().constData());
  if (renderNow) render();
}

void VisualizationWidget::setScalarBarNumberOfLabels(int n, bool renderNow) {
  if (m_scalarBar) m_scalarBar->SetNumberOfLabels(n);
  if (renderNow) render();
}

vtkMapper* VisualizationWidget::actorMapper(vtkProp* prop) {
  auto* actor = vtkActor::SafeDownCast(prop);
  return actor ? actor->GetMapper() : nullptr;
}

bool VisualizationWidget::applyLookupTableToActor(
    const QString& id, const QString& scalarArrayName, double rangeMin,
    double rangeMax, bool useRange, bool renderNow) {
  auto it = m_registry.find(id);
  if (it == m_registry.end()) return false;

  auto* actor = vtkActor::SafeDownCast(it.value().prop);
  if (!actor) return false;

  vtkMapper* mp = actor->GetMapper();
  if (!mp) return false;

  mp->SetLookupTable(m_lut);

  if (!scalarArrayName.isEmpty()) {
    mp->SelectColorArray(scalarArrayName.toUtf8().constData());
    mp->SetScalarModeToUsePointFieldData();  // 如需 cell
                                             // data：SetScalarModeToUseCellFieldData()
    mp->ScalarVisibilityOn();
  }

  if (useRange) {
    mp->SetScalarRange(rangeMin, rangeMax);
  }

  mp->SetColorModeToMapScalars();

  if (renderNow) render();
  return true;
}

bool VisualizationWidget::updateActorPolyData(const QString& id,
                                              vtkPolyData* poly,
                                              bool renderNow) {
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

void VisualizationWidget::showOrientationMarker(bool on) {
  if (!m_omw) return;
  m_omw->SetEnabled(on ? 1 : 0);
  render();
}

void VisualizationWidget::setCameraToAxis(double x, double y, double z) {
  if (!m_renderer || !m_renderer->GetActiveCamera()) return;

  m_renderer->ResetCamera();
  vtkCamera* camera = m_renderer->GetActiveCamera();

  double fp[3] = {0.0, 0.0, 0.0};
  double pos[3] = {0.0, 0.0, 1.0};
  camera->GetFocalPoint(fp);
  camera->GetPosition(pos);

  const double distance = std::sqrt((pos[0] - fp[0]) * (pos[0] - fp[0]) +
                                    (pos[1] - fp[1]) * (pos[1] - fp[1]) +
                                    (pos[2] - fp[2]) * (pos[2] - fp[2]));

  camera->SetPosition(fp[0] + x * distance, fp[1] + y * distance,
                      fp[2] + z * distance);
  camera->SetViewUp(0.0, 0.0, 1.0);
  if (z != 0.0) {
    camera->SetViewUp(0.0, 1.0, 0.0);
  }

  m_renderer->ResetCameraClippingRange();
  render();
}

void VisualizationWidget::rotateCamera(double azimuthDeg, double elevationDeg) {
  if (!m_renderer || !m_renderer->GetActiveCamera()) return;
  vtkCamera* camera = m_renderer->GetActiveCamera();
  camera->Azimuth(azimuthDeg);
  camera->Elevation(elevationDeg);
  camera->OrthogonalizeViewUp();
  m_renderer->ResetCameraClippingRange();
  render();
}

void VisualizationWidget::zoomCamera(double factor) {
  if (!m_renderer || !m_renderer->GetActiveCamera()) return;
  vtkCamera* camera = m_renderer->GetActiveCamera();
  camera->Dolly(factor);
  m_renderer->ResetCameraClippingRange();
  render();
}

void VisualizationWidget::updateProjectionButtonIcon() {
  if (!m_projectionButton) return;
  const QString icon = m_isParallelProjection
                           ? QString(":/resources/icon/parallel.png")
                           : QString(":/resources/icon/perspective.png");
  m_projectionButton->setIcon(QIcon(icon));
  m_projectionButton->setIconSize(QSize(18, 18));
}

void VisualizationWidget::updateScaleButtonIcon() {
  if (!m_scaleButton) return;
  const QString icon = m_isScaleVisible
                           ? QString(":/resources/icon/view_scale_on.svg")
                           : QString(":/resources/icon/view_scale_off.svg");
  m_scaleButton->setIcon(QIcon(icon));
  m_scaleButton->setIconSize(QSize(18, 18));
}

void VisualizationWidget::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (m_toolbar) {
    const int toolbarHeight = 48;
    m_toolbar->setGeometry(0, 0, width(), toolbarHeight);
  }
}
