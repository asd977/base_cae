#include "VisualizationWidget.h"

#include <ServiceRegistry.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <StyleManager.h>
#include <services/UiFrameService.h>

#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSet>
#include <QSizePolicy>
#include <QSlider>
#include <QStackedWidget>
#include <QSurfaceFormat>
#include <QToolBar>
#include <QToolButton>
#include <QVariantMap>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "CurvePlotWidget.h"
#include <cmath>

// VTK
#include <QFileInfo>
#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCaptionActor2D.h>
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
#include <vtkProperty2D.h>

namespace
{
QString findMicrosoftYaHeiFontFile()
{
#ifdef Q_OS_WIN
    const QString winDir = qEnvironmentVariable("WINDIR", "C:/Windows");
    const QString fontDir = QDir(winDir).filePath("Fonts");

    const QStringList candidates = {
        "msyh.ttc",     // 微软雅黑
        "msyh.ttf",
        "msyhbd.ttc",   // 微软雅黑 Bold
        "msyhbd.ttf",
        "msyhui.ttf"    // Microsoft YaHei UI
    };

    for (const QString& name : candidates)
    {
        const QString path = QDir(fontDir).filePath(name);
        if (QFileInfo::exists(path))
            return path;
    }
#endif
    return QString();
}

void applyYaHeiTextStyle(vtkTextProperty* prop, int fontSize, bool bold = false)
{
    if (!prop)
        return;

    prop->SetFontSize(fontSize);
    prop->SetColor(0.0, 0.0, 0.0);
    prop->SetBold(bold ? 1 : 0);
    prop->SetItalic(0);
    prop->SetShadow(0);

    const QString fontFile = findMicrosoftYaHeiFontFile();
    if (!fontFile.isEmpty())
    {
        const QByteArray bytes = fontFile.toLocal8Bit();
        prop->SetFontFamily(VTK_FONT_FILE);
        prop->SetFontFile(bytes.constData());
    }
    else
    {
        prop->SetFontFamilyToArial();
    }
}

QIcon visualizationThemeIcon(const QString& path)
{
    const bool darkTheme = StyleManager::currentThemeName() == QStringLiteral("dark");
    if (path.isEmpty() || !darkTheme)
        return QIcon(path);

    const QIcon source(path);
    QIcon result;
    const QList<int> sizes = {16, 20, 24, 32, 48, 64};
    for (int size : sizes)
    {
        QPixmap pixmap = source.pixmap(size, size);
        if (pixmap.isNull())
            continue;
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), QColor(QStringLiteral("#dce5ec")));
        painter.end();
        result.addPixmap(pixmap);
    }
    return result.isNull() ? QIcon(path) : result;
}
}

VisualizationWidget::VisualizationWidget(QWidget* parent) : QWidget(parent)
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    // 注册服务：this 同时是 QWidget + VisualWidgetService
    ServiceRegistry::instance().registerService<VisualWidgetService>(this);

    // 可视化区域本身需要跟随 Dock/Splitter 自由缩放，不能再被 ADS 外层滚动区包裹。
    setProperty("dockForceNoScrollArea", true);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setMinimumSize(0, 0);
    m_stack->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    layout->addWidget(m_stack);

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setMinimumSize(0, 0);
    m_vtkWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_stack->addWidget(m_vtkWidget);

    m_curvePlotWidget = new CurvePlotWidget(this);
    m_curvePlotWidget->setMinimumSize(0, 0);
    m_curvePlotWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_stack->addWidget(m_curvePlotWidget);
    m_stack->setCurrentWidget(m_vtkWidget);

    initVtk();
    initViewToolbar();
    if (m_toolbar)
        layout->insertWidget(0, m_toolbar);

    applyUiTheme(StyleManager::currentThemeName());
    EventManager::instance().registerHandler(
        PublicConstants::kEventUiThemeChanged, this,
        [this](const QVariantMap& data)
        {
            applyUiTheme(data.value(QStringLiteral("theme")).toString());
        });

    UiFrameService* uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    if (uiFrameService)
    {
        uiFrameService->addWidget(this, "可视化");
    }
}

VisualizationWidget::~VisualizationWidget() = default;

void VisualizationWidget::initVtk()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
    m_vtkWidget->setRenderWindow(m_renderWindow);
    m_defaultMultiSamples = m_renderWindow->GetMultiSamples();
    m_renderWindow->SetAlphaBitPlanes(1);

    m_renderer = vtkSmartPointer<vtkRenderer>::New();
    m_renderer->SetMaximumNumberOfPeels(8);
    m_renderer->SetOcclusionRatio(0.25);
    m_renderer->SetUseDepthPeeling(false);
    m_renderer->SetBackground(244.0 / 255.0, 246.0 / 255.0, 248.0 / 255.0);
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
    m_scalarBar->SetTitle("");
    m_scalarBar->SetNumberOfLabels(7);          // 默认显示更多刻度，工程结果更易读
    m_scalarBar->SetLabelFormat("%.3g");        // 数值更紧凑
    m_scalarBar->SetMaximumNumberOfColors(256);
    m_scalarBar->SetVisibility(false);

    m_scalarBar->SetOrientationToVertical();
    m_scalarBar->UnconstrainedFontSizeOn();

    // 位置：靠右，但不要太贴边
    m_scalarBar->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
    m_scalarBar->GetPosition2Coordinate()->SetCoordinateSystemToNormalizedViewport();
    m_scalarBar->SetPosition(0.90, 0.20);
    m_scalarBar->SetPosition2(0.055, 0.60);     // 默认适当放大，页面可继续自定义

    // 像素限制：防止窗口大时文字和色条一起变粗变大
    m_scalarBar->SetMaximumWidthInPixels(90);
    m_scalarBar->SetMaximumHeightInPixels(480);

    // 字体：微软雅黑，字号缩小；开启 UnconstrainedFontSize 后字号才不会被色标尺寸重新约束
    applyYaHeiTextStyle(m_scalarBar->GetTitleTextProperty(), 14, true);
    applyYaHeiTextStyle(m_scalarBar->GetLabelTextProperty(), 12, false);

    // 不要背景白框
    m_scalarBar->DrawBackgroundOff();
    m_scalarBar->DrawFrameOff();

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

    // 默认使用透视投影；按钮初始显示 perspective 图标且不处于选中状态。
    m_isParallelProjection = false;
    if (m_renderer && m_renderer->GetActiveCamera())
        m_renderer->GetActiveCamera()->ParallelProjectionOff();

    resetCamera(false);
    render();
}

void VisualizationWidget::initViewToolbar()
{
    m_toolbar = new QFrame(this);
    m_toolbar->setObjectName("visualizationViewToolbar");
    m_toolbar->setAttribute(Qt::WA_StyledBackground, true);
    m_toolbar->setMinimumWidth(0);
    m_toolbar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    auto* root = new QHBoxLayout(m_toolbar);
    root->setContentsMargins(12, 3, 12, 3);
    root->setSpacing(2);

    // 使用 QToolBar 自带的 overflow/extension 机制：空间不足时，Qt 会自动把
    // 放不下的 QAction 收进扩展菜单，不再手动计算宽度或隐藏按钮。
    auto* actionToolbar = new QToolBar(m_toolbar);
    m_actionToolbar = actionToolbar;
    actionToolbar->setObjectName("visualizationActionToolbar");
    actionToolbar->setMovable(false);
    actionToolbar->setFloatable(false);
    actionToolbar->setOrientation(Qt::Horizontal);
    actionToolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    actionToolbar->setIconSize(QSize(16, 16));
    actionToolbar->setMinimumWidth(24);
    actionToolbar->setFixedHeight(30);
    actionToolbar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    actionToolbar->setStyleSheet(
        "QToolBar { border: none; background: transparent; padding: 0px; spacing: 2px; }");

    auto addAction = [this, actionToolbar](const QString& iconPath, const QString& text,
                                           const QString& tip, auto fn)
    {
        QAction* action = actionToolbar->addAction(visualizationThemeIcon(iconPath), text);
        action->setProperty("themeIconPath", iconPath);
        action->setToolTip(tip);
        connect(action, &QAction::triggered, this, fn);
        return action;
    };

    addAction(":/resources/icon/view_fit.svg", tr("自适应视图"),
              tr("重置并自适应视图"), [this]() { resetCamera(true); });

    addAction(":/resources/icon/view_rotate_left.svg", tr("逆时针旋转"),
              tr("逆时针旋转"), [this]() { rotateCamera(-12.0, 0.0); });
    addAction(":/resources/icon/view_rotate_right.svg", tr("顺时针旋转"),
              tr("顺时针旋转"), [this]() { rotateCamera(12.0, 0.0); });
    addAction(":/resources/icon/view_zoom_in.svg", tr("放大"), tr("放大"),
              [this]() { zoomCamera(1.15); });
    addAction(":/resources/icon/view_zoom_out.svg", tr("缩小"), tr("缩小"),
              [this]() { zoomCamera(1.0 / 1.15); });

    QAction* projectionAction = actionToolbar->addAction(tr("投影方式"));
    projectionAction->setCheckable(true);
    projectionAction->setChecked(m_isParallelProjection);
    connect(projectionAction, &QAction::triggered, this,
            [this, projectionAction](bool checked)
            {
                m_isParallelProjection = checked;
                if (m_renderer && m_renderer->GetActiveCamera())
                {
                    m_renderer->GetActiveCamera()->SetParallelProjection(
                        m_isParallelProjection ? 1 : 0);
                }
                updateProjectionButtonIcon();
                render();
            });
    m_projectionButton =
        qobject_cast<QToolButton*>(actionToolbar->widgetForAction(projectionAction));
    updateProjectionButtonIcon();

    QAction* scaleAction = actionToolbar->addAction(tr("尺度标尺"));
    scaleAction->setCheckable(true);
    scaleAction->setChecked(m_isScaleVisible);
    connect(scaleAction, &QAction::triggered, this,
            [this](bool checked)
            {
                m_isScaleVisible = checked;
                updateScaleActorBounds();
                updateScaleButtonIcon();
                render();
            });
    m_scaleButton = qobject_cast<QToolButton*>(actionToolbar->widgetForAction(scaleAction));
    updateScaleButtonIcon();

    actionToolbar->addSeparator();

    addAction(":/resources/icon/view_front.png", tr("前视图"), tr("前视图"),
              [this]() { setCameraToAxis(0.0, 0.0, 1.0); });
    addAction(":/resources/icon/view_back.png", tr("后视图"), tr("后视图"),
              [this]() { setCameraToAxis(0.0, 0.0, -1.0); });
    addAction(":/resources/icon/view_left.png", tr("左视图"), tr("左视图"),
              [this]() { setCameraToAxis(-1.0, 0.0, 0.0); });
    addAction(":/resources/icon/view_right.png", tr("右视图"), tr("右视图"),
              [this]() { setCameraToAxis(1.0, 0.0, 0.0); });
    addAction(":/resources/icon/view_top.png", tr("顶视图"), tr("顶视图"),
              [this]() { setCameraToAxis(0.0, 1.0, 0.0); });
    addAction(":/resources/icon/view_bottom.png", tr("底视图"), tr("底视图"),
              [this]() { setCameraToAxis(0.0, -1.0, 0.0); });

    // QToolBar 按内容宽度排列；窗口变窄时由 Qt 自己显示扩展按钮。
    // 透明度按钮紧跟在其后，不再使用中间 stretch，因此二者之间不会留空白。
    root->addWidget(actionToolbar, 0, Qt::AlignVCenter);

    m_opacityButton = new QToolButton(m_toolbar);
    m_opacityButton->setObjectName("opacityButton");
    m_opacityButton->setAutoRaise(true);
    m_opacityButton->setToolTip(tr("整体透明度"));
    m_opacityButton->setProperty("themeIconPath", QStringLiteral(":/resources/icon/view_opacity.svg"));
    m_opacityButton->setIcon(visualizationThemeIcon(
        QStringLiteral(":/resources/icon/view_opacity.svg")));
    m_opacityButton->setIconSize(QSize(16, 16));
    m_opacityButton->setFixedSize(30, 30);
    m_opacityButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    root->addWidget(m_opacityButton, 0, Qt::AlignVCenter);
    root->addStretch(1);

    m_opacityMenu = new QMenu(m_toolbar);
    m_opacityMenu->setWindowFlags(m_opacityMenu->windowFlags() | Qt::FramelessWindowHint);
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
            [this](int value)
            {
                if (m_opacityPercentLabel)
                    m_opacityPercentLabel->setText(QString::number(value) + "%");
                if (m_opacityButton)
                    m_opacityButton->setToolTip(tr("整体透明度：%1%").arg(value));

                // 内部域面 + depth peeling 的一次 Render 可能远超过 40 ms。
                // 拖动期间只更新滑块和百分比，松开后再一次性提交透明度，避免
                // Render 阻塞事件循环后积压 valueChanged，造成越拖越卡。
                if (m_globalOpacitySlider && m_globalOpacitySlider->isSliderDown())
                    return;
                applyGlobalOpacity(static_cast<double>(value) / 100.0, true);
            });

    connect(m_globalOpacitySlider, &QSlider::sliderReleased, this,
            [this]()
            {
                if (!m_globalOpacitySlider)
                    return;
                applyGlobalOpacity(
                    static_cast<double>(m_globalOpacitySlider->value()) / 100.0, true);
            });

    auto* action = new QWidgetAction(m_opacityMenu);
    action->setDefaultWidget(popupWidget);
    m_opacityMenu->addAction(action);

    connect(m_opacityButton, &QToolButton::clicked, this,
            [this]()
            {
                if (!m_opacityMenu || !m_opacityButton)
                    return;
                const QPoint p =
                    m_opacityButton->mapToGlobal(QPoint(0, m_opacityButton->height() + 4));
                m_opacityMenu->popup(p);
            });

    m_toolbar->setFixedHeight(36);
    m_toolbar->show();
}

void VisualizationWidget::render()
{
    if (m_renderWindow)
        m_renderWindow->Render();
}

void VisualizationWidget::resetCamera(bool renderNow)
{
    if (m_renderer)
        m_renderer->ResetCamera();
    if (renderNow)
        render();
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

bool VisualizationWidget::addProp(const QString& id, vtkSmartPointer<vtkProp> prop,
                                  const QString& group, const QString& name, bool resetCam,
                                  bool renderNow)
{
    if (id.isEmpty() || !prop)
        return false;

    showVisualizationView();
    if (m_registry.contains(id))
        return false;

    Item it;
    it.id = id;
    it.name = name;
    it.group = group;
    it.prop = prop;
    it.visible = true;

    if (auto* actor = vtkActor::SafeDownCast(prop))
    {
        it.opacity = qBound(0.0, actor->GetProperty()->GetOpacity(), 1.0);
        actor->GetProperty()->SetOpacity(it.opacity * m_globalOpacity);
    }

    m_renderer->AddViewProp(prop.GetPointer());
    m_registry.insert(id, it);

    // 暗色主题下网格原本的纯黑边线会与画布背景混在一起。
    if (auto* actor = vtkActor::SafeDownCast(prop))
    {
        const bool dark = StyleManager::currentThemeName() == QStringLiteral("dark");
        if (id == QStringLiteral("mesh.imported.surface"))
            actor->GetProperty()->SetEdgeColor(dark ? 0.63 : 0.0,
                                                dark ? 0.68 : 0.0,
                                                dark ? 0.72 : 0.0);
        else if (id == QStringLiteral("mesh.imported.line") && dark)
            actor->GetProperty()->SetColor(0.72, 0.77, 0.81);
    }

    updateScaleActorBounds();
    updateTransparencyRendering();

    if (resetCam)
        resetCamera(false);
    if (renderNow)
        render();
    return true;
}

bool VisualizationWidget::removeProp(const QString& id, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end())
        return false;

    // Registry 保存的是 Actor 自身的基础透明度，全局透明度只应在挂载期间相乘。
    // 卸载前必须恢复基础值，否则同一 Actor 再次挂载时会把已经相乘后的值当作
    // 新基准，页面每切换一次透明度就会继续衰减（例如 50% -> 25% -> 12.5%）。
    if (auto* actor = vtkActor::SafeDownCast(it.value().prop))
    {
        if (actor->GetProperty())
            actor->GetProperty()->SetOpacity(it.value().opacity);
    }

    if (it.value().prop)
        m_renderer->RemoveViewProp(it.value().prop);

    m_registry.erase(it);

    updateScaleActorBounds();
    updateTransparencyRendering();

    if (renderNow)
        render();
    return true;
}

void VisualizationWidget::clearAll(bool renderNow)
{
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        if (auto* actor = vtkActor::SafeDownCast(it.value().prop))
        {
            if (actor->GetProperty())
                actor->GetProperty()->SetOpacity(it.value().opacity);
        }
        if (it.value().prop)
            m_renderer->RemoveViewProp(it.value().prop);
    }
    m_registry.clear();

    if (m_cubeAxesActor)
        m_cubeAxesActor->SetVisibility(0);

    updateTransparencyRendering();
    if (renderNow)
        render();
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
    if (it == m_registry.end())
        return false;

    vtkProp* prop = it.value().prop.GetPointer();
    if (it.value().visible == on &&
        (!prop || (prop->GetVisibility() != 0) == on))
        return true;

    it.value().visible = on;
    if (prop)
        prop->SetVisibility(on ? 1 : 0);

    updateScaleActorBounds();
    updateTransparencyRendering();

    if (renderNow)
        render();
    return true;
}

void VisualizationWidget::setGroupVisible(const QString& group, bool on, bool renderNow)
{
    bool changed = false;
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        if (it.value().group == group && it.value().prop)
        {
            if (it.value().visible == on &&
                (it.value().prop->GetVisibility() != 0) == on)
                continue;
            it.value().visible = on;
            it.value().prop->SetVisibility(on ? 1 : 0);
            changed = true;
        }
    }

    if (!changed)
        return;

    updateScaleActorBounds();
    updateTransparencyRendering();
    if (renderNow)
        render();
}

bool VisualizationWidget::setOpacity(const QString& id, double alpha, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end())
        return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor || !actor->GetProperty())
        return false;

    const double baseOpacity = qBound(0.0, alpha, 1.0);
    const double effectiveOpacity = baseOpacity * m_globalOpacity;
    if (std::abs(it.value().opacity - baseOpacity) < 1.0e-6 &&
        std::abs(actor->GetProperty()->GetOpacity() - effectiveOpacity) < 1.0e-6)
        return true;

    it.value().opacity = baseOpacity;
    actor->GetProperty()->SetOpacity(effectiveOpacity);
    updateTransparencyRendering();

    if (id.startsWith(QStringLiteral("post."), Qt::CaseInsensitive))
    {
        QVariantMap event;
        event.insert(QStringLiteral("opacity"), actor->GetProperty()->GetOpacity());
        EventManager::instance().sendEvent(QStringLiteral("visualization.opacity.changed"), event);
    }

    if (renderNow)
        render();
    return true;
}

void VisualizationWidget::applyGlobalOpacity(double alpha, bool renderNow)
{
    const double boundedOpacity = qBound(0.05, alpha, 1.0);
    if (std::abs(m_globalOpacity - boundedOpacity) < 1.0e-6)
        return;

    m_globalOpacity = boundedOpacity;
    for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
    {
        auto* actor = vtkActor::SafeDownCast(it.value().prop);
        if (!actor)
            continue;
        actor->GetProperty()->SetOpacity(it.value().opacity * m_globalOpacity);
    }

    updateTransparencyRendering();

    QVariantMap event;
    event.insert(QStringLiteral("opacity"), m_globalOpacity);
    EventManager::instance().sendEvent(QStringLiteral("visualization.opacity.changed"), event);

    if (renderNow)
        render();
}

void VisualizationWidget::updateTransparencyRendering()
{
    if (!m_renderer || !m_renderWindow)
        return;

    bool hasTranslucentActor = false;
    for (auto it = m_registry.constBegin(); it != m_registry.constEnd(); ++it)
    {
        if (!it.value().visible)
            continue;
        auto* actor = vtkActor::SafeDownCast(it.value().prop);
        if (!actor || !actor->GetProperty())
            continue;
        if (actor->GetProperty()->GetOpacity() < 0.999)
        {
            hasTranslucentActor = true;
            break;
        }
    }

    // 只有“不透明 <-> 半透明”状态真正变化时才改 Renderer 配置。
    // 透明度从 60% 拖到 55% 时没有必要重复 SetUseDepthPeeling/SetMultiSamples。
    if (hasTranslucentActor == m_transparencyRenderingEnabled)
        return;
    m_transparencyRenderingEnabled = hasTranslucentActor;

    if (hasTranslucentActor)
    {
        // 大网格透明显示使用 depth peeling。限制为 8 层，并允许 25% 的
        // 遮挡收敛，可显著降低旋转和最终提交透明度时的 GPU 多遍绘制开销；
        // 域界面几何仍完整保留，不通过删面换取速度。
        m_renderWindow->SetAlphaBitPlanes(1);
        m_renderWindow->SetMultiSamples(0);
        m_renderer->SetUseDepthPeeling(true);
        m_renderer->SetMaximumNumberOfPeels(8);
        m_renderer->SetOcclusionRatio(0.25);
    }
    else
    {
        m_renderer->SetUseDepthPeeling(false);
        m_renderWindow->SetMultiSamples(m_defaultMultiSamples);
    }
}

bool VisualizationWidget::setTransform(const QString& id, vtkTransform* tr, bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end())
        return false;

    auto* p3d = vtkProp3D::SafeDownCast(it.value().prop);
    if (!p3d)
        return false;

    p3d->SetUserTransform(tr);
    if (renderNow)
        render();
    return true;
}

// ---------------- LUT & ScalarBar ----------------

void VisualizationWidget::setLookupTable(vtkLookupTable* lut, bool applyToAllActors, bool renderNow)
{
    if (!lut)
        return;

    m_lut = lut;
    m_scalarBar->SetLookupTable(m_lut);

    if (applyToAllActors)
    {
        for (auto it = m_registry.begin(); it != m_registry.end(); ++it)
        {
            vtkMapper* mp = actorMapper(it.value().prop);
            if (mp)
                mp->SetLookupTable(m_lut);
        }
    }

    if (renderNow)
        render();
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
    if (renderNow)
        render();
}

bool VisualizationWidget::isScalarBarVisible() const
{
    return m_scalarBar && m_scalarBar->GetVisibility() != 0;
}

void VisualizationWidget::setScalarBarTitle(const QString& title, bool renderNow)
{
    if (m_scalarBar)
    {
        m_scalarBar->SetTitle(title.toUtf8().constData());
        m_scalarBar->UnconstrainedFontSizeOn();
        // 字号和图例尺寸可能由后处理页面自定义，这里只更新标题，不能重置用户设置。
        m_scalarBar->Modified();
    }

    if (renderNow)
        render();
}
void VisualizationWidget::setScalarBarNumberOfLabels(int n, bool renderNow)
{
    if (m_scalarBar)
        m_scalarBar->SetNumberOfLabels(n);
    if (renderNow)
        render();
}

vtkMapper* VisualizationWidget::actorMapper(vtkProp* prop)
{
    auto* actor = vtkActor::SafeDownCast(prop);
    return actor ? actor->GetMapper() : nullptr;
}

bool VisualizationWidget::applyLookupTableToActor(const QString& id, const QString& scalarArrayName,
                                                  double rangeMin, double rangeMax, bool useRange,
                                                  bool renderNow)
{
    auto it = m_registry.find(id);
    if (it == m_registry.end())
        return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor)
        return false;

    vtkMapper* mp = actor->GetMapper();
    if (!mp)
        return false;

    mp->SetLookupTable(m_lut);

    if (!scalarArrayName.isEmpty())
    {
        mp->SelectColorArray(scalarArrayName.toUtf8().constData());
        mp->SetScalarModeToUsePointFieldData(); // 如需 cell
                                                // data：SetScalarModeToUseCellFieldData()
        mp->ScalarVisibilityOn();
    }

    if (useRange)
    {
        mp->SetScalarRange(rangeMin, rangeMax);
    }

    mp->SetColorModeToMapScalars();

    if (renderNow)
        render();
    return true;
}

bool VisualizationWidget::updateActorPolyData(const QString& id, vtkPolyData* poly, bool renderNow)
{
    if (!poly)
        return false;

    auto it = m_registry.find(id);
    if (it == m_registry.end())
        return false;

    auto* actor = vtkActor::SafeDownCast(it.value().prop);
    if (!actor)
        return false;

    auto* pdMapper = vtkPolyDataMapper::SafeDownCast(actor->GetMapper());
    if (!pdMapper)
        return false;

    pdMapper->SetInputData(poly);
    pdMapper->Update();

    if (renderNow)
        render();
    return true;
}

bool VisualizationWidget::showCurveFromTraceFile(const QString& traceFilePath, const QString& xField,
                                              const QStringList& yFields, QString* errMsg)
{
    if (!m_curvePlotWidget || traceFilePath.isEmpty())
        return false;

    const QSet<QString> yFieldSet = QSet<QString>(yFields.cbegin(), yFields.cend());
    const bool ok = m_curvePlotWidget->loadDataFromTraceFile(traceFilePath, xField, yFieldSet, errMsg);
    if (ok && m_stack)
    {
        m_stack->setCurrentWidget(m_curvePlotWidget);
        setViewToolbarVisible(false);
    }
    return ok;
}

void VisualizationWidget::showVisualizationView()
{
    if (m_stack && m_vtkWidget)
        m_stack->setCurrentWidget(m_vtkWidget);
    setViewToolbarVisible(true);
}

void VisualizationWidget::setViewToolbarVisible(bool visible)
{
    if (!m_toolbar)
        return;

    m_toolbar->setVisible(visible);
}

// ---------------- Orientation marker ----------------

void VisualizationWidget::showOrientationMarker(bool on)
{
    if (!m_omw)
        return;
    m_omw->SetEnabled(on ? 1 : 0);
    render();
}

void VisualizationWidget::setCameraToAxis(double x, double y, double z)
{
    if (!m_renderer || !m_renderer->GetActiveCamera())
        return;

    m_renderer->ResetCamera();
    vtkCamera* camera = m_renderer->GetActiveCamera();

    double fp[3] = {0.0, 0.0, 0.0};
    double pos[3] = {0.0, 0.0, 1.0};
    camera->GetFocalPoint(fp);
    camera->GetPosition(pos);

    const double distance =
        std::sqrt((pos[0] - fp[0]) * (pos[0] - fp[0]) + (pos[1] - fp[1]) * (pos[1] - fp[1]) +
                  (pos[2] - fp[2]) * (pos[2] - fp[2]));

    camera->SetPosition(fp[0] + x * distance, fp[1] + y * distance, fp[2] + z * distance);
    camera->SetViewUp(0.0, 0.0, 1.0);
    if (z != 0.0)
    {
        camera->SetViewUp(0.0, 1.0, 0.0);
    }

    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::rotateCamera(double azimuthDeg, double elevationDeg)
{
    if (!m_renderer || !m_renderer->GetActiveCamera())
        return;
    vtkCamera* camera = m_renderer->GetActiveCamera();
    camera->Azimuth(azimuthDeg);
    camera->Elevation(elevationDeg);
    camera->OrthogonalizeViewUp();
    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::zoomCamera(double factor)
{
    if (!m_renderer || !m_renderer->GetActiveCamera())
        return;
    vtkCamera* camera = m_renderer->GetActiveCamera();
    if (camera->GetParallelProjection())
        camera->SetParallelScale(camera->GetParallelScale() / factor);
    else
        camera->Dolly(factor);
    m_renderer->ResetCameraClippingRange();
    render();
}

void VisualizationWidget::updateProjectionButtonIcon()
{
    if (!m_projectionButton)
        return;

    const QString iconPath = m_isParallelProjection
                                 ? QStringLiteral(":/resources/icon/parallel.png")
                                 : QStringLiteral(":/resources/icon/perspective.png");
    const QIcon icon = visualizationThemeIcon(iconPath);
    const QString tip =
        m_isParallelProjection
            ? tr("当前为正交投影，点击切换为透视投影")
            : tr("当前为透视投影，点击切换为正交投影");

    if (QAction* action = m_projectionButton->defaultAction())
    {
        action->setIcon(icon);
        action->setToolTip(tip);
        action->setChecked(m_isParallelProjection);
    }
    else
    {
        m_projectionButton->setIcon(icon);
        m_projectionButton->setToolTip(tip);
        m_projectionButton->setChecked(m_isParallelProjection);
    }
    m_projectionButton->setIconSize(QSize(16, 16));
}

void VisualizationWidget::updateScaleButtonIcon()
{
    if (!m_scaleButton)
        return;

    const QString iconPath = m_isScaleVisible
                                 ? QStringLiteral(":/resources/icon/view_scale_on.svg")
                                 : QStringLiteral(":/resources/icon/view_scale_off.svg");
    const QIcon icon = visualizationThemeIcon(iconPath);
    const QString tip = m_isScaleVisible ? tr("隐藏尺度标尺") : tr("显示尺度标尺");

    if (QAction* action = m_scaleButton->defaultAction())
    {
        action->setIcon(icon);
        action->setToolTip(tip);
        action->setChecked(m_isScaleVisible);
    }
    else
    {
        m_scaleButton->setIcon(icon);
        m_scaleButton->setToolTip(tip);
        m_scaleButton->setChecked(m_isScaleVisible);
    }
    m_scaleButton->setIconSize(QSize(16, 16));
}

void VisualizationWidget::applyUiTheme(const QString& themeName)
{
    const QString theme = StyleManager::normalizeThemeName(themeName);
    const bool dark = theme == QStringLiteral("dark");

    double background[3] = {244.0 / 255.0, 246.0 / 255.0, 248.0 / 255.0};
    double background2[3] = {1.0, 1.0, 1.0};
    double textColor[3] = {0.0, 0.0, 0.0};

    if (dark)
    {
        background[0] = 30.0 / 255.0;
        background[1] = 36.0 / 255.0;
        background[2] = 42.0 / 255.0;
        background2[0] = 43.0 / 255.0;
        background2[1] = 51.0 / 255.0;
        background2[2] = 59.0 / 255.0;
        textColor[0] = 220.0 / 255.0;
        textColor[1] = 229.0 / 255.0;
        textColor[2] = 236.0 / 255.0;
    }
    else if (theme == QStringLiteral("blue"))
    {
        background[0] = 223.0 / 255.0;
        background[1] = 241.0 / 255.0;
        background[2] = 251.0 / 255.0;
        background2[0] = 246.0 / 255.0;
        background2[1] = 251.0 / 255.0;
        background2[2] = 255.0 / 255.0;
    }

    if (m_renderer)
    {
        m_renderer->SetBackground(background);
        m_renderer->SetBackground2(background2);
    }

    if (m_scalarBar)
    {
        if (auto* title = m_scalarBar->GetTitleTextProperty())
            title->SetColor(textColor);
        if (auto* label = m_scalarBar->GetLabelTextProperty())
            label->SetColor(textColor);
        m_scalarBar->Modified();
    }

    if (m_cubeAxesActor)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            if (auto* title = m_cubeAxesActor->GetTitleTextProperty(axis))
                title->SetColor(textColor);
            if (auto* label = m_cubeAxesActor->GetLabelTextProperty(axis))
                label->SetColor(textColor);
        }
        m_cubeAxesActor->GetXAxesLinesProperty()->SetColor(textColor);
        m_cubeAxesActor->GetYAxesLinesProperty()->SetColor(textColor);
        m_cubeAxesActor->GetZAxesLinesProperty()->SetColor(textColor);
        m_cubeAxesActor->Modified();
    }

    if (m_axes)
    {
        vtkCaptionActor2D* captions[] = {
            m_axes->GetXAxisCaptionActor2D(),
            m_axes->GetYAxisCaptionActor2D(),
            m_axes->GetZAxisCaptionActor2D()};
        for (vtkCaptionActor2D* caption : captions)
        {
            if (caption && caption->GetCaptionTextProperty())
                caption->GetCaptionTextProperty()->SetColor(textColor);
        }
        m_axes->Modified();
    }

    if (auto* meshSurface = vtkActor::SafeDownCast(getProp(QStringLiteral("mesh.imported.surface"))))
    {
        meshSurface->GetProperty()->SetEdgeColor(dark ? 0.63 : 0.0,
                                                  dark ? 0.68 : 0.0,
                                                  dark ? 0.72 : 0.0);
    }
    if (auto* meshLine = vtkActor::SafeDownCast(getProp(QStringLiteral("mesh.imported.line"))))
    {
        meshLine->GetProperty()->SetColor(dark ? 0.72 : 0.78,
                                           dark ? 0.77 : 0.78,
                                           dark ? 0.81 : 0.78);
    }

    updateToolbarIcons();
    updateProjectionButtonIcon();
    updateScaleButtonIcon();
    render();
}

void VisualizationWidget::updateToolbarIcons()
{
    if (m_actionToolbar)
    {
        for (QAction* action : m_actionToolbar->actions())
        {
            if (!action)
                continue;
            const QString path = action->property("themeIconPath").toString();
            if (!path.isEmpty())
                action->setIcon(visualizationThemeIcon(path));
        }
    }

    if (m_opacityButton)
    {
        const QString path = m_opacityButton->property("themeIconPath").toString();
        if (!path.isEmpty())
            m_opacityButton->setIcon(visualizationThemeIcon(path));
    }
}

void VisualizationWidget::updateScaleActorBounds()
{
    if (!m_cubeAxesActor || !m_renderer)
        return;

    // 标尺默认关闭时不需要遍历全部 Actor 计算 Bounds。页面切换会批量改变
    // Actor 显隐，这个提前返回可避免对大网格反复执行整场景 Bounds 扫描。
    m_cubeAxesActor->SetVisibility(0);
    if (!m_isScaleVisible)
        return;

    // 计算边界时先隐藏标尺本身，避免将旧标尺边界再次计入结果。

    double bounds[6] = {0.0, -1.0, 0.0, -1.0, 0.0, -1.0};
    m_renderer->ComputeVisiblePropBounds(bounds);
    const bool hasBounds =
        bounds[0] <= bounds[1] && bounds[2] <= bounds[3] && bounds[4] <= bounds[5];

    if (hasBounds)
        m_cubeAxesActor->SetBounds(bounds);
    m_cubeAxesActor->SetVisibility(hasBounds ? 1 : 0);

}
