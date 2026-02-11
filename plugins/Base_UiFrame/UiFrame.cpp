#include "UiFrame.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QAbstractButton>
#include <QSizePolicy>
#include <QIcon>

#include <ServiceRegistry.h>
#include <EventManager.h>
#include <PublicConstants.h>

// SARibbon
#include "SARibbonBar.h"
#include "SARibbonMenu.h"
#include "SARibbonApplicationButton.h"
#include "SARibbonPannel.h"
#include "SARibbonCategory.h"
#include "SARibbonCheckBox.h"
#include "SARibbonComboBox.h"
#include "SARibbonButtonGroupWidget.h"

namespace
{
inline void bindEvent(UiFrame* ctx, QAction* act, const char* eventKey)
{
    QObject::connect(act, &QAction::triggered, ctx, [eventKey]() {
        EventManager::instance().sendEvent(eventKey);
    });
}

inline void addActionToPanel(SARibbonPannel* panel, QAction* act, int actionType /*1 small 2 medium 3 large*/)
{
    if (!panel || !act) return;

    switch (actionType)
    {
    case 1: panel->addSmallAction(act);  break;
    case 2: panel->addMediumAction(act); break;
    case 3: panel->addLargeAction(act);  break;
    default: panel->addSmallAction(act); break;
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

    setWindowFlags(Qt::Window
                   | Qt::WindowTitleHint
                   | Qt::WindowSystemMenuHint
                   | Qt::WindowMinMaxButtonsHint
                   | Qt::WindowCloseButtonHint);

    EventManager::instance().registerHandler(PublicConstants::kEventLoadComplete,this, [this](const QVariantMap&) {
        qDebug()<<"restoreUiState";
        this->restoreUiState();
        qDebug()<<"end restoreUiState";
    });
    EventManager::instance().registerHandler(PublicConstants::kEventQuitApp,this, [this](const QVariantMap&) {
        this->storeUiState();
    });




    // 2. 如果你使用的是 ads::CDockManager，它会接管中央区域
    // 你需要针对 Dock 容器设置，否则背景会被覆盖
//    if (m_dockManager) { // 假设你的 DockManager 变量名是这个
//        m_dockManager->setStyleSheet("ads--CDockContainerWidget { background-color: white; }");
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
    ads::CDockManager::setConfigFlag(ads::CDockManager::FocusHighlighting, true);
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::DefaultAutoHideConfig);

    m_dockManager = new ads::CDockManager(this);

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
    show();
}

void UiFrame::storeUiState()
{
    if (!m_dockManager) return;

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
//        if (this->centralWidget()) {
//            // 使用 setStyleSheet 仅针对中央区域
//            this->centralWidget()->setStyleSheet("background:white;");
//        }
    QString qss = R"(
                  QSplitter::handle {
                  height: 4px; /* 间接调整分隔线的高度（如果是水平分隔线） */
                     width: 4px; /* 间接调整分隔线的宽度（如果是垂直分隔线） */
                  }
                  ads--CDockAreaWidget[focused="true"]  {
                  background: white;

                  border-color: rgb(96,181,247);
                  }

                  #dockAreaAutoHideButton {
                      qproperty-icon: url(:/resources/images/vs-pin-button.svg),
                          url(:/resources/images/vs-pin-button-disabled.svg) disabled;
                      qproperty-iconSize: 16px;
                  }

                  ads--CAutoHideDockContainer #dockAreaAutoHideButton {
                      qproperty-icon: url(:/resources/images/vs-pin-button-pinned-focused.svg);
                      qproperty-iconSize: 16px;
                  }


                  ads--CDockAreaWidget #tabsMenuButton::menu-indicator {
                      image: none;
                  }
                  QScrollArea#dockWidgetScrollArea {
                  padding: 0px;
                  }
                  QScrollArea#dockWidgetScrollArea {
                      border: 0px solid rgb(200, 200, 200);
                  }
                  ads--CDockAreaWidget {
                      border: 1px solid rgb(200, 200, 200);
                  }
                  ads--CDockWidgetTab  {
                      background: red;
                  }
                  QLabel#dockWidgetTabLabel {
                      text-align: left;
                  }
                  QScrollBar:vertical {
                      width: 10px; /* 设置垂直滚动条的宽度 */
                      background: #f0f0f0; /* 设置滚动条的背景颜色 */
                      border: none; /* 取消垂直滚动条的边框样式 */
                      border-radius: 5px; /* 设置滚动条的圆角 */
                  }

                  QScrollBar:horizontal {
                      height: 10px; /* 设置水平滚动条的高度 */
                      background: #f0f0f0; /* 设置滚动条的背景颜色 */
                      border: none; /* 取消水平滚动条的边框样式 */
                      border-radius: 5px; /* 设置滚动条的圆角 */
                  }

                  QScrollBar::handle:vertical {
                      background: #c0c0c0; /* 设置垂直滚动条滑块的背景颜色 */
                  }

                  QScrollBar::handle:horizontal {
                      background: #c0c0c0; /* 设置水平滚动条滑块的背景颜色 */
                  }
                  )";
    qDebug()<<"m_dockManager->setStyleSheet(qss)";
    m_dockManager->setStyleSheet(qss);
    qDebug()<<"m_dockManager->setStyleSheet(qss)";
}

ads::DockWidgetArea UiFrame::preferDockArea(const QString& name) const
{
    const QString lower = name.toLower();

    if (lower.contains(QStringLiteral("模型")) || lower.contains("model")
        || lower.contains(QStringLiteral("工程")) || lower.contains("tree"))
        return ads::LeftDockWidgetArea;

    if (lower.contains(QStringLiteral("日志")) || lower.contains("log")
        || lower.contains(QStringLiteral("输出")) || lower.contains(QStringLiteral("进度"))
        || lower.contains("console") || lower.contains("message"))
        return ads::BottomDockWidgetArea;

    if (lower.contains(QStringLiteral("图形")) || lower.contains(QStringLiteral("视图"))
        || lower.contains("render") || lower.contains("viewer"))
        return ads::CenterDockWidgetArea;

    if (lower.contains(QStringLiteral("属性")) || lower.contains(QStringLiteral("设置"))
        || lower.contains("property") || lower.contains("panel"))
        return ads::RightDockWidgetArea;

    return ads::RightDockWidgetArea;
}

void UiFrame::captureDefaultDockState()
{
    if (!m_dockManager) return;
    m_defaultDockState = m_dockManager->saveState();
}

void UiFrame::initSARibbon()
{
    SARibbonBar::initHighDpi();
    setWindowTitle("POWERCHINA-GEOCAE");

    m_ribbon = ribbonBar();
    m_ribbon->setRibbonStyle(SARibbonBar::RibbonStyleCompactTwoRow);
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2021Blue);

    buildRibbonAppButton();
    buildRibbonTabs();
}

void UiFrame::buildRibbonAppButton()
{
    QAbstractButton* btn = m_ribbon->applicationButton();
    btn->setText(" &文件 ");

    m_applicationMenu = new SARibbonMenu(this);
    auto* appBtn = qobject_cast<SARibbonApplicationButton*>(btn);
    appBtn->setMenu(m_applicationMenu);

    auto* actNew    = makeAction(this, ":/resources/menu/NewProject.png", "&新建工程");
    auto* actOpen   = makeAction(this, ":/resources/menu/OpenProject.png", "&打开工程");
    auto* actSave   = makeAction(this, ":/resources/menu/Save.png",       "&保存工程");
    auto* actExport = makeAction(this, ":/resources/menu/ExportDB.png",   "&导出当前数据库");
    auto* actExit   = makeAction(this, ":/resources/menu/Exit.png",       "&退出");

    m_applicationMenu->addAction(actNew);
    m_applicationMenu->addAction(actOpen);
    m_applicationMenu->addAction(actSave);
    m_applicationMenu->addAction(actExport);
    m_applicationMenu->addSeparator();
    m_applicationMenu->addAction(actExit);

    bindEvent(this, actNew,  PublicConstants::kActionNewProject);
    // 如果你有 open/save/export 对应事件，在 PublicConstants 里补上即可
    bindEvent(this, actOpen, PublicConstants::kActionOpenProject);
    bindEvent(this, actSave, PublicConstants::kActionSaveProject);
    bindEvent(this, actExport, PublicConstants::kActionExportCurrentDatabase);

    connect(actExit, &QAction::triggered, this, [this]() {
        EventManager::instance().sendEvent(PublicConstants::kEventQuitApp);
        close();
    });
}

void UiFrame::buildRibbonTabs()
{
    // =========================
    // 自定义参数
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("自定义参数");
        cate->setObjectName("paramObj");

        auto* panel = cate->addPannel("参数");
        auto* actParam = makeAction(this, ":/resources/menu/Parameter.png", "&参数");
        auto* actFunc  = makeAction(this, ":/resources/menu/Function.png",  "&函数");

        addActionToPanel(panel, actParam, 3);
        addActionToPanel(panel, actFunc,  3);

        bindEvent(this, actParam, PublicConstants::kActionCustomParam);
        bindEvent(this, actFunc,  PublicConstants::kActionCustomFunction);
    }

    // =========================
    // 几何
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("几何");
        cate->setObjectName("geomObj");

        // ---------------------------------
        // 1) 导入导出（保留）
        // ---------------------------------
        {
            auto* panel = cate->addPannel("导入导出");
            auto* actImport = makeAction(this, "", "&导入");
            auto* actExport = makeAction(this, "", "&导出");
            addActionToPanel(panel, actImport, 2);
            addActionToPanel(panel, actExport, 2);
            bindEvent(this, actImport, PublicConstants::kActionPreprocessImportGeometry);
            bindEvent(this, actExport, PublicConstants::kActionPreprocessExportGeometry);
        }

        // ---------------------------------
        // 2) 创建（Create）：基本体 + 2D轮廓(Profile)
        // ---------------------------------
        {
            auto* panel = cate->addPannel("创建");

            // ---- 2.1 基本体（你已有的这些保留）----
            auto* actBox      = makeAction(this, "", "&长方体");
            auto* actSphere   = makeAction(this, "", "&球体");
            auto* actCone     = makeAction(this, "", "&圆锥体");
            auto* actTorus    = makeAction(this, "", "&圆环");
            auto* actCylinder = makeAction(this, "", "&圆柱体");

            addActionToPanel(panel, actBox,      1);
            addActionToPanel(panel, actSphere,   1);
            addActionToPanel(panel, actCone,     1);
            addActionToPanel(panel, actTorus,    1);
            addActionToPanel(panel, actCylinder, 1);

            bindEvent(this, actBox,      PublicConstants::kActionPreprocessBox);
            bindEvent(this, actSphere,   PublicConstants::kActionPreprocessSphere);
            bindEvent(this, actCone,     PublicConstants::kActionPreprocessCone);
            bindEvent(this, actTorus,    PublicConstants::kActionPreprocessTorus);
            bindEvent(this, actCylinder, PublicConstants::kActionPreprocessCylinder);

            // ---- 2.2 2D轮廓(Profile)：替代2D草图（参数化节点）----
            auto* actProfileMain = makeAction(this, "", "&2D轮廓");
            auto* profileMenu = new SARibbonMenu(this);

            auto* actProfileRect    = makeAction(this, "", "&矩形");
            auto* actProfileCircle  = makeAction(this, "", "&圆");
            auto* actProfileRing    = makeAction(this, "", "&环");
            auto* actProfilePolygon = makeAction(this, "", "&多边形");

            profileMenu->addAction(actProfileRect);
            profileMenu->addAction(actProfileCircle);
            profileMenu->addAction(actProfileRing);
            profileMenu->addAction(actProfilePolygon);

            actProfileMain->setMenu(profileMenu);
            addActionToPanel(panel, actProfileMain, 2);

            // Profile 节点创建：节点属性里选择支撑面/偏移/尺寸，不做“工作平面”菜单
            // bindEvent(this, actProfileMain,   PublicConstants::kActionGeomCreateProfile); // 可选：总入口
            bindEvent(this, actProfileRect,    PublicConstants::kActionGeomCreatePlanarRect);
            bindEvent(this, actProfileCircle,  PublicConstants::kActionGeomCreatePlanarCircle);
            bindEvent(this, actProfileRing,    PublicConstants::kActionGeomCreateEllipse);
            bindEvent(this, actProfilePolygon, PublicConstants::kActionGeomCreatePlanarPolygon);
        }

        // ---------------------------------
        // 3) 特征（Feature）：在树上加特征节点
        // ---------------------------------
        // {
        //     auto* panel = cate->addPannel("特征");

        //     auto* actExtrude = makeAction(this, "", "&拉伸");
        //     auto* actRevolve = makeAction(this, "", "&旋转");
        //     auto* actThicken = makeAction(this, "", "&加厚");
        //     auto* actShell   = makeAction(this, "", "&抽壳");

        //     addActionToPanel(panel, actExtrude, 3);
        //     addActionToPanel(panel, actRevolve, 3);
        //     addActionToPanel(panel, actThicken, 3);
        //     addActionToPanel(panel, actShell,   3);

        //     bindEvent(this, actExtrude, PublicConstants::kActionGeomAddExtrude);
        //     bindEvent(this, actRevolve, PublicConstants::kActionGeomAddRevolve);
        //     bindEvent(this, actThicken, PublicConstants::kActionGeomAddThicken);
        //     bindEvent(this, actShell,   PublicConstants::kActionGeomAddShell);
        // }

        // ---------------------------------
        // 4) 布尔（Boolean）：不合并，保留 Fuse/Union/Cut/Common 四个节点入口
        // ---------------------------------
        {
            auto* panel = cate->addPannel("布尔");

            auto* actFuse   = makeAction(this, "", "&并集");
            auto* actUnion  = makeAction(this, "", "联&合体");
            auto* actCut    = makeAction(this, "", "&差集");
            auto* actCommon = makeAction(this, "", "&交集");

            addActionToPanel(panel, actFuse,   3);
            addActionToPanel(panel, actUnion,  3);
            addActionToPanel(panel, actCut,    3);
            addActionToPanel(panel, actCommon, 3);

            bindEvent(this, actFuse,   PublicConstants::kActionGeomFuse);
            bindEvent(this, actUnion,  PublicConstants::kActionGeomUnion);
            bindEvent(this, actCut,    PublicConstants::kActionGeomCut);
            bindEvent(this, actCommon, PublicConstants::kActionGeomCommon);
        }

        // ---------------------------------
        // 5) 修改（Modify）：节点化（边处理/偏移/分割）
        // ---------------------------------
        // {
        //     auto* panel = cate->addPannel("修改");

        //     auto* actEdgeBlend = makeAction(this, "", "&边处理"); // 圆角/倒角在节点属性里选择
        //     auto* actOffset    = makeAction(this, "", "&偏移");
        //     auto* actSplit     = makeAction(this, "", "&分割");

        //     addActionToPanel(panel, actEdgeBlend, 3);
        //     addActionToPanel(panel, actOffset,    3);
        //     addActionToPanel(panel, actSplit,     3);

        //     bindEvent(this, actEdgeBlend, PublicConstants::kActionGeomAddEdgeBlend);
        //     bindEvent(this, actOffset,    PublicConstants::kActionGeomAddOffset);
        //     bindEvent(this, actSplit,     PublicConstants::kActionGeomAddSplit);
        // }

        // // ---------------------------------
        // // 6) 变换与阵列（Transform）：节点化（变换/镜像/阵列）
        // // ---------------------------------
        // {
        //     auto* panel = cate->addPannel("变换");

        //     auto* actTransform = makeAction(this, "", "&移动/旋转");
        //     auto* actMirror    = makeAction(this, "", "&镜像");
        //     auto* actPattern   = makeAction(this, "", "&阵列"); // 线性/圆周/矩形在节点属性里选

        //     addActionToPanel(panel, actTransform, 3);
        //     addActionToPanel(panel, actMirror,    3);
        //     addActionToPanel(panel, actPattern,   3);

        //     bindEvent(this, actTransform, PublicConstants::kActionGeomAddTransform);
        //     bindEvent(this, actMirror,    PublicConstants::kActionGeomAddMirror);
        //     bindEvent(this, actPattern,   PublicConstants::kActionGeomAddPattern);
        // }

        // ---------------------------------
        // 7) 显示与测量（Display/Measure）：保留；删除“选择模式”控件
        // ---------------------------------
        // {
        //     auto* panel = cate->addPannel("显示测量");

        //     // 显示模式
        //     auto* actSolid = makeAction(this, "", "&实体显示");
        //     auto* actWire  = makeAction(this, "", "&线框显示");
        //     auto* actTrans = makeAction(this, "", "&透明显示");

        //     addActionToPanel(panel, actSolid, 3);
        //     addActionToPanel(panel, actWire,  3);
        //     addActionToPanel(panel, actTrans, 3);

        //     bindEvent(this, actSolid, PublicConstants::kActionPreprocessSolidView);
        //     bindEvent(this, actWire,  PublicConstants::kActionPreprocessLineView);
        //     bindEvent(this, actTrans, PublicConstants::kActionPreprocessTransmissionView);

        //     // 显示元素
        //     auto* actShowFace  = makeAction(this, "", "&显示面");
        //     auto* actShowLine  = makeAction(this, "", "&显示线");
        //     auto* actShowPoint = makeAction(this, "", "&显示点");

        //     addActionToPanel(panel, actShowFace,  3);
        //     addActionToPanel(panel, actShowLine,  2);
        //     addActionToPanel(panel, actShowPoint, 2);

        //     bindEvent(this, actShowFace,  PublicConstants::kActionPreprocessShowFace);
        //     bindEvent(this, actShowLine,  PublicConstants::kActionPreprocessShowLine);
        //     bindEvent(this, actShowPoint, PublicConstants::kActionPreprocessShowPoint);

        //     // 测量（合并成下拉）
        //     auto* actMeasureMain = makeAction(this, "", "&测量");
        //     auto* measureMenu = new SARibbonMenu(this);

        //     auto* actPos = makeAction(this, "", "&位置测量");
        //     auto* actCen = makeAction(this, "", "&质心测量");
        //     auto* actDis = makeAction(this, "", "&距离测量");

        //     measureMenu->addAction(actPos);
        //     measureMenu->addAction(actCen);
        //     measureMenu->addAction(actDis);

        //     actMeasureMain->setMenu(measureMenu);
        //     addActionToPanel(panel, actMeasureMain, 3);

        //     bindEvent(this, actMeasureMain, PublicConstants::kActionPreprocessMeasure);
        //     bindEvent(this, actPos, PublicConstants::kActionPreprocessMeasurePosition);
        //     bindEvent(this, actCen, PublicConstants::kActionPreprocessMeasureCentroid);
        //     bindEvent(this, actDis, PublicConstants::kActionPreprocessMeasureDistance);
        // }
    }

    // =========================
    // 网格
    // =========================
    {
        auto* cate = m_ribbon->addCategoryPage("网格");
        cate->setObjectName("gridObj");

        // ============ 导入导出 ============
        auto* panelIO = cate->addPannel("导入导出");
        auto* actImport = makeAction(this, ":/resources/menu/importGrid.png", "&导入");
        auto* actExport = makeAction(this, ":/resources/menu/exportGrid.png", "&导出");
        addActionToPanel(panelIO, actImport, 2);
        addActionToPanel(panelIO, actExport, 2);

        bindEvent(this, actImport, PublicConstants::kActionPreprocessImportGrid);
        bindEvent(this, actExport, PublicConstants::kActionPreprocessExportGrid);

        // // ============ 剖分方式 ============
         auto* panelMesh = cate->addPannel("剖分");
         auto* actGlobal = makeAction(this, ":/resources/menu/meshGlobal.png",  "&整体剖分");
        // auto* actSingle = makeAction(this, ":/resources/menu/meshSingle.png",  "&单体剖分");
        // auto* actMapped = makeAction(this, ":/resources/menu/meshMapped.png",  "&映射剖分");
        // auto* actSweep  = makeAction(this, ":/resources/menu/meshSweep.png",   "&扫掠剖分");

         addActionToPanel(panelMesh, actGlobal, 2);
        // addActionToPanel(panelMesh, actSingle, 2);
        // addActionToPanel(panelMesh, actMapped, 2);
        // addActionToPanel(panelMesh, actSweep,  2);

         bindEvent(this, actGlobal, PublicConstants::kActionPreprocessMeshGlobal);
        // bindEvent(this, actSingle, PublicConstants::kActionPreprocessMeshSingle);
        // bindEvent(this, actMapped, PublicConstants::kActionPreprocessMeshMapped);
        // bindEvent(this, actSweep,  PublicConstants::kActionPreprocessMeshSweep);

        // // ============ 控制与质量 ============
        auto* panelCtrl = cate->addPannel("控制");
        // auto* actVolCtrl  = makeAction(this, ":/resources/menu/volumeCtrl.png", "&体控制");
        // auto* actFaceCtrl = makeAction(this, ":/resources/menu/surfaceCtrl.png","&面控制");
        // auto* actEdgeCtrl = makeAction(this, ":/resources/menu/edgeCtrl.png",    "&边控制");
        auto* actMeshEdit = makeAction(this, ":/resources/menu/meshEdit.png", "&网格编辑");
        // auto* actQuality  = makeAction(this, ":/resources/menu/qualityStd.png", "&网格质量标准");

        // addActionToPanel(panelCtrl, actVolCtrl, 2);
        // addActionToPanel(panelCtrl, actFaceCtrl,2);
        // addActionToPanel(panelCtrl, actEdgeCtrl, 2);
        addActionToPanel(panelCtrl, actMeshEdit, 2);
        // addActionToPanel(panelCtrl, actQuality, 2);

        // bindEvent(this, actVolCtrl,  PublicConstants::kActionPreprocessCtrlVolume);
        // bindEvent(this, actFaceCtrl, PublicConstants::kActionPreprocessCtrlSurface);
        // bindEvent(this, actEdgeCtrl, PublicConstants::kActionPreprocessCtrlEdge);
        bindEvent(this, actMeshEdit, PublicConstants::kActionPreprocessMeshEdit);
        // bindEvent(this, actQuality,  PublicConstants::kActionPreprocessQualityStd);
    }


    // // =========================
    // // 边界条件（你已有常量，统一改 bindEvent）
    // // =========================
    // {
    //     auto* cate = m_ribbon->addCategoryPage("边界条件");
    //     cate->setObjectName("boundObj");

    //     auto* panel = cate->addPannel("边界条件");
    //     auto* actSolid = makeAction(this, ":/resources/menu/boundary_solid.png", "&域");
    //     auto* actFace  = makeAction(this, ":/resources/menu/boundary_face.png",  "&边界");
    //     auto* actLine  = makeAction(this, ":/resources/menu/boundary_line.png",  "&边");
    //     auto* actPoint = makeAction(this, ":/resources/menu/boundary_point.png", "&点");

    //     addActionToPanel(panel, actSolid, 3);
    //     addActionToPanel(panel, actFace,  3);
    //     addActionToPanel(panel, actLine,  3);
    //     addActionToPanel(panel, actPoint, 3);

    //     bindEvent(this, actSolid, PublicConstants::kActionBoundarySolid);
    //     bindEvent(this, actFace,  PublicConstants::kActionBoundaryFace);
    //     bindEvent(this, actLine,  PublicConstants::kActionBoundaryLine);
    //     bindEvent(this, actPoint, PublicConstants::kActionBoundaryPoint);
    // }

    // // =========================
    // // 结果（你已有的“标准方式”，这里也统一 bindEvent）
    // // =========================
    // {
    //     auto* cate = m_ribbon->addCategoryPage("结果");

    //     auto* panel = cate->addPannel("后处理操作");
    //     auto* actNeph = makeAction(this, ":/resources/menu/Nephogram.png", "&云图");
    //     auto* actVec  = makeAction(this, ":/resources/menu/Vector.png",   "&矢量");
    //     auto* actSlc  = makeAction(this, ":/resources/menu/Slice.png",    "&切面");
    //     auto* actDice = makeAction(this, ":/resources/menu/Block.png",    "&切块");
    //     auto* actIso  = makeAction(this, ":/resources/menu/Isosurface.png", "&等值面");
    //     auto* actIsoS = makeAction(this, ":/resources/menu/IsosurfaceSegmentation.png", "&等值面分割");
    //     auto* actXY   = makeAction(this, ":/resources/menu/Curve.png",    "&曲线");
    //     auto* actAnim = makeAction(this, ":/resources/menu/Animation.png","&动画");
    //     auto* actPick = makeAction(this, ":/resources/menu/PickUp.png",   "&拾取物理量");
    //     auto* actDer  = makeAction(this, ":/resources/menu/DerivedValue2.png", "&派生值");
    //     auto* actExp  = makeAction(this, ":/resources/menu/Vector.png",   "&数据导出");

    //     addActionToPanel(panel, actNeph, 2);
    //     addActionToPanel(panel, actVec,  2);
    //     addActionToPanel(panel, actSlc,  2);
    //     addActionToPanel(panel, actDice, 2);
    //     addActionToPanel(panel, actIso,  2);
    //     addActionToPanel(panel, actIsoS, 2);
    //     addActionToPanel(panel, actXY,   3);
    //     addActionToPanel(panel, actAnim, 3);
    //     addActionToPanel(panel, actPick, 3);
    //     addActionToPanel(panel, actDer,  2);
    //     addActionToPanel(panel, actExp,  2);

    //     bindEvent(this, actNeph, PublicConstants::kActionPostProcessNephogram);
    //     bindEvent(this, actVec,  PublicConstants::kActionPostProcessVector);
    //     bindEvent(this, actSlc,  PublicConstants::kActionPostProcessSection);
    //     bindEvent(this, actDice, PublicConstants::kActionPostProcessDice);
    //     bindEvent(this, actIso,  PublicConstants::kActionPostProcessIsosurface);
    //     bindEvent(this, actIsoS, PublicConstants::kActionPostProcessIsosurfaceSegmentation);
    //     bindEvent(this, actXY,   PublicConstants::kActionPostProcessXyPlot);
    //     bindEvent(this, actAnim, PublicConstants::kActionPostProcessAnimation);
    //     bindEvent(this, actPick, PublicConstants::kActionPostProcessPickPhysical);
    //     bindEvent(this, actDer,  PublicConstants::kActionPostProcessDerivative);
    //     bindEvent(this, actExp,  PublicConstants::kActionPostProcessDataExport);
    // }

    // // =========================
    // // 显示（视图）
    // // =========================
    // {
    //     auto* cate = m_ribbon->addCategoryPage("显示");
    //     cate->setObjectName("viewObj");

    //     auto* panel = cate->addPannel("显示");

    //     auto* actPX = makeAction(this, ":/resources/icon/view_right.png", "&+X");
    //     auto* actNX = makeAction(this, ":/resources/icon/view_left.png",  "&-X");
    //     auto* actPY = makeAction(this, ":/resources/icon/view_top.png",   "&+Y");
    //     auto* actNY = makeAction(this, ":/resources/icon/view_bottom.png","&-Y");
    //     auto* actPZ = makeAction(this, ":/resources/icon/view_front.png", "&+Z");
    //     auto* actNZ = makeAction(this, ":/resources/icon/view_back.png",  "&-Z");
    //     auto* actFit= makeAction(this, ":/resources/icon/autofit.png",    "&重置相机");
    //     auto* actLay= makeAction(this, ":/resources/icon/reset_default_setting.png", "&恢复默认布局");

    //     addActionToPanel(panel, actPX, 2);
    //     addActionToPanel(panel, actNX, 2);
    //     addActionToPanel(panel, actPY, 2);
    //     addActionToPanel(panel, actNY, 2);
    //     addActionToPanel(panel, actPZ, 2);
    //     addActionToPanel(panel, actNZ, 2);
    //     addActionToPanel(panel, actFit,3);
    //     addActionToPanel(panel, actLay,3);

    //     bindEvent(this, actPX, PublicConstants::kActionViewPosX);
    //     bindEvent(this, actNX, PublicConstants::kActionViewNegX);
    //     bindEvent(this, actPY, PublicConstants::kActionViewPosY);
    //     bindEvent(this, actNY, PublicConstants::kActionViewNegY);
    //     bindEvent(this, actPZ, PublicConstants::kActionViewPosZ);
    //     bindEvent(this, actNZ, PublicConstants::kActionViewNegZ);
    //     bindEvent(this, actFit, PublicConstants::kActionViewResetCamera);

    //     connect(actLay, &QAction::triggered, this, [this]() {
    //         if (m_dockManager && !m_defaultDockState.isEmpty())
    //             m_dockManager->restoreState(m_defaultDockState);
    //         EventManager::instance().sendEvent(PublicConstants::kActionViewResetLayout);
    //     });

    //     // checkbox：标尺/光照
    //     auto* staff = new SARibbonCheckBox(this);
    //     staff->setText("标尺显示");
    //     staff->setChecked(true);
    //     panel->addSmallWidget(staff);
    //     connect(staff, &QCheckBox::toggled, this, [](bool on) {
    //         QVariantMap data; data["checked"] = on;
    //         EventManager::instance().sendEvent(PublicConstants::kActionViewToggleStaff, data);
    //     });

    //     auto* light = new SARibbonCheckBox(this);
    //     light->setText("光照");
    //     light->setChecked(false);
    //     panel->addSmallWidget(light);
    //     connect(light, &QCheckBox::toggled, this, [](bool on) {
    //         QVariantMap data; data["checked"] = on;
    //         EventManager::instance().sendEvent(PublicConstants::kActionViewToggleLight, data);
    //     });

    //     // 透明度 slider + label
    //     auto* container = new QWidget(this);
    //     auto* lay = new QHBoxLayout(container);
    //     lay->setContentsMargins(0,0,0,0);
    //     lay->setSpacing(5);

    //     auto* lbl = new QLabel("透明度:", container);
    //     auto* slider = new QSlider(Qt::Horizontal, container);
    //     slider->setRange(1, 100);
    //     slider->setValue(100);
    //     slider->setFixedWidth(120);
    //     auto* val = new QLabel("100", container);
    //     val->setFixedWidth(40);

    //     lay->addWidget(lbl);
    //     lay->addWidget(slider);
    //     lay->addWidget(val);

    //     panel->addWidget(container, SARibbonPannelItem::Small);

    //     connect(slider, &QSlider::valueChanged, this, [val](int v) {
    //         val->setText(QString::number(v));
    //         QVariantMap data; data["value"] = v;
    //         EventManager::instance().sendEvent(PublicConstants::kActionViewSetTransparency, data);
    //     });

    //     // 主题下拉
    //     auto* groupWidget = new SARibbonButtonGroupWidget(panel);
    //     groupWidget->setMinimumWidth(170);
    //     groupWidget->setContentsMargins(0, 0, 0, 0);
    //     groupWidget->setWindowTitle("颜色主题");

    //     auto* tlabel = new QLabel(groupWidget);
    //     tlabel->setText("颜色主题");
    //     groupWidget->addWidget(tlabel);

    //     auto* combo = new SARibbonComboBox(groupWidget);
    //     combo->addItems(QStringList() << "亮色主题" << "暗色主题");
    //     combo->setMinimumWidth(100);
    //     groupWidget->addWidget(combo);

    //     panel->addWidget(groupWidget, SARibbonPannelItem::Large);

    //     connect(combo, &QComboBox::currentTextChanged, this, [](const QString& text) {
    //         QVariantMap data; data["theme"] = text;
    //         EventManager::instance().sendEvent(PublicConstants::kActionViewSetTheme, data);
    //     });
    // }

    // // =========================
    // // 代理模型（你已有常量）
    // // =========================
    // {
    //     auto* cate = m_ribbon->addCategoryPage("代理模型");
    //     cate->setObjectName("modelObj");

    //     auto* panel = cate->addPannel("代理模型");
    //     auto* actSampling = makeAction(this, ":/resources/menu/DataSampling.png", "&数据采样");

    //     auto* menu = new SARibbonMenu(this);
    //     auto* actOrtho = makeAction(this, ":/resources/menu/OrthogonalSampling.png", "&正交采样");
    //     auto* actLatin = makeAction(this, ":/resources/menu/LatinHypercubeSampling.png", "&拉丁超立方采样");
    //     menu->addAction(actOrtho);
    //     menu->addAction(actLatin);

    //     actSampling->setMenu(menu);
    //     addActionToPanel(panel, actSampling, 3);

    //     bindEvent(this, actOrtho, PublicConstants::kActionSurrogateModelOrthogonal);
    //     bindEvent(this, actLatin, PublicConstants::kActionSurrogateModelLatin);

    //     auto* actEval = makeAction(this, ":/resources/menu/DatasetEvaluation.png", "&数据集评估");
    //     auto* actDnn  = makeAction(this, ":/resources/menu/DNN.png", "&DNN");
    //     auto* actPod  = makeAction(this, ":/resources/menu/POD.png", "&POD");
    //     addActionToPanel(panel, actEval, 3);
    //     addActionToPanel(panel, actDnn,  2);
    //     addActionToPanel(panel, actPod,  2);

    //     bindEvent(this, actEval, PublicConstants::kActionSurrogateModelDataEvaluation);
    //     bindEvent(this, actDnn,  PublicConstants::kActionSurrogateModelDnn);
    //     bindEvent(this, actPod,  PublicConstants::kActionSurrogateModelPod);
    // }
}
