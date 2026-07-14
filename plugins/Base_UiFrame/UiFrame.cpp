#include "UiFrame.h"

#include <EventManager.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>

#include <DockAreaWidget.h>
#include <DockAreaTitleBar.h>
#include <DockAreaTabBar.h>
#include <DockWidgetTab.h>
#include <QStyle>
#include <QPointer>

#include <QAbstractButton>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QSettings>
#include <QString>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

// SARibbon
#include "SARibbonApplicationButton.h"
#include "SARibbonBar.h"
#include "SARibbonButtonGroupWidget.h"
#include "SARibbonCategory.h"
#include "SARibbonCheckBox.h"
#include "SARibbonComboBox.h"
#include "SARibbonMenu.h"
#include "SARibbonPannel.h"
#include "SARibbonElementFactory.h"

namespace
{
inline void bindEvent(UiFrame* ctx, QAction* act, const char* eventKey)
{
    QObject::connect(act, &QAction::triggered, ctx,
                     [eventKey]()
                     {
                         EventManager::instance().sendEvent(eventKey);
                     });
}

inline void addActionToPanel(SARibbonPannel* panel, QAction* act,
                             int actionType /*1 small 2 medium 3 large*/)
{
    if (!panel || !act)
        return;

    switch (actionType)
    {
    case 1:
        panel->addSmallAction(act);
        break;
    case 2:
        panel->addMediumAction(act);
        break;
    case 3:
        panel->addLargeAction(act);
        break;
    default:
        panel->addSmallAction(act);
        break;
    }
}

inline QAction* makeAction(UiFrame* ctx, const QString& icon, const QString& text)
{
    return new QAction(QIcon(icon), text, ctx);
}
} // namespace

UiFrame::UiFrame(QWidget* parent, SARibbonMainWindowStyles style)
    : SARibbonMainWindow(parent, style)
{
    initSARibbon();
    initDockManager();

    ServiceRegistry::instance().registerService<UiFrameService>(this);

    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);

    EventManager::instance().registerHandler(PublicConstants::kEventLoadComplete, this,
                                             [this](const QVariantMap&)
                                             {
                                                 qInfo() << "restoreUiState";
                                                 this->restoreUiState();
                                                 qInfo() << "end restoreUiState";
                                             });
    EventManager::instance().registerHandler(PublicConstants::kEventQuitApp, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->storeUiState();
                                             });

    // 2. 如果你使用的是 ads::CDockManager，它会接管中央区域
    // 你需要针对 Dock 容器设置，否则背景会被覆盖
    //    if (m_dockManager) { // 假设你的 DockManager 变量名是这个
    //        m_dockManager->setStyleSheet("ads--CDockContainerWidget {
    //        background-color: white; }");
    //    }
    setWindowIcon(QIcon(":/resources/icon.svg"));
}

UiFrame::~UiFrame()
{
    // 你原注释“析构时存储布局”，这里顺便补上
    storeUiState();
}

void UiFrame::closeEvent(QCloseEvent* event)
{
    storeUiState();
    SARibbonMainWindow::closeEvent(event);
}

void UiFrame::initDockManager()
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);

    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, false);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);
    ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);

    m_dockManager = new ads::CDockManager(this);

    connect(m_dockManager, &ads::CDockManager::dockAreaCreated, this,
            [this](ads::CDockAreaWidget* area)
            {
                if (!area)
                    return;
                QPointer<ads::CDockAreaWidget> safeArea(area);
                auto titleBar = area->titleBar();
                auto tabBar = titleBar ? titleBar->tabBar() : nullptr;
                if (tabBar)
                {
                    connect(tabBar, &ads::CDockAreaTabBar::tabInserted, this,
                            [this, safeArea]() { if (safeArea) this->updateAreaTabStyle(safeArea); });
                    connect(tabBar, &ads::CDockAreaTabBar::tabClosed, this,
                            [this, safeArea]() { if (safeArea) this->updateAreaTabStyle(safeArea); });
                    connect(tabBar, &ads::CDockAreaTabBar::tabOpened, this,
                            [this, safeArea]() { if (safeArea) this->updateAreaTabStyle(safeArea); });
                    connect(tabBar, &ads::CDockAreaTabBar::removingTab, this,
                            [this, safeArea]()
                            {
                                QMetaObject::invokeMethod(this,
                                                          [this, safeArea]()
                                                          {
                                                              if (safeArea)
                                                                  this->updateAreaTabStyle(safeArea);
                                                          },
                                                          Qt::QueuedConnection);
                            });
                }
                if (safeArea)
                    this->updateAreaTabStyle(safeArea);
            });

    m_dockManager->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_dockManager->setContentsMargins(0, 0, 0, 0);

    if (auto layout = m_dockManager->layout())
    {
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
    }

    QWidget* central = new QWidget(this);
    QVBoxLayout* centralLayout = new QVBoxLayout(central);
    centralLayout->setContentsMargins(0, 0, 0, 0);
    centralLayout->setSpacing(0);
    centralLayout->addWidget(m_dockManager);
    setCentralWidget(central);
    setDockWidgetStyle();

    captureDefaultDockState();
}

QAction* UiFrame::addWidget(QWidget* widget, QString name)
{
    ads::CDockWidget* dockWidget = new ads::CDockWidget(name);
    dockWidget->setWidget(widget);

    dockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, true);

    const ads::DockWidgetArea area = preferDockArea(name);
    ads::CDockAreaWidget* targetArea = m_centerDockArea;

    if (!targetArea)
    {
        targetArea = m_dockManager->addDockWidget(area, dockWidget);
        if (area == ads::CenterDockWidgetArea)
            m_centerDockArea = targetArea;
    }
    else if (area == ads::CenterDockWidgetArea)
    {
        m_dockManager->addDockWidgetTabToArea(dockWidget, targetArea);
    }
    else
    {
        m_dockManager->addDockWidget(area, dockWidget, targetArea);
    }

    if (!m_centerDockArea && targetArea)
        m_centerDockArea = targetArea;

    captureDefaultDockState();
    updateAllContainersTabStyle();
    return dockWidget->toggleViewAction();
}

QAction* UiFrame::addWidgetForTabwidget(QWidget* widget, QString name)
{
    ads::CDockWidget* dockWidget = new ads::CDockWidget(name);
    dockWidget->setWidget(widget);

    dockWidget->setFeature(ads::CDockWidget::DockWidgetMovable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetClosable, true);
    dockWidget->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    dockWidget->toggleView(false);

    if (!m_centerDockArea)
        m_centerDockArea = m_dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);
    else
        m_dockManager->addDockWidgetTabToArea(dockWidget, m_centerDockArea);

    captureDefaultDockState();
    updateAllContainersTabStyle();
    return dockWidget->toggleViewAction();
}

void UiFrame::restoreUiState()
{
    QSettings settings("config/file.ini", QSettings::IniFormat);
    const QByteArray savedState = settings.value("stateData").toByteArray();

    bool success = false;
    if (!savedState.isEmpty())
        success = m_dockManager->restoreState(savedState);

    if (settings.contains("windowSize"))
        resize(settings.value("windowSize").toSize());

    // 你原代码逻辑：restore 成功就把它当“默认布局”
    if (success)
        m_defaultDockState = savedState;

    setDockWidgetStyle();
    updateAllContainersTabStyle();
    show();
}

void UiFrame::storeUiState()
{
    if (!m_dockManager)
        return;

    QDir dir;
    if (!dir.exists("config"))
        dir.mkpath("config");

    const QByteArray state = m_dockManager->saveState();

    QSettings settings("config/file.ini", QSettings::IniFormat);
    settings.setValue("stateData", QVariant(state));
    settings.setValue("windowSize", size());

    // 如果你在用 perspectives，这行保留
    m_dockManager->savePerspectives(settings);
}

void UiFrame::setDockWidgetStyle()
{
    const QString qss = QString::fromUtf8(R"(
        ads--CDockManager,
        ads--CDockContainerWidget {
            background: #edf0f2;
        }

        ads--CDockAreaWidget {
            background: #ffffff;
            border: 1px solid #d7dcdf;
        }

        ads--CDockAreaWidget[focused="true"] {
            border-color: #c8d0d5;
        }

        QSplitter::handle {
            width: 3px;
            height: 3px;
            background: #d9dee1;
        }

        QSplitter::handle:hover {
            background: #aebac1;
        }

        QScrollArea#dockWidgetScrollArea {
            padding: 0px;
            border: none;
        }

        ads--CDockAreaWidget #tabsMenuButton::menu-indicator {
            image: none;
        }

        #dockAreaAutoHideButton {
            qproperty-icon: url(:/resources/images/vs-pin-button.svg),
                url(:/resources/images/vs-pin-button-disabled.svg) disabled;
            qproperty-iconSize: 13px;
        }

        ads--CAutoHideDockContainer #dockAreaAutoHideButton {
            qproperty-icon: url(:/resources/images/vs-pin-button-pinned-focused.svg);
            qproperty-iconSize: 13px;
        }

        ads--CAutoHideSideBar {
            background: #f2f4f5;
            border: none;
            qproperty-spacing: 8;
        }

        ads--CAutoHideTab {
            color: #657078;
            background: transparent;
            border: none;
            border-radius: 2px;
            padding: 4px;
            qproperty-iconSize: 14px 14px;
        }

        ads--CAutoHideTab:hover,
        ads--CAutoHideTab[activeTab="true"] {
            color: #2c5368;
            background: #e4ebef;
        }

        ads--CAutoHideDockContainer {
            background: #ffffff;
            border: 1px solid #c7d0d5;
        }
    )");

    m_dockManager->setStyleSheet(qss);
}


void UiFrame::updateAreaTabStyle(ads::CDockAreaWidget* area)
{
    if (!area)
        return;

    auto* titleBar = area->titleBar();
    if (!titleBar)
        return;

    const bool isSingle = area->openDockWidgetsCount() <= 1;

    titleBar->setStyleSheet(QString::fromUtf8(R"(
        ads--CDockAreaTitleBar {
            background: #f6f7f8;
            border: none;
            border-bottom: 1px solid #dce1e4;
            min-height: 31px;
            max-height: 31px;
        }

        ads--CDockAreaTitleBar ads--CTitleBarButton {
            background: transparent;
            border: none;
            border-radius: 2px;
            min-width: 21px;
            min-height: 21px;
            max-width: 21px;
            max-height: 21px;
            margin: 5px 2px;
            padding: 3px;
        }

        ads--CDockAreaTitleBar ads--CTitleBarButton:hover {
            background: #e8ecef;
        }

        ads--CDockAreaTitleBar ads--CTitleBarButton:pressed {
            background: #dce2e6;
        }

        ads--CDockAreaTitleBar #dockAreaCloseButton {
            qproperty-icon: url(:/resources/images/close-button.svg);
            qproperty-iconSize: 12px;
        }

        ads--CDockAreaTitleBar #dockAreaCloseButton:hover {
            background: #c84a4a;
            qproperty-icon: url(:/resources/images/close-button-focused.svg);
        }

        ads--CDockAreaTitleBar #dockAreaAutoHideButton {
            qproperty-icon: url(:/resources/images/vs-pin-button.svg);
            qproperty-iconSize: 12px;
        }

        ads--CAutoHideDockContainer ads--CDockAreaTitleBar #dockAreaAutoHideButton {
            qproperty-icon: url(:/resources/images/vs-pin-button-pinned-focused.svg);
            qproperty-iconSize: 12px;
        }

        ads--CDockAreaTitleBar #tabsMenuButton {
            qproperty-icon: url(:/resources/images/tabs-menu-button.svg);
            qproperty-iconSize: 12px;
        }

        ads--CDockAreaTitleBar #tabsMenuButton::menu-indicator {
            image: none;
        }

        ads--CDockAreaTitleBar #dockAreaUndockButton {
            qproperty-icon: url(:/resources/images/detach-button.svg);
            qproperty-iconSize: 12px;
        }
    )"));

    auto* tabBar = titleBar->tabBar();
    if (!tabBar)
        return;

    tabBar->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    const QString singleTabStyle = QString::fromUtf8(R"(
        ads--CDockWidgetTab,
        ads--CDockWidgetTab:hover,
        ads--CDockWidgetTab[activeTab="true"],
        ads--CDockWidgetTab[focused="true"] {
            color: #293139;
            background: transparent;
            border: none;
            min-height: 29px;
            max-height: 29px;
            padding: 0px 10px;
        }

        ads--CDockWidgetTab QLabel#dockWidgetTabLabel,
        ads--CDockWidgetTab:hover QLabel#dockWidgetTabLabel,
        ads--CDockWidgetTab[activeTab="true"] QLabel#dockWidgetTabLabel,
        ads--CDockWidgetTab[focused="true"] QLabel#dockWidgetTabLabel {
            color: #293139;
            background: transparent;
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "Segoe UI";
            font-size: 9.5pt;
            font-weight: 600;
        }
    )");

    const QString multiTabStyle = QString::fromUtf8(R"(
        ads--CDockWidgetTab {
            color: #5d676f;
            background: transparent;
            border: none;
            border-bottom: 2px solid transparent;
            border-radius: 0px;
            min-height: 29px;
            max-height: 29px;
            margin: 0px;
            padding: 0px 13px;
        }

        ads--CDockWidgetTab:hover {
            color: #354b57;
            background: #eef2f4;
        }

        ads--CDockWidgetTab[activeTab="true"],
        ads--CDockWidgetTab[focused="true"] {
            color: #2c5368;
            background: #ffffff;
            border-bottom: 2px solid #4f778d;
        }

        ads--CDockWidgetTab QLabel#dockWidgetTabLabel {
            color: #5d676f;
            background: transparent;
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "Segoe UI";
            font-size: 9pt;
            font-weight: 500;
        }

        ads--CDockWidgetTab:hover QLabel#dockWidgetTabLabel {
            color: #354b57;
        }

        ads--CDockWidgetTab[activeTab="true"] QLabel#dockWidgetTabLabel,
        ads--CDockWidgetTab[focused="true"] QLabel#dockWidgetTabLabel {
            color: #2c5368;
            font-weight: 600;
        }
    )");

    for (int i = 0; i < tabBar->count(); ++i)
    {
        auto* tab = tabBar->tab(i);
        if (tab)
            tab->setStyleSheet(isSingle ? singleTabStyle : multiTabStyle);
    }
}


void UiFrame::updateAllContainersTabStyle()
{
    if (!m_dockManager)
        return;
    for (auto area : m_dockManager->findChildren<ads::CDockAreaWidget*>())
    {
        updateAreaTabStyle(area);
    }
}

ads::DockWidgetArea UiFrame::preferDockArea(const QString& name) const
{
    const QString lower = name.toLower();

    if (lower.contains(QString("模型")) || lower.contains("model") ||
        lower.contains(QString("工程")) || lower.contains("tree"))
        return ads::LeftDockWidgetArea;

    if (lower.contains(QString("日志")) || lower.contains("log") ||
        lower.contains(QString("输出")) || lower.contains(QString("进度")) ||
        lower.contains("console") || lower.contains("message"))
        return ads::BottomDockWidgetArea;

    if (lower.contains(QString("图形")) || lower.contains(QString("视图")) ||
        lower.contains("render") || lower.contains("viewer"))
        return ads::CenterDockWidgetArea;

    if (lower.contains(QString("属性")) || lower.contains(QString("设置")) ||
        lower.contains("property") || lower.contains("panel"))
        return ads::RightDockWidgetArea;

    return ads::RightDockWidgetArea;
}

void UiFrame::captureDefaultDockState()
{
    if (!m_dockManager)
        return;
    m_defaultDockState = m_dockManager->saveState();
}

void UiFrame::initSARibbon()
{
    SARibbonBar::initHighDpi();
    setWindowTitle(QStringLiteral("POWERCHINA-GEOCAE"));

    m_ribbon = ribbonBar();
    m_ribbon->setObjectName(QStringLiteral("geoCaeRibbonBar"));
    m_ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactTwoRow);
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);

    // Keep the established workflow. The refinement is intentionally subtle:
    // flatter tabs, restrained hover states and one consistent accent.
    m_ribbon->setStyleSheet(QString::fromUtf8(R"(
        SARibbonBar#geoCaeRibbonBar {
            background: #ffffff;
            border: none;
            border-bottom: 1px solid #d7dde1;
        }

        SARibbonBar#geoCaeRibbonBar QTabBar {
            background: #ffffff;
            border: none;
        }

        SARibbonBar#geoCaeRibbonBar QTabBar::tab {
            color: #3d464e;
            background: transparent;
            border: none;
            border-bottom: 2px solid transparent;
            border-radius: 0px;
            min-height: 30px;
            padding: 0px 16px;
            margin: 0px;
            font-size: 10pt;
        }

        SARibbonBar#geoCaeRibbonBar QTabBar::tab:hover:!selected {
            color: #2e5367;
            background: #f3f5f6;
        }

        SARibbonBar#geoCaeRibbonBar QTabBar::tab:selected {
            color: #284d61;
            background: #ffffff;
            border-bottom: 2px solid #4f778d;
            font-weight: 600;
        }

        SARibbonBar#geoCaeRibbonBar QStackedWidget {
            background: #fbfcfc;
            border: none;
            border-top: 1px solid #e3e6e8;
        }

        SARibbonBar#geoCaeRibbonBar SARibbonPannel {
            background: #fbfcfc;
            border: none;
            border-right: 1px solid #e3e7e9;
        }

        SARibbonBar#geoCaeRibbonBar QToolButton {
            color: #2f373e;
            background: transparent;
            border: 1px solid transparent;
            border-radius: 2px;
            padding: 3px 6px;
        }

        SARibbonBar#geoCaeRibbonBar QToolButton:hover {
            color: #294f66;
            background: #f0f3f5;
            border-color: #dce2e5;
        }

        SARibbonBar#geoCaeRibbonBar QToolButton:pressed,
        SARibbonBar#geoCaeRibbonBar QToolButton:checked {
            color: #253f4d;
            background: #e2e9ed;
            border-color: #c7d3d9;
        }
    )"));

    buildRibbonAppButton();
    buildRibbonTabs();
}


void UiFrame::buildRibbonAppButton()
{
    QAbstractButton* btn = m_ribbon->applicationButton();
    btn->setObjectName(QStringLiteral("geoCaeFileButton"));
    btn->setText(QStringLiteral("文件"));
    btn->setMinimumWidth(64);
    btn->setStyleSheet(QString::fromUtf8(R"(
        QAbstractButton#geoCaeFileButton {
            color: #30383f;
            background: #ffffff;
            border: none;
            border-right: 1px solid #e0e4e7;
            border-bottom: 2px solid transparent;
            border-radius: 0px;
            padding: 0px 15px;
            font-size: 10pt;
            font-weight: 600;
        }

        QAbstractButton#geoCaeFileButton:hover {
            color: #294f66;
            background: #f3f5f6;
        }

        QAbstractButton#geoCaeFileButton:pressed {
            color: #284d61;
            background: #e8edef;
            border-bottom: 2px solid #4f778d;
        }
    )"));

    m_applicationMenu = new SARibbonMenu(this);
    auto* appBtn = qobject_cast<SARibbonApplicationButton*>(btn);
    appBtn->setMenu(m_applicationMenu);

    auto* actNew = makeAction(this, ":/resources/menu/minimal/app-new.svg", "新建工程");
    auto* actOpen = makeAction(this, ":/resources/menu/minimal/app-open.svg", "打开工程");
    auto* actSave = makeAction(this, ":/resources/menu/minimal/app-save.svg", "保存工程");
    auto* actExit = makeAction(this, ":/resources/menu/minimal/app-exit.svg", "退出");

    m_applicationMenu->addAction(actNew);
    m_applicationMenu->addAction(actOpen);
    m_applicationMenu->addAction(actSave);
    m_applicationMenu->addSeparator();
    m_applicationMenu->addAction(actExit);

    bindEvent(this, actNew, PublicConstants::kActionNewProject);
    bindEvent(this, actOpen, PublicConstants::kActionOpenProject);
    bindEvent(this, actSave, PublicConstants::kActionSaveProject);

    connect(actExit, &QAction::triggered, this,
            [this]()
            {
                EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
                close();
            });
}


void UiFrame::buildRibbonTabs()
{
    // =========================
    // 几何
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("几何");
        cate->setObjectName("geomObj");

               // ---------- 文件 ----------
        {
            auto* panel = cate->addPannel("文件");

            auto* actImport = makeAction(this, ":/resources/menu/minimal/import.svg", "&导入");
            auto* actExport = makeAction(this, ":/resources/menu/minimal/export.svg", "&导出");

            addActionToPanel(panel, actImport, 3);
            addActionToPanel(panel, actExport, 3);

            bindEvent(this, actImport, PublicConstants::kActionPreprocessImportGeometry);
            bindEvent(this, actExport, PublicConstants::kActionPreprocessExportGeometry);
        }

        // ---------- 创建 ----------
        {
            auto* panel = cate->addPannel("创建");

                   // =========================
                   // 1) 三维实体
                   // =========================
            auto* actSolidMain =
                makeAction(this, ":/resources/menu/minimal/cuboid.svg", "&三维实体");

            auto* solidMenu = new SARibbonMenu(this);

            auto* actBox =
                makeAction(this, ":/resources/menu/minimal/cuboid.svg", "&长方体");
            auto* actCylinder =
                makeAction(this, ":/resources/menu/minimal/cylinder.svg", "&圆柱体");
            auto* actSphere =
                makeAction(this, ":/resources/menu/minimal/sphere.svg", "&球体");
            auto* actCone =
                makeAction(this, ":/resources/menu/minimal/cone.svg", "&圆锥体");
            auto* actTorus =
                makeAction(this, ":/resources/menu/minimal/torus.svg", "&圆环");

            solidMenu->addAction(actBox);
            solidMenu->addAction(actCylinder);
            solidMenu->addAction(actSphere);
            solidMenu->addAction(actCone);
            solidMenu->addAction(actTorus);

            actSolidMain->setMenu(solidMenu);
            addActionToPanel(panel, actSolidMain, 3);

            bindEvent(this, actBox, PublicConstants::kActionPreprocessBox);
            bindEvent(this, actCylinder, PublicConstants::kActionPreprocessCylinder);
            bindEvent(this, actSphere, PublicConstants::kActionPreprocessSphere);
            bindEvent(this, actCone, PublicConstants::kActionPreprocessCone);
            bindEvent(this, actTorus, PublicConstants::kActionPreprocessTorus);

                   // =========================
                   // 2) 二维轮廓
                   // =========================
            auto* actProfileMain =
                makeAction(this, ":/resources/menu/minimal/profile.svg", "&二维轮廓");

            auto* profileMenu = new SARibbonMenu(this);

            auto* actProfileRect =
                makeAction(this, ":/resources/menu/minimal/rectangle.svg", "&矩形");
            auto* actProfileCircle =
                makeAction(this, ":/resources/menu/minimal/circle.svg", "&圆");
            auto* actProfileRing =
                makeAction(this, ":/resources/menu/minimal/ring.svg", "&环");
            auto* actProfilePolygon =
                makeAction(this, ":/resources/menu/minimal/polygon.svg", "&多边形");

            profileMenu->addAction(actProfileRect);
            profileMenu->addAction(actProfileCircle);
            profileMenu->addAction(actProfileRing);
            profileMenu->addAction(actProfilePolygon);

            actProfileMain->setMenu(profileMenu);
            addActionToPanel(panel, actProfileMain, 3);

            bindEvent(this, actProfileRect, PublicConstants::kActionGeomCreatePlanarRect);
            bindEvent(this, actProfileCircle, PublicConstants::kActionGeomCreatePlanarCircle);
            bindEvent(this, actProfileRing, PublicConstants::kActionGeomCreateEllipse);
            bindEvent(this, actProfilePolygon, PublicConstants::kActionGeomCreatePlanarPolygon);
        }

               // ---------- 工程模板 ----------
        {
            auto* panel = cate->addPannel("工程模板");

            auto* actGravityDam =
                makeAction(this, ":/resources/menu/minimal/analysis-dam.svg", "&重力坝");
            auto* actEarthRockfillDam =
                makeAction(this, ":/resources/menu/minimal/mat-soil-eb.svg", "&土石坝");
            auto* actSlopeTemplate =
                makeAction(this, ":/resources/menu/minimal/analysis-slope.svg", "&边坡");
            auto* actCavernTemplate =
                makeAction(this, ":/resources/menu/minimal/analysis-cave.svg", "&洞室");

            addActionToPanel(panel, actGravityDam, 3);
            addActionToPanel(panel, actEarthRockfillDam, 3);
            addActionToPanel(panel, actSlopeTemplate, 3);
            addActionToPanel(panel, actCavernTemplate, 3);

            bindEvent(this, actGravityDam, PublicConstants::kActionPreprocessGravityDam);
            bindEvent(this, actEarthRockfillDam, PublicConstants::kActionPreprocessEarthRockfillDam);
            bindEvent(this, actSlopeTemplate, PublicConstants::kActionPreprocessSlopeTemplate);
            bindEvent(this, actCavernTemplate, PublicConstants::kActionPreprocessCavernTemplate);
        }


               // ---------- 布尔 ----------
        {
            auto* panel = cate->addPannel("布尔");

            auto* actFuse = makeAction(this, ":/resources/menu/minimal/boolean-fuse.svg", "&并集");
            auto* actCut = makeAction(this, ":/resources/menu/minimal/boolean-cut.svg", "&差集");
            auto* actCommon =
                makeAction(this, ":/resources/menu/minimal/boolean-common.svg", "&交集");

            addActionToPanel(panel, actFuse, 3);
            addActionToPanel(panel, actCut, 3);
            addActionToPanel(panel, actCommon, 3);

            bindEvent(this, actFuse, PublicConstants::kActionGeomFuse);
            bindEvent(this, actCut, PublicConstants::kActionGeomCut);
            bindEvent(this, actCommon, PublicConstants::kActionGeomCommon);
        }
    }

           // =========================
           // 网格
           // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("网格");
        cate->setObjectName("gridObj");

               // ---------- 文件 ----------
        {
            auto* panel = cate->addPannel("文件");

            auto* actImport = makeAction(this, ":/resources/menu/minimal/import.svg", "&导入");
            auto* actExport = makeAction(this, ":/resources/menu/minimal/export.svg", "&导出");

            addActionToPanel(panel, actImport, 3);
            addActionToPanel(panel, actExport, 3);

            bindEvent(this, actImport, PublicConstants::kActionPreprocessImportGrid);
            bindEvent(this, actExport, PublicConstants::kActionPreprocessExportGrid);
        }

               // ---------- 剖分 ----------
        {
            auto* panel = cate->addPannel("剖分");

            auto* actGlobal =
                makeAction(this, ":/resources/menu/minimal/mesh-global.svg", "&整体剖分");

            addActionToPanel(panel, actGlobal, 3);

            bindEvent(this, actGlobal, PublicConstants::kActionPreprocessMeshGlobal);
        }

               // ---------- 尺寸控制 ----------
        {
            auto* panel = cate->addPannel("尺寸控制");

            auto* actVolCtrl =
                makeAction(this, ":/resources/menu/minimal/mesh-vol.svg", "&体控制");
            auto* actFaceCtrl =
                makeAction(this, ":/resources/menu/minimal/mesh-face.svg", "&面控制");
            auto* actEdgeCtrl =
                makeAction(this, ":/resources/menu/minimal/mesh-edge.svg", "&边控制");

            addActionToPanel(panel, actVolCtrl, 3);
            addActionToPanel(panel, actFaceCtrl, 3);
            addActionToPanel(panel, actEdgeCtrl, 3);

            bindEvent(this, actVolCtrl, PublicConstants::kActionPreprocessCtrlVolume);
            bindEvent(this, actFaceCtrl, PublicConstants::kActionPreprocessCtrlSurface);
            bindEvent(this, actEdgeCtrl, PublicConstants::kActionPreprocessCtrlEdge);
        }

               // ---------- 网格工具 ----------
        {
            auto* panel = cate->addPannel("网格工具");

            auto* actMeshEdit =
                makeAction(this, ":/resources/menu/minimal/mesh-edit.svg", "&显示控制");
            // auto* actMeshGrouping =
            //     makeAction(this, ":/resources/menu/minimal/mesh-group.svg", "&网格分组");
            auto* actQuality =
                makeAction(this, ":/resources/menu/minimal/mesh-quality.svg", "&质量检查");

            addActionToPanel(panel, actMeshEdit, 3);
            // addActionToPanel(panel, actMeshGrouping, 3);
            addActionToPanel(panel, actQuality, 3);

            bindEvent(this, actMeshEdit, PublicConstants::kActionPreprocessMeshEdit);
            // bindEvent(this, actMeshGrouping, PublicConstants::kActionPreprocessMeshGrouping);
            bindEvent(this, actQuality, PublicConstants::kActionPreprocessQualityStd);
        }
    }

           // =========================
           // 材料
           // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("材料");
        cate->setObjectName("materialObj");

        auto addMaterialAction = [this](QAction* action, const QString& materialKey)
        {
            connect(action, &QAction::triggered, this,
                    [materialKey]()
                    {
                        auto* materialService =
                            ServiceRegistry::instance().getService<MaterialManagerService>();
                        if (materialService)
                            materialService->addMaterialFromMenu(materialKey);
                    });
        };

               // ---------- 土体模型 ----------
        {
            auto* panel = cate->addPannel("土体模型");

            auto* actSoilEb =
                makeAction(this, ":/resources/menu/minimal/mat-soil-eb.svg", "E-B模型");
            auto* actSoilEmu =
                makeAction(this, ":/resources/menu/minimal/mat-soil-emu.svg", "E-μ模型");
            auto* actSoilEmuCrush =
                makeAction(this, ":/resources/menu/minimal/mat-soil-emu-crush.svg", "E-μ破碎");
            auto* actNanshui =
                makeAction(this, ":/resources/menu/minimal/mat-nanshui.svg", "非线性土体");
            auto* actKg =
                makeAction(this, ":/resources/menu/minimal/mat-kg.svg", "K-G模型");

            addActionToPanel(panel, actSoilEb, 2);
            addActionToPanel(panel, actSoilEmu, 2);
            addActionToPanel(panel, actSoilEmuCrush, 2);
            addActionToPanel(panel, actNanshui, 2);
            addActionToPanel(panel, actKg, 2);

            addMaterialAction(actSoilEb, QStringLiteral("soil_eb"));
            addMaterialAction(actSoilEmu, QStringLiteral("soil_emu"));
            addMaterialAction(actSoilEmuCrush, QStringLiteral("soil_emu_crush"));
            addMaterialAction(actNanshui, QStringLiteral("soil_nanshui"));
            addMaterialAction(actKg, QStringLiteral("soil_kg"));
        }

               // ---------- 工程模型 ----------
        {
            auto* panel = cate->addPannel("工程模型");

            auto* actHjp =
                makeAction(this, ":/resources/menu/minimal/mat-hjp.svg", "围岩开挖");
            auto* actDynamic =
                makeAction(this, ":/resources/menu/minimal/mat-dynamic.svg", "动力本构");
            auto* actBar =
                makeAction(this, ":/resources/menu/minimal/mat-bar.svg", "锚杆单元");
            auto* actDagangshan =
                makeAction(this, ":/resources/menu/minimal/mat-dagangshan.svg", "各向同性");

            addActionToPanel(panel, actHjp, 2);
            addActionToPanel(panel, actDynamic, 2);
            addActionToPanel(panel, actBar, 2);
            addActionToPanel(panel, actDagangshan, 2);

            addMaterialAction(actHjp, QStringLiteral("huangjinping_underground_plant"));
            addMaterialAction(actDynamic, QStringLiteral("dam_dynamic"));
            addMaterialAction(actBar, QStringLiteral("huangjinping_bar_element"));
            addMaterialAction(actDagangshan, QStringLiteral("dagangshan_isotropic"));
        }

               // ---------- 数据与变换 ----------
        {
            auto* panel = cate->addPannel("数据与变换");

            auto* actBuiltinMaterial =
                makeAction(this, ":/resources/menu/minimal/mat-builtin-material.svg", "材料示例");
            auto* actBuiltinSeepage =
                makeAction(this, ":/resources/menu/minimal/mat-builtin-seepage.svg", "渗流示例");
            auto* actMaterialTransition =
                makeAction(this, ":/resources/menu/minimal/mat-transition.svg", "材料变换");

            addActionToPanel(panel, actBuiltinMaterial, 3);
            addActionToPanel(panel, actBuiltinSeepage, 3);
            addActionToPanel(panel, actMaterialTransition, 3);

            connect(actBuiltinMaterial, &QAction::triggered, this,
                    []()
                    {
                        auto* materialService =
                            ServiceRegistry::instance().getService<MaterialManagerService>();
                        if (materialService)
                            materialService->showBuiltinMaterialListDialog();
                    });

            connect(actBuiltinSeepage, &QAction::triggered, this,
                    []()
                    {
                        auto* materialService =
                            ServiceRegistry::instance().getService<MaterialManagerService>();
                        if (materialService)
                            materialService->showBuiltinSeepageListDialog();
                    });

            connect(actMaterialTransition, &QAction::triggered, this,
                    []()
                    {
                        auto* materialService =
                            ServiceRegistry::instance().getService<MaterialManagerService>();
                        if (materialService)
                            materialService->showMaterialTransitionDialog();
                    });
        }
    }

           // =========================
           // 约束与荷载
           // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("约束与荷载");
        cate->setObjectName("boundObj");

               // ---------- 约束 ----------
        {
            auto* panel = cate->addPannel("约束");

            auto* actDisplacement =
                makeAction(this, ":/resources/menu/minimal/constraint.svg", "&位移约束");

            addActionToPanel(panel, actDisplacement, 3);

            bindEvent(this, actDisplacement,
                      PublicConstants::kActionPhysicalDisplacementConstraint);
        }

               // ---------- 地应力 ----------
        {
            auto* panel = cate->addPannel("地应力");

            auto* actGeoStressPoint =
                makeAction(this, ":/resources/menu/minimal/load-geostress-point.svg",
                           "&现场地应力");
            auto* actGeoStressFitFace =
                makeAction(this, ":/resources/menu/minimal/load-geostress-face.svg",
                           "&梯形边界");

            addActionToPanel(panel, actGeoStressPoint, 3);
            addActionToPanel(panel, actGeoStressFitFace, 3);

            bindEvent(this, actGeoStressPoint, PublicConstants::kActionPhysicalGeoStressPoint);
            bindEvent(this, actGeoStressFitFace, PublicConstants::kActionPhysicalGeoStressFitFace);
        }

               // ---------- 荷载 ----------
        {
            auto* panel = cate->addPannel("荷载");

            auto* actBodyForce =
                makeAction(this, ":/resources/menu/minimal/load-body-force.svg", "&体积力");
            auto* actConcentratedForce =
                makeAction(this, ":/resources/menu/minimal/load-concentrated.svg", "&集中力");
            auto* actLinearForce =
                makeAction(this, ":/resources/menu/minimal/load-linear.svg", "&线力");
            auto* actSurfaceForce =
                makeAction(this, ":/resources/menu/minimal/load-surface.svg", "&面力");
            auto* actTemperatureLoad =
                makeAction(this, ":/resources/menu/minimal/load-temperature.svg", "&变温");
            auto* actBeamLoad =
                makeAction(this, ":/resources/menu/minimal/load-beam.svg", "&梁单元");
            auto* actSeismicAndAddedMass =
                makeAction(this, ":/resources/menu/minimal/load-seismic.svg", "&地震表");

            addActionToPanel(panel, actBodyForce, 2);
            addActionToPanel(panel, actConcentratedForce, 2);
            addActionToPanel(panel, actLinearForce, 2);
            addActionToPanel(panel, actSurfaceForce, 2);
            addActionToPanel(panel, actTemperatureLoad, 2);
            addActionToPanel(panel, actBeamLoad, 2);
            addActionToPanel(panel, actSeismicAndAddedMass, 2);

            bindEvent(this, actBodyForce, PublicConstants::kActionPhysicalBodyForce);
            bindEvent(this, actConcentratedForce,
                      PublicConstants::kActionPhysicalConcentratedForce);
            bindEvent(this, actLinearForce, PublicConstants::kActionPhysicalLinearForce);
            bindEvent(this, actSurfaceForce, PublicConstants::kActionPhysicalSurfaceForce);
            bindEvent(this, actTemperatureLoad, PublicConstants::kActionPhysicalTemperatureLoad);
            bindEvent(this, actBeamLoad, PublicConstants::kActionPhysicalBeamLoad);
            bindEvent(this, actSeismicAndAddedMass,
                      PublicConstants::kActionPhysicalSeismicAndAddedMass);
        }
    }

           // =========================
           // 分析
           // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("分析");
        cate->setObjectName("analysisObj");

        auto* panel = cate->addPannel("分析类型");

        auto* actParam =
            makeAction(this, ":/resources/menu/minimal/analysis-seepage.svg", "&渗流分析");
        auto* actStress =
            makeAction(this, ":/resources/menu/minimal/analysis-dam.svg", "&土石坝计算");
        auto* actCave =
            makeAction(this, ":/resources/menu/minimal/analysis-cave.svg", "&洞室开挖");
        auto* actSlope =
            makeAction(this, ":/resources/menu/minimal/analysis-slope.svg", "&边坡分析");
        auto* actBlock =
            makeAction(this, ":/resources/menu/minimal/analysis-block.svg", "&块体稳定计算");

        addActionToPanel(panel, actParam, 3);
        addActionToPanel(panel, actStress, 3);
        addActionToPanel(panel, actCave, 3);
        addActionToPanel(panel, actSlope, 3);
        addActionToPanel(panel, actBlock, 3);

        bindEvent(this, actParam, PublicConstants::kActionSeepageKey);
        bindEvent(this, actStress, PublicConstants::kActionStressStrainKey);
        bindEvent(this, actCave, PublicConstants::kActionCaveExcavationKey);
        bindEvent(this, actSlope, PublicConstants::kActionSlopeStabilityKey);
        bindEvent(this, actBlock, PublicConstants::kActionCaveBlockStabilityKey);
    }

           // =========================
           // 结果
           // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("结果");
        cate->setObjectName("resultObj");

               // ---------- 云图显示 ----------
        {
            auto* panel = cate->addPannel("显示");

            auto* actNeph =
                makeAction(this, ":/resources/menu/minimal/post-nephogram.svg", "&云图");
            auto* actVec =
                makeAction(this, ":/resources/menu/minimal/post-vector.svg", "&矢量");
            auto* actSlc =
                makeAction(this, ":/resources/menu/minimal/post-slice.svg", "&切面");
            auto* actDice =
                makeAction(this, ":/resources/menu/minimal/post-block.svg", "&切块");
            auto* actContourReader =
                makeAction(this, ":/resources/menu/minimal/post-nephogram.svg", "&洞室变形轮廓图");

            addActionToPanel(panel, actNeph, 3);
            addActionToPanel(panel, actVec, 3);
            addActionToPanel(panel, actSlc, 2);
            addActionToPanel(panel, actDice, 2);
            addActionToPanel(panel, actContourReader, 3);

            bindEvent(this, actNeph, PublicConstants::kActionPostProcessNephogram);
            bindEvent(this, actVec, PublicConstants::kActionPostProcessVector);
            bindEvent(this, actSlc, PublicConstants::kActionPostProcessSection);
            bindEvent(this, actDice, PublicConstants::kActionPostProcessDice);
            bindEvent(this, actContourReader,
                      PublicConstants::kActionPostProcessContourReader);
        }

               // ---------- 提取 ----------
        {
            auto* panel = cate->addPannel("提取");

            auto* actIso =
                makeAction(this, ":/resources/menu/minimal/post-isosurface.svg", "&等值面");
            auto* actIsoS =
                makeAction(this, ":/resources/menu/minimal/post-isosurface-seg.svg",
                           "&等值面分割");
            auto* actXY =
                makeAction(this, ":/resources/menu/minimal/post-curve.svg", "&曲线");

            addActionToPanel(panel, actIso, 3);
            addActionToPanel(panel, actIsoS, 3);
            addActionToPanel(panel, actXY, 3);

            bindEvent(this, actIso, PublicConstants::kActionPostProcessIsosurface);
            bindEvent(this, actIsoS,
                      PublicConstants::kActionPostProcessIsosurfaceSegmentation);
            bindEvent(this, actXY, PublicConstants::kActionPostProcessXyPlot);
        }

               // ---------- 成果 ----------
        {
            auto* panel = cate->addPannel("成果");

            auto* actArrange =
                makeAction(this, ":/resources/menu/minimal/post-arrange.svg", "&成果整理");
            auto* actResultConfig =
                makeAction(this, ":/resources/menu/minimal/post-config.svg", "&结果配置");

            addActionToPanel(panel, actArrange, 3);
            addActionToPanel(panel, actResultConfig, 3);

            bindEvent(this, actArrange, PublicConstants::kActionPostProcessResultArrange);
            bindEvent(this, actResultConfig,
                      PublicConstants::kActionPostProcessResultRuleConfig);
        }
    }
}