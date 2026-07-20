#include "UiFrame.h"

#include <EventManager.h>
#include <PublicConstants.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <StyleManager.h>

#include <DockAreaWidget.h>
#include <DockAreaTitleBar.h>
#include <DockAreaTabBar.h>
#include <DockWidgetTab.h>
#include <QStyle>
#include <QPointer>

#include <QAbstractButton>
#include <QActionGroup>
#include <QApplication>
#include <QComboBox>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSettings>
#include <QString>
#include <QSizePolicy>
#include <QSlider>
#include <QTabBar>
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
struct ThemeColors
{
    QString id;
    QString text;
    QString secondaryText;
    QString accent;
    QString window;
    QString manager;
    QString surface;
    QString surfaceAlt;
    QString panel;
    QString border;
    QString borderStrong;
    QString hover;
    QString pressed;
    QString selection;
    QString splitter;
    QString splitterHover;
    QString file;
    QString fileHover;
    QString filePressed;
    QString iconTint;
};

ThemeColors currentThemeColors()
{
    const QString theme = StyleManager::currentThemeName();
    if (theme == QStringLiteral("dark"))
    {
        return {theme,
                QStringLiteral("#e7edf2"), QStringLiteral("#aeb8c1"),
                QStringLiteral("#6aa6d1"), QStringLiteral("#1d2329"),
                QStringLiteral("#181d22"), QStringLiteral("#242b32"),
                QStringLiteral("#293139"), QStringLiteral("#293139"),
                QStringLiteral("#3d4852"), QStringLiteral("#596672"),
                QStringLiteral("#313b45"), QStringLiteral("#3a4856"),
                QStringLiteral("#34506a"), QStringLiteral("#39444e"),
                QStringLiteral("#5c6c79"), QStringLiteral("#2f78b7"),
                QStringLiteral("#3b89c6"), QStringLiteral("#27689f"),
                QStringLiteral("#dce5ec")};
    }
    if (theme == QStringLiteral("blue"))
    {
        return {theme,
                QStringLiteral("#1b3549"), QStringLiteral("#647b8d"),
                QStringLiteral("#2a6b9a"), QStringLiteral("#dfe9f1"),
                QStringLiteral("#d3e2ed"), QStringLiteral("#eef5fa"),
                QStringLiteral("#e5eff6"), QStringLiteral("#f3f8fb"),
                QStringLiteral("#b4c8d7"), QStringLiteral("#829fb5"),
                QStringLiteral("#e5f0f7"), QStringLiteral("#d5e5f0"),
                QStringLiteral("#d9eaf5"), QStringLiteral("#adc3d2"),
                QStringLiteral("#779ab4"), QStringLiteral("#2b70a6"),
                QStringLiteral("#3c82b5"), QStringLiteral("#205d8c"), QString()};
    }
    return {QStringLiteral("light"),
            QStringLiteral("#000000"), QStringLiteral("#68727c"),
            QStringLiteral("#2c5368"), QStringLiteral("#f0f2f4"),
            QStringLiteral("#edf0f2"), QStringLiteral("#ffffff"),
            QStringLiteral("#f6f7f8"), QStringLiteral("#fbfcfc"),
            QStringLiteral("#d7dcdf"), QStringLiteral("#bac5cc"),
            QStringLiteral("#eef2f4"), QStringLiteral("#e0e8ec"),
            QStringLiteral("#e6ecef"), QStringLiteral("#d9dee1"),
            QStringLiteral("#aebac1"), QStringLiteral("#2f6f9f"),
            QStringLiteral("#3b7eae"), QStringLiteral("#285f89"), QString()};
}

QString themedStyle(QString style, const ThemeColors& c)
{
    const QPair<QString, QString> tokens[] = {
        {QStringLiteral("@text@"), c.text},
        {QStringLiteral("@secondaryText@"), c.secondaryText},
        {QStringLiteral("@accent@"), c.accent},
        {QStringLiteral("@window@"), c.window},
        {QStringLiteral("@manager@"), c.manager},
        {QStringLiteral("@surface@"), c.surface},
        {QStringLiteral("@surfaceAlt@"), c.surfaceAlt},
        {QStringLiteral("@panel@"), c.panel},
        {QStringLiteral("@border@"), c.border},
        {QStringLiteral("@borderStrong@"), c.borderStrong},
        {QStringLiteral("@hover@"), c.hover},
        {QStringLiteral("@pressed@"), c.pressed},
        {QStringLiteral("@selection@"), c.selection},
        {QStringLiteral("@splitter@"), c.splitter},
        {QStringLiteral("@splitterHover@"), c.splitterHover},
        {QStringLiteral("@file@"), c.file},
        {QStringLiteral("@fileHover@"), c.fileHover},
        {QStringLiteral("@filePressed@"), c.filePressed}};
    for (const auto& token : tokens)
        style.replace(token.first, token.second);
    return style;
}

QIcon themedIcon(const QString& path)
{
    const ThemeColors colors = currentThemeColors();
    if (path.isEmpty() || colors.iconTint.isEmpty())
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
        painter.fillRect(pixmap.rect(), QColor(colors.iconTint));
        painter.end();
        result.addPixmap(pixmap);
    }
    return result.isNull() ? QIcon(path) : result;
}

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
    auto* action = new QAction(themedIcon(icon), text, ctx);
    action->setProperty("themeIconPath", icon);
    return action;
}

inline void applyActionIcon(QAction* action, const QString& icon)
{
    if (!action || icon.isEmpty())
        return;
    action->setProperty("themeIconPath", icon);
    action->setIcon(themedIcon(icon));
}

QString dockActionIconPath(const QString& name)
{
    const QString lower = name.toLower();
    if (lower.contains(QStringLiteral("可视化")) || lower.contains(QStringLiteral("图形")) ||
        lower.contains(QStringLiteral("视图")) || lower.contains(QStringLiteral("viewer")) ||
        lower.contains(QStringLiteral("render")))
        return QStringLiteral(":/resources/menu/concept/view-visualization.svg");

    if (lower.contains(QStringLiteral("模型")) || lower.contains(QStringLiteral("工程")) ||
        lower.contains(QStringLiteral("tree")) || lower.contains(QStringLiteral("manager")))
        return QStringLiteral(":/resources/menu/concept/view-model.svg");

    if (lower.contains(QStringLiteral("物理")) || lower.contains(QStringLiteral("量")) ||
        lower.contains(QStringLiteral("probe")) || lower.contains(QStringLiteral("result")))
        return QStringLiteral(":/resources/menu/concept/view-quantity.svg");

    if (lower.contains(QStringLiteral("设置")) || lower.contains(QStringLiteral("属性")) ||
        lower.contains(QStringLiteral("panel")) || lower.contains(QStringLiteral("property")))
        return QStringLiteral(":/resources/menu/concept/view-settings.svg");

    if (lower.contains(QStringLiteral("日志")) || lower.contains(QStringLiteral("输出")) ||
        lower.contains(QStringLiteral("消息")) || lower.contains(QStringLiteral("log")) ||
        lower.contains(QStringLiteral("console")))
        return QStringLiteral(":/resources/menu/concept/view-log.svg");

    return QStringLiteral(":/resources/menu/concept/view-window.svg");
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

    if (name == QStringLiteral("模型管理"))
        m_modelDockWidget = dockWidget;
    else if (name == QStringLiteral("设置"))
        m_settingsDockWidget = dockWidget;
    else if (name == QStringLiteral("可视化"))
        m_visualDockWidget = dockWidget;
    else if (name == QStringLiteral("物理量"))
        m_physicalDockWidget = dockWidget;
    else if (name == QStringLiteral("日志"))
        m_logDockWidget = dockWidget;

    // ADS 默认会为普通 QWidget 再套一层 QScrollArea。可视化这类自适应画布不需要
    // 外层滚动区，否则窗口变窄时会出现横向滚动条，并把右侧工具按钮推到视口外。
    const bool forceNoScrollArea =
        widget && widget->property("dockForceNoScrollArea").toBool();
    dockWidget->setWidget(widget, forceNoScrollArea ? ads::CDockWidget::ForceNoScrollArea
                                                    : ads::CDockWidget::AutoScrollArea);

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

    QAction* toggleAction = dockWidget->toggleViewAction();
    registerDockToggleAction(toggleAction, name);
    arrangeDefaultDockLayout();
    return toggleAction;
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

    QAction* toggleAction = dockWidget->toggleViewAction();
    registerDockToggleAction(toggleAction, name);
    return toggleAction;
}

void UiFrame::restoreUiState()
{
    QSettings settings("config/file.ini", QSettings::IniFormat);
    const QByteArray savedState = settings.value("stateData").toByteArray();

    if (!savedState.isEmpty())
        m_dockManager->restoreState(savedState);

    if (settings.contains("windowSize"))
        resize(settings.value("windowSize").toSize());

    // m_defaultDockState 保留插件首次创建窗口时的布局，用于“恢复默认布局”。
    // 用户保存的布局只负责启动恢复，不能覆盖真正的默认布局。

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
    if (!m_dockManager)
        return;

    const ThemeColors c = currentThemeColors();
    QString qss = QString::fromUtf8(R"(
        ads--CDockManager,
        ads--CDockContainerWidget {
            background: @manager@;
        }

        ads--CDockAreaWidget {
            background: @surface@;
            border: 1px solid @border@;
        }

        ads--CDockAreaWidget[focused="true"] {
            border-color: @borderStrong@;
        }

        QSplitter::handle {
            width: 3px;
            height: 3px;
            background: @splitter@;
        }

        QSplitter::handle:hover {
            background: @splitterHover@;
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
            background: @surfaceAlt@;
            border: none;
            qproperty-spacing: 8;
        }

        ads--CAutoHideTab {
            color: @text@;
            background: transparent;
            border: none;
            border-radius: 2px;
            padding: 4px;
            qproperty-iconSize: 14px 14px;
        }

        ads--CAutoHideTab:hover,
        ads--CAutoHideTab[activeTab="true"] {
            color: @accent@;
            background: @hover@;
        }

        ads--CAutoHideDockContainer {
            background: @surface@;
            border: 1px solid @borderStrong@;
        }
    )");

    if (c.id == QStringLiteral("dark"))
    {
        qss += QString::fromUtf8(R"(
            #dockAreaAutoHideButton {
                qproperty-icon: url(:/resources/images/dock-pin-light.svg);
            }
            ads--CAutoHideDockContainer #dockAreaAutoHideButton {
                qproperty-icon: url(:/resources/images/dock-pin-pinned-light.svg);
            }
        )");
    }
    else if (c.id == QStringLiteral("blue"))
    {
        qss += QString::fromUtf8(R"(
            ads--CDockManager,
            ads--CDockContainerWidget {
                background: #d3e2ed;
            }

            ads--CDockAreaWidget {
                background: #f7fafc;
                border: 1px solid #91adbf;
            }

            ads--CDockAreaWidget[focused="true"] {
                border-color: #4d82a8;
            }

            QSplitter::handle {
                background: #adc3d2;
            }

            QSplitter::handle:hover {
                background: #779ab4;
            }

            ads--CAutoHideSideBar {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                            stop:0 #f3f8fb, stop:1 #e1ecf4);
                border: 1px solid #a5bccd;
            }
        )");
    }

    m_dockManager->setStyleSheet(themedStyle(qss, c));
}


void UiFrame::updateAreaTabStyle(ads::CDockAreaWidget* area)
{
    if (!area)
        return;

    auto* titleBar = area->titleBar();
    if (!titleBar)
        return;

    const ThemeColors c = currentThemeColors();
    const bool isSingle = area->openDockWidgetsCount() <= 1;

    QString titleStyle = QString::fromUtf8(R"(
        ads--CDockAreaTitleBar {
            background: @surfaceAlt@;
            border: none;
            border-bottom: 1px solid @border@;
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
            background: @hover@;
        }

        ads--CDockAreaTitleBar ads--CTitleBarButton:pressed {
            background: @pressed@;
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
    )");

    if (c.id == QStringLiteral("dark"))
    {
        titleStyle += QString::fromUtf8(R"(
            ads--CDockAreaTitleBar #dockAreaCloseButton {
                qproperty-icon: url(:/resources/images/dock-close-light.svg);
            }
            ads--CDockAreaTitleBar #dockAreaAutoHideButton {
                qproperty-icon: url(:/resources/images/dock-pin-light.svg);
            }
            ads--CAutoHideDockContainer ads--CDockAreaTitleBar #dockAreaAutoHideButton {
                qproperty-icon: url(:/resources/images/dock-pin-pinned-light.svg);
            }
            ads--CDockAreaTitleBar #tabsMenuButton {
                qproperty-icon: url(:/resources/images/dock-menu-light.svg);
            }
            ads--CDockAreaTitleBar #dockAreaUndockButton {
                qproperty-icon: url(:/resources/images/dock-detach-light.svg);
            }
        )");
    }
    else if (c.id == QStringLiteral("blue"))
    {
        titleStyle += QString::fromUtf8(R"(
            ads--CDockAreaTitleBar {
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                            stop:0 #f6fafd, stop:0.48 #edf4f8,
                                            stop:1 #dce9f3);
                border-top: 1px solid #fbfdfe;
                border-bottom: 1px solid #9db6c8;
            }

            ads--CDockAreaTitleBar ads--CTitleBarButton:hover {
                background: #e2eef6;
                border: 1px solid #9eb7c8;
            }

            ads--CDockAreaTitleBar ads--CTitleBarButton:pressed {
                background: #d2e4f0;
                border: 1px solid #7f9fb6;
            }
        )");
    }
    titleBar->setStyleSheet(themedStyle(titleStyle, c));

    auto* tabBar = titleBar->tabBar();
    if (!tabBar)
        return;

    tabBar->setStyleSheet(QStringLiteral("background: transparent; border: none;"));

    QString singleTabStyle = themedStyle(QString::fromUtf8(R"(
        ads--CDockWidgetTab,
        ads--CDockWidgetTab:hover,
        ads--CDockWidgetTab[activeTab="true"],
        ads--CDockWidgetTab[focused="true"] {
            color: @text@;
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
            color: @text@;
            background: transparent;
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "Segoe UI";
            font-size: 9.5pt;
            font-weight: 600;
        }
    )"), c);

    QString multiTabStyle = themedStyle(QString::fromUtf8(R"(
        ads--CDockWidgetTab {
            color: @text@;
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
            color: @accent@;
            background: @hover@;
        }

        ads--CDockWidgetTab[activeTab="true"],
        ads--CDockWidgetTab[focused="true"] {
            color: @accent@;
            background: @surface@;
            border-bottom: 2px solid @accent@;
        }

        ads--CDockWidgetTab QLabel#dockWidgetTabLabel {
            color: @text@;
            background: transparent;
            font-family: "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "Segoe UI";
            font-size: 9pt;
            font-weight: 500;
        }

        ads--CDockWidgetTab:hover QLabel#dockWidgetTabLabel {
            color: @accent@;
        }

        ads--CDockWidgetTab[activeTab="true"] QLabel#dockWidgetTabLabel,
        ads--CDockWidgetTab[focused="true"] QLabel#dockWidgetTabLabel {
            color: @accent@;
            font-weight: 600;
        }
    )"), c);

    if (c.id == QStringLiteral("blue"))
    {
        singleTabStyle += QString::fromUtf8(R"(
            ads--CDockWidgetTab,
            ads--CDockWidgetTab:hover,
            ads--CDockWidgetTab[activeTab="true"],
            ads--CDockWidgetTab[focused="true"] {
                color: #173a57;
                background: transparent;
            }

            ads--CDockWidgetTab QLabel#dockWidgetTabLabel,
            ads--CDockWidgetTab:hover QLabel#dockWidgetTabLabel,
            ads--CDockWidgetTab[activeTab="true"] QLabel#dockWidgetTabLabel,
            ads--CDockWidgetTab[focused="true"] QLabel#dockWidgetTabLabel {
                color: #173a57;
            }
        )");

        multiTabStyle += QString::fromUtf8(R"(
            ads--CDockWidgetTab {
                color: #173a57;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                            stop:0 #f5f9fc, stop:1 #e1ecf4);
                border-right: 1px solid #afc4d2;
                border-bottom: 1px solid #9db6c8;
            }

            ads--CDockWidgetTab:hover {
                color: #155e93;
                background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                            stop:0 #f9fcfe, stop:1 #e3eef6);
            }

            ads--CDockWidgetTab[activeTab="true"],
            ads--CDockWidgetTab[focused="true"] {
                color: #155e93;
                background: #f7fafc;
                border-bottom: 2px solid #2b70a6;
            }
        )");
    }

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

void UiFrame::applyThemeToUi()
{
    const ThemeColors c = currentThemeColors();

    if (m_ribbon)
    {
        m_ribbon->setStyleSheet(themedStyle(QString::fromUtf8(R"(
            SARibbonBar#geoCaeRibbonBar {
                background: @surface@;
                border: none;
                border-bottom: 1px solid @border@;
            }

            SARibbonBar#geoCaeRibbonBar QLabel {
                color: @text@;
            }

            SARibbonBar#geoCaeRibbonBar QStackedWidget {
                background: @surface@;
                border: none;
                border-top: 1px solid @borderStrong@;
                margin: 0px;
                padding: 0px;
            }

            SARibbonBar#geoCaeRibbonBar QStackedWidget > QWidget,
            SARibbonBar#geoCaeRibbonBar SARibbonCategory,
            SARibbonBar#geoCaeRibbonBar SARibbonCategory > QWidget {
                background: @surface@;
                border: none;
                margin: 0px;
                padding: 0px;
            }

            SARibbonBar#geoCaeRibbonBar SARibbonPannel,
            SARibbonBar#geoCaeRibbonBar SARibbonPannel > QWidget {
                background: @panel@;
                border: none;
                border-right: 1px solid @border@;
            }

            SARibbonBar#geoCaeRibbonBar SARibbonCategoryScrollButton,
            SARibbonBar#geoCaeRibbonBar SARibbonButtonGroupWidget {
                color: @text@;
                background: @surface@;
                border: none;
            }

            SARibbonBar#geoCaeRibbonBar QToolButton {
                color: @text@;
                background: transparent;
                border: 1px solid transparent;
                border-radius: 2px;
                padding: 3px 6px;
            }

            SARibbonBar#geoCaeRibbonBar QToolButton:hover {
                color: @accent@;
                background: @hover@;
                border-color: @border@;
            }

            SARibbonBar#geoCaeRibbonBar QToolButton:pressed,
            SARibbonBar#geoCaeRibbonBar QToolButton:checked {
                color: @accent@;
                background: @pressed@;
                border-color: @borderStrong@;
            }
        )"), c));

        if (c.id == QStringLiteral("blue"))
        {
            m_ribbon->setStyleSheet(m_ribbon->styleSheet() + QString::fromUtf8(R"(
                SARibbonBar#geoCaeRibbonBar {
                    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                stop:0 #f8fbfd, stop:0.42 #f1f6fa,
                                                stop:1 #e1ecf4);
                    border-bottom: 1px solid #8eaabd;
                }

                SARibbonBar#geoCaeRibbonBar QStackedWidget {
                    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                stop:0 #f7fafc, stop:1 #e7f0f6);
                    border: none;
                    border-top: 1px solid #8fa9bb;
                    margin: 0px;
                    padding: 0px;
                }

                SARibbonBar#geoCaeRibbonBar QStackedWidget > QWidget,
                SARibbonBar#geoCaeRibbonBar SARibbonCategory,
                SARibbonBar#geoCaeRibbonBar SARibbonCategory > QWidget {
                    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                stop:0 #f7fafc, stop:1 #e7f0f6);
                    border: none;
                    margin: 0px;
                    padding: 0px;
                }

                SARibbonBar#geoCaeRibbonBar SARibbonPannel,
                SARibbonBar#geoCaeRibbonBar SARibbonPannel > QWidget {
                    background: transparent;
                    border-right: 1px solid #b1c5d3;
                }

                SARibbonBar#geoCaeRibbonBar SARibbonCategoryScrollButton,
                SARibbonBar#geoCaeRibbonBar SARibbonButtonGroupWidget {
                    background: transparent;
                }

                SARibbonBar#geoCaeRibbonBar QToolButton:hover {
                    color: #155e93;
                    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                stop:0 #f9fcfe, stop:1 #e1edf5);
                    border-color: #9db6c8;
                }

                SARibbonBar#geoCaeRibbonBar QToolButton:pressed,
                SARibbonBar#geoCaeRibbonBar QToolButton:checked {
                    color: #155e93;
                    background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                stop:0 #d4e5f0, stop:1 #edf4f8);
                    border-color: #829fb5;
                }
            )"));
        }

        if (auto* categoryTabBar = m_ribbon->findChild<QTabBar*>())
        {
            categoryTabBar->setDocumentMode(true);
            categoryTabBar->setExpanding(false);
            categoryTabBar->setStyleSheet(themedStyle(QString::fromUtf8(R"(
                QTabBar {
                    background: @surface@;
                    border: none;
                }

                QTabBar::tab {
                    color: @text@;
                    background: transparent;
                    border: none;
                    border-radius: 0px;
                    min-width: 0px;
                    min-height: 30px;
                    padding: 0px 10px;
                    margin: 0px;
                    font-size: 10pt;
                }

                QTabBar::tab:hover:!selected {
                    color: @accent@;
                    background: @hover@;
                }

                QTabBar::tab:selected {
                    color: @accent@;
                    background: @surface@;
                    border-left: 1px solid @borderStrong@;
                    border-top: 1px solid @borderStrong@;
                    border-right: 1px solid @borderStrong@;
                    border-bottom: 1px solid @surface@;
                    margin-bottom: -1px;
                    font-weight: 600;
                }
            )"), c));

            if (c.id == QStringLiteral("blue"))
            {
                categoryTabBar->setStyleSheet(categoryTabBar->styleSheet() + QString::fromUtf8(R"(
                    QTabBar {
                        background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                                    stop:0 #f6fafd, stop:1 #e5eef5);
                        border: none;
                    }

                    QTabBar::tab {
                        color: #173a57;
                        background: transparent;
                    }

                    QTabBar::tab:hover:!selected {
                        color: #155e93;
                        background: #e5f0f7;
                    }

                    QTabBar::tab:selected {
                        color: #155e93;
                        background: #f7fafc;
                        border-left: 1px solid #8fa9bb;
                        border-top: 1px solid #8fa9bb;
                        border-right: 1px solid #8fa9bb;
                        border-bottom: 1px solid #f7fafc;
                    }
                )"));
            }
        }

        if (QAbstractButton* button = m_ribbon->applicationButton())
        {
            button->setStyleSheet(themedStyle(QString::fromUtf8(R"(
                QAbstractButton#geoCaeFileButton {
                    color: #ffffff;
                    background: @file@;
                    border: none;
                    border-radius: 0px;
                    padding: 0px 15px;
                    font-size: 10pt;
                    font-weight: 600;
                }

                QAbstractButton#geoCaeFileButton:hover {
                    color: #ffffff;
                    background: @fileHover@;
                }

                QAbstractButton#geoCaeFileButton:pressed,
                QAbstractButton#geoCaeFileButton:checked {
                    color: #ffffff;
                    background: @filePressed@;
                }
            )"), c));
        }
    }

    const QString menuStyle = themedStyle(QString::fromUtf8(R"(
        QMenu {
            color: @text@;
            background: @surface@;
            border: 1px solid @border@;
            padding: 3px 0px;
        }

        QMenu::item {
            padding: 6px 28px 6px 12px;
            background: transparent;
        }

        QMenu::item:selected {
            color: @text@;
            background: @hover@;
        }

        QMenu::item:disabled {
            color: @secondaryText@;
        }

        QMenu::separator {
            height: 1px;
            margin: 4px 9px;
            background: @border@;
        }
    )"), c);
    for (QMenu* menu : findChildren<QMenu*>())
    {
        if (!menu)
            continue;
        menu->setStyleSheet(menuStyle);
        menu->style()->unpolish(menu);
        menu->style()->polish(menu);
    }
    if (m_applicationMenu)
        m_applicationMenu->setStyleSheet(menuStyle);

    setDockWidgetStyle();
    updateAllContainersTabStyle();
    updateThemedActionIcons();
    updateThemeActionChecks();
    update();
}

void UiFrame::switchTheme(const QString& themeName)
{
    auto* app = qobject_cast<QApplication*>(QApplication::instance());
    if (!app)
        return;

    const QString normalized = StyleManager::normalizeThemeName(themeName);
    if (!StyleManager::applyTheme(*app, normalized))
        return;

    const QString settingsPath =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/file.ini"));
    QSettings settings(settingsPath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("General/uiTheme"), normalized);
    settings.sync();

    applyThemeToUi();

    QVariantMap data;
    data.insert(QStringLiteral("theme"), normalized);
    EventManager::instance().sendEvent(PublicConstants::kEventUiThemeChanged, data,
                                       EventManager::Dispatch::Direct);
}

void UiFrame::updateThemeActionChecks()
{
    const QString theme = StyleManager::currentThemeName();
    if (m_themeLightAction)
        m_themeLightAction->setChecked(theme == QStringLiteral("light"));
    if (m_themeBlueAction)
        m_themeBlueAction->setChecked(theme == QStringLiteral("blue"));
    if (m_themeDarkAction)
        m_themeDarkAction->setChecked(theme == QStringLiteral("dark"));
}

void UiFrame::updateThemedActionIcons()
{
    const QList<QAction*> actions = findChildren<QAction*>();
    for (QAction* action : actions)
    {
        if (!action)
            continue;
        const QString path = action->property("themeIconPath").toString();
        if (!path.isEmpty())
            action->setIcon(themedIcon(path));
    }
}

void UiFrame::registerDockToggleAction(QAction* action, const QString& name)
{
    if (!action)
        return;

    for (const QPointer<QAction>& existing : m_dockToggleActions)
    {
        if (existing == action)
            return;
    }

    action->setText(name);
    action->setToolTip(tr("显示或隐藏%1窗口").arg(name));
    applyActionIcon(action, dockActionIconPath(name));
    m_dockToggleActions.append(action);

    addDockActionToWindowPanel(action, name);
}

void UiFrame::addDockActionToWindowPanel(QAction* toggleAction, const QString& name)
{
    if (!m_windowPanel || !toggleAction)
        return;

    // DockWidget 的原始 toggleViewAction 是可勾选 QAction。直接放进 Ribbon 后，
    // 窗口处于显示状态时按钮会长期绘制 checked 背景，看起来像一排色块。
    // Ribbon 使用无勾选状态的展示 Action，点击时再转发给真正的窗口开关。
    auto* viewAction = makeAction(this, dockActionIconPath(name), name);
    viewAction->setToolTip(tr("显示或隐藏%1窗口").arg(name));
    viewAction->setCheckable(false);
    viewAction->setEnabled(toggleAction->isEnabled());
    viewAction->setProperty("dockToggleFacade", true);

    connect(viewAction, &QAction::triggered, this,
            [toggleAction]()
            {
                if (toggleAction)
                    toggleAction->trigger();
            });

    connect(toggleAction, &QAction::changed, viewAction,
            [viewAction, toggleAction]()
            {
                if (viewAction && toggleAction)
                    viewAction->setEnabled(toggleAction->isEnabled());
            });

    addActionToPanel(m_windowPanel, viewAction, 3);
}

void UiFrame::showAllDockWidgets()
{
    for (const QPointer<QAction>& action : m_dockToggleActions)
    {
        if (action && !action->isChecked())
            action->trigger();
    }
    updateAllContainersTabStyle();
}

void UiFrame::resetDockLayout()
{
    if (!m_dockManager)
        return;

    bool restored = false;
    if (!m_defaultDockState.isEmpty())
        restored = m_dockManager->restoreState(m_defaultDockState);

    if (!restored)
        showAllDockWidgets();

    setDockWidgetStyle();
    updateAllContainersTabStyle();
}

void UiFrame::showAboutDialog()
{
    QString versionLine;
    const QString version = QCoreApplication::applicationVersion().trimmed();
    if (!version.isEmpty())
        versionLine = tr("<p>版本：%1</p>").arg(version.toHtmlEscaped());

    const QString text =
        tr("<h3>POWERCHINA-GEOCAE</h3>"
           "<p>电建岩土工程仿真平台</p>"
           "%1"
           "<p>用于岩土工程的几何、网格、材料、约束荷载、计算分析与结果处理。</p>")
            .arg(versionLine);

    QMessageBox::about(this, tr("关于 POWERCHINA-GEOCAE"), text);
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

void UiFrame::arrangeDefaultDockLayout()
{
    if (m_defaultLayoutArranged || !m_dockManager || !m_modelDockWidget ||
        !m_settingsDockWidget || !m_visualDockWidget || !m_physicalDockWidget ||
        !m_logDockWidget)
        return;

    // 固定默认结构：模型管理在左、设置在中、可视化在右上，
    // 物理量与日志作为右下方同一停靠区的标签页。
    ads::CDockAreaWidget* modelArea =
        m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_modelDockWidget);
    ads::CDockAreaWidget* settingsArea =
        m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_settingsDockWidget, modelArea);
    ads::CDockAreaWidget* visualArea =
        m_dockManager->addDockWidget(ads::RightDockWidgetArea, m_visualDockWidget, settingsArea);
    ads::CDockAreaWidget* outputArea =
        m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_physicalDockWidget, visualArea);
    m_dockManager->addDockWidgetTabToArea(m_logDockWidget, outputArea);

    m_centerDockArea = settingsArea;
    m_defaultLayoutArranged = true;
    captureDefaultDockState();
    setDockWidgetStyle();
    updateAllContainersTabStyle();
}

void UiFrame::initSARibbon()
{
    SARibbonBar::initHighDpi();
    setWindowTitle(QStringLiteral("POWERCHINA-GEOCAE"));

    m_ribbon = ribbonBar();
    m_ribbon->setObjectName(QStringLiteral("geoCaeRibbonBar"));
    m_ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactTwoRow);
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2013);

    buildRibbonAppButton();
    buildRibbonTabs();
    applyThemeToUi();
}


void UiFrame::buildRibbonAppButton()
{
    QAbstractButton* btn = m_ribbon->applicationButton();
    btn->setObjectName(QStringLiteral("geoCaeFileButton"));
    btn->setText(QStringLiteral("文件"));
    btn->setMinimumWidth(64);
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

               // ---------- 土体本构 ----------
        {
            auto* panel = cate->addPannel("土体本构");

            auto* actSoilEb =
                makeAction(this, ":/resources/menu/minimal/mat-soil-eb.svg", "E-B本构");
            auto* actSoilEmu =
                makeAction(this, ":/resources/menu/minimal/mat-soil-emu.svg", "E-μ本构");
            auto* actSoilEmuCrush =
                makeAction(this, ":/resources/menu/minimal/mat-soil-emu-crush.svg", "E-μ破碎本构");
            auto* actNanshui =
                makeAction(this, ":/resources/menu/minimal/mat-nanshui.svg", "南水非线性本构");
            auto* actKg =
                makeAction(this, ":/resources/menu/minimal/mat-kg.svg", "K-G本构");

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

               // ---------- 岩体本构 ----------
        {
            auto* panel = cate->addPannel("岩体本构");

            auto* actHoekBrown =
                makeAction(this, ":/resources/menu/minimal/mat-dagangshan.svg", "霍克-布朗本构");
            auto* actDagangshan =
                makeAction(this, ":/resources/menu/minimal/mat-dagangshan.svg", "摩尔-库仑本构");

            addActionToPanel(panel, actHoekBrown, 2);
            addActionToPanel(panel, actDagangshan, 2);

            addMaterialAction(actHoekBrown, QStringLiteral("hoek_brown_rock"));
            addMaterialAction(actDagangshan, QStringLiteral("dagangshan_isotropic"));
        }

               // ---------- 工程模型 ----------
        {
            auto* panel = cate->addPannel("工程模型");

            auto* actHjp =
                makeAction(this, ":/resources/menu/minimal/mat-hjp.svg", "围岩开挖模型");
            auto* actDynamic =
                makeAction(this, ":/resources/menu/minimal/mat-dynamic.svg", "坝料动力本构");
            auto* actBar =
                makeAction(this, ":/resources/menu/minimal/mat-bar.svg", "锚杆单元模型");

            addActionToPanel(panel, actHjp, 2);
            addActionToPanel(panel, actDynamic, 2);
            addActionToPanel(panel, actBar, 2);

            addMaterialAction(actHjp, QStringLiteral("huangjinping_underground_plant"));
            addMaterialAction(actDynamic, QStringLiteral("dam_dynamic"));
            addMaterialAction(actBar, QStringLiteral("huangjinping_bar_element"));
        }

               // ---------- 材料参考 ----------
        {
            auto* panel = cate->addPannel("材料参考");

            auto* actBuiltinMaterial =
                makeAction(this, ":/resources/menu/minimal/mat-builtin-material.svg", "材料示例");
            auto* actBuiltinSeepage =
                makeAction(this, ":/resources/menu/minimal/mat-builtin-seepage.svg", "渗流示例");
            addActionToPanel(panel, actBuiltinMaterial, 3);
            addActionToPanel(panel, actBuiltinSeepage, 3);

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
            addActionToPanel(panel, actNeph, 3);
            addActionToPanel(panel, actVec, 3);
            addActionToPanel(panel, actSlc, 2);
            addActionToPanel(panel, actDice, 2);

            bindEvent(this, actNeph, PublicConstants::kActionPostProcessNephogram);
            bindEvent(this, actVec, PublicConstants::kActionPostProcessVector);
            bindEvent(this, actSlc, PublicConstants::kActionPostProcessSection);
            bindEvent(this, actDice, PublicConstants::kActionPostProcessDice);
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

    }

    // =========================
    // 视图
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage(QStringLiteral("视图"));
        cate->setObjectName(QStringLiteral("viewObj"));

        // ---------- 面板 ----------
        m_windowPanel = cate->addPannel(QStringLiteral("窗口"));
        for (const QPointer<QAction>& action : m_dockToggleActions)
        {
            if (action)
                addDockActionToWindowPanel(action, action->text());
        }

        // ---------- 布局 ----------
        {
            auto* panel = cate->addPannel(QStringLiteral("布局"));
            auto* actShowAll =
                makeAction(this, ":/resources/menu/concept/view-show-all.svg", "显示全部窗口");
            auto* actReset =
                makeAction(this, ":/resources/menu/concept/view-reset-layout.svg", "恢复默认布局");

            addActionToPanel(panel, actShowAll, 3);
            addActionToPanel(panel, actReset, 3);

            connect(actShowAll, &QAction::triggered, this, &UiFrame::showAllDockWidgets);
            connect(actReset, &QAction::triggered, this, &UiFrame::resetDockLayout);
        }

        // ---------- 界面 ----------
        {
            auto* panel = cate->addPannel(QStringLiteral("界面"));
            auto* actTheme =
                makeAction(this, ":/resources/menu/concept/view-theme.svg", "界面主题");
            auto* themeMenu = new SARibbonMenu(this);

            m_themeActionGroup = new QActionGroup(this);
            m_themeActionGroup->setExclusive(true);

            m_themeLightAction = themeMenu->addAction(QStringLiteral("浅灰"));
            m_themeBlueAction = themeMenu->addAction(QStringLiteral("工程蓝"));
            m_themeDarkAction = themeMenu->addAction(QStringLiteral("暗色"));

            const QList<QPair<QAction*, QString>> themeActions = {
                {m_themeLightAction, QStringLiteral("light")},
                {m_themeBlueAction, QStringLiteral("blue")},
                {m_themeDarkAction, QStringLiteral("dark")}};
            for (const auto& one : themeActions)
            {
                one.first->setCheckable(true);
                one.first->setData(one.second);
                m_themeActionGroup->addAction(one.first);
            }

            connect(m_themeActionGroup, &QActionGroup::triggered, this,
                    [this](QAction* action)
                    {
                        if (action)
                            switchTheme(action->data().toString());
                    });

            actTheme->setMenu(themeMenu);
            addActionToPanel(panel, actTheme, 3);
            updateThemeActionChecks();
        }
    }

    // =========================
    // 帮助
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage(QStringLiteral("帮助"));
        cate->setObjectName(QStringLiteral("helpObj"));

        auto* panel = cate->addPannel(QStringLiteral("软件"));
        auto* actAbout = makeAction(this, ":/resources/menu/concept/help-about.svg", "关于软件");
        addActionToPanel(panel, actAbout, 3);
        connect(actAbout, &QAction::triggered, this, &UiFrame::showAboutDialog);
    }

}
