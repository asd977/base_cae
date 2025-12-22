#include "UiFrame.h"

#include <QDockWidget>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QDebug>
#include <QTextEdit>
#include <QMdiArea>
#include <QSettings>
#include <DockWidgetTab.h>
#include <QSplitter>
#include <QCoreApplication>
#include <ServiceRegistry.h>
#include <qtimer.h>
#include <QDir>
#include <EventManager.h>
#include <PublicConstants.h>
#include <QPainter>
#include <QFile>
#include <QLayout>
#include <QVBoxLayout>
#include <QSizePolicy>


UiFrame::UiFrame(QWidget* par, SARibbonMainWindowStyles style) : SARibbonMainWindow(par, style)
{
	initSARibbon();
    initDockManager();
    ServiceRegistry::instance().registerService<UiFrameService>(this);
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
    EventManager::instance().registerHandler(PublicConstants::kEventLoadComplete, [this](const QVariantMap &data) {
        this->restoreUiState();
    });
    EventManager::instance().registerHandler(PublicConstants::kEventQuitApp, [this](const QVariantMap &data) {
        this->storeUiState();
    });
    setWindowIcon(QIcon(":/resources/icon.svg"));
}

UiFrame::~UiFrame()
{

}

void UiFrame::closeEvent(QCloseEvent *event)
{

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

        QWidget *central = new QWidget(this);
        QVBoxLayout *centralLayout = new QVBoxLayout(central);
        centralLayout->setContentsMargins(0, 0, 0, 0);
        centralLayout->setSpacing(0);
        centralLayout->addWidget(m_dockManager);
        setCentralWidget(central);
    setDockWidgetStyle();
        captureDefaultDockState();
}

QAction *UiFrame::addWidget(QWidget *widget, QString name)
{
    ads::CDockWidget *dockWidget = new ads::CDockWidget(name);
    dockWidget->setWidget(widget);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetClosable, true);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetMovable, true);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetFloatable, true);
        const ads::DockWidgetArea area = preferDockArea(name);
        ads::CDockAreaWidget* targetArea = m_centerDockArea;
        if (!targetArea)
        {
                targetArea = m_dockManager->addDockWidget(area, dockWidget);
                if (area == ads::CenterDockWidgetArea)
                {
                        m_centerDockArea = targetArea;
                }
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
        {
                m_centerDockArea = targetArea;
        }
        captureDefaultDockState();
    return dockWidget->toggleViewAction();
}

QAction * UiFrame::addWidgetForTabwidget(QWidget * widget, QString name)
{
	ads::CDockWidget *dockWidget = new ads::CDockWidget(name);
	dockWidget->setWidget(widget);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetMovable, true);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetClosable, true);
        dockWidget->setFeature(ads::CDockWidget::DockWidgetFeature::DockWidgetFloatable, false);
        dockWidget->toggleView(false);

        if (!m_centerDockArea)
        {
                m_centerDockArea = m_dockManager->addDockWidget(ads::CenterDockWidgetArea, dockWidget);
        }
        else
        {
                m_dockManager->addDockWidgetTabToArea(dockWidget, m_centerDockArea);
        }
        captureDefaultDockState();
        return dockWidget->toggleViewAction();
}

void UiFrame::restoreUiState()
{
    // 输出当前运行目录
    QString currentDir = QCoreApplication::applicationDirPath();
    qDebug() << "当前运行目录：" << currentDir;

    QSettings settings("config/file.ini", QSettings::IniFormat);
    QByteArray savedState = settings.value("stateData").toByteArray();
    // 使用 dockManager->restoreState() 恢复状态
    bool success = m_dockManager->restoreState(savedState);

    if (settings.contains("windowSize")) {
        resize(settings.value("windowSize").toSize());
    }
	if (success)
	{
		m_defaultDockState = savedState;
	}

    if (!success)
    {
        // 恢复状态失败的处理逻辑
    }
    this->show();
}

void UiFrame::storeUiState()
{
    QDir dir;
    if (!dir.exists("config")) {
        dir.mkpath("config");
    }

    QByteArray state = m_dockManager->saveState();
    QSettings settings("config/file.ini", QSettings::IniFormat);
    settings.setValue("stateData", QVariant(state));
    settings.setValue("windowSize", this->size());
    m_dockManager->savePerspectives(settings);
}

void UiFrame::addMenuButton(QString tabName, QString groupName, QToolButton *button)
{
    //m_ribbonTabWidget->addButton(tabName, groupName, button);
}

void UiFrame::addFileButton(QPushButton *button)
{
    //m_ribbonTabWidget->addFileButton(button);
}

void UiFrame::setDockWidgetStyle()
{
	QFile qssFile(":/resources/style/dock.css");
	if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text))
	{
		QString qss = QString::fromUtf8(qssFile.readAll());
		m_dockManager->setStyleSheet(qss);
	}
}

ads::DockWidgetArea UiFrame::preferDockArea(const QString& name) const
{
	const QString lower = name.toLower();
	if (lower.contains(QStringLiteral("模型")) || lower.contains("model") || lower.contains(QStringLiteral("工程")) || lower.contains("tree"))
	{
		return ads::LeftDockWidgetArea;
	}
	if (lower.contains(QStringLiteral("日志")) || lower.contains("log") || lower.contains(QStringLiteral("输出"))
		|| lower.contains(QStringLiteral("进度")) || lower.contains("console") || lower.contains("message"))
	{
		return ads::BottomDockWidgetArea;
	}
	if (lower.contains(QStringLiteral("图形")) || lower.contains(QStringLiteral("视图")) || lower.contains("render") || lower.contains("viewer"))
	{
		return ads::CenterDockWidgetArea;
	}
	if (lower.contains(QStringLiteral("属性")) || lower.contains(QStringLiteral("设置")) || lower.contains("property") || lower.contains("panel"))
	{
		return ads::RightDockWidgetArea;
	}
	return ads::RightDockWidgetArea;
}

void UiFrame::captureDefaultDockState()
{
	if (!m_dockManager)
	{
		return;
	}
        m_defaultDockState = m_dockManager->saveState();
}

void UiFrame::test()
{
    QAction* act1 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&新建项目"));
    QAction* act2 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&保存项目"));
    QAction* act3 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&另存为项目"));
    QAction* act4 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&打开项目"));
	addMenuAction(act1);
	addMenuAction(act2);
	addMenuAction(act3);
	addMenuAction(act4);
	addRibbonCategory("&主页", "main");
	addRibbonCategory("&主页1", "main1");
	addRibbonCategory("&主页2", "main2");
	auto p = getCategoryByObjName("main");
	auto p1 = addRibbonPanel(p, "name");
	auto p2 = addRibbonPanel(p, "name2");
    QAction* m_actionCreateProject = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&新建项目"));
    QAction* m_actionCreateProject2 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&新建项目666"));
    QAction* m_actionCreateProject3 = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&新建项目888"));
	addRibbonPanelAction(p1, m_actionCreateProject);
	addRibbonPanelAction(p1, m_actionCreateProject2, 2);
	QList<QAction*> _list;
	_list << m_actionCreateProject2 << m_actionCreateProject3;
	QList<QString> _list2;
	_list2 << "666" << "777";
	addRibbonPanelCombox(p1, m_actionCreateProject, "666", _list, _list2);
	QStringList list;
	list << "888" << "999";
	addRibbonPanelCheckBox(p1, "123", "123");
	addRibbonPanelBtnWidget(p2, "123", "123", "字体", "ziti", "combox", list);
	setRibbonPanelEnabled(p2,false);
	//setRibbonCategoryEnabled(getCategoryByObjName("main"),false);
	// 创建滑动条和标签
	QLabel* label = new QLabel("透明度:");
	QSlider* slider = new QSlider(Qt::Horizontal);
	slider->setRange(0, 100); 
	slider->setValue(100);
	QLabel* valueLabel = new QLabel("100%");
	addRibbonPanelSlider(p1, label, valueLabel, slider,3);
}

void UiFrame::initSARibbon()
{
    //SARibbonBar::initHighDpi();
    setWindowTitle("CAE_BASE");
    setWindowModified(true);
    // 保持整个窗口与 Ribbon 布局无缝贴合
    setContentsMargins(0, 0, 0, 0);

        if (auto mainLayout = layout())
        {
                mainLayout->setContentsMargins(0, 0, 0, 0);
                mainLayout->setSpacing(0);
        }

    m_pSARibbon = ribbonBar();
    m_pSARibbon->setRibbonStyle(SARibbonBar::RibbonStyleTwoRow);
    setRibbonTheme(SARibbonTheme::RibbonThemeOffice2021Blue);

        // 默认布局的边距会在 Ribbon 左侧和底部留下空白，这里清零边距保持贴合
        m_pSARibbon->setContentsMargins(0, 0, 0, 0);
        if (auto layout = m_pSARibbon->layout())
        {
                layout->setContentsMargins(0, 0, 0, 0);
                layout->setSpacing(0);
        }
        buildRibbonAppButton();
    //QTimer::singleShot(0, this, [this]() {setWindowState(windowState() | Qt::WindowMaximized);});
}

void UiFrame::buildRibbonAppButton()
{
	QAbstractButton* btn = m_pSARibbon->applicationButton();
	btn->setText(" &文件 ");
	m_applicationMenu = new SARibbonMenu(this);
	SARibbonApplicationButton* appBtn = qobject_cast<SARibbonApplicationButton*>(btn);
	appBtn->setMenu(m_applicationMenu);
#pragma region 文件标签页
    m_newProAct = new QAction(QIcon(":/resources/menu/NewProject.png"), ("&新建工程"));
    connect(m_newProAct, &QAction::triggered, this, &UiFrame::onNewProjectClicked);

    m_openProAct = new QAction(QIcon(":/resources/menu/OpenProject.png"), ("&打开工程"));
    m_saveProAct = new QAction(QIcon(":/resources/menu/Save.png"), ("&保存工程"));
    m_exportCurDatabaseAct = new QAction(QIcon(":/resources/menu/ExportDB.png"), ("&导出当前数据库"));
    m_exitAct = new QAction(QIcon(":/resources/menu/Exit.png"), ("&退出"));
	addMenuAction(m_newProAct);
	addMenuAction(m_openProAct);
	addMenuAction(m_saveProAct);
	addMenuAction(m_exportCurDatabaseAct);
	addMenuSeparator();
	addMenuAction(m_exitAct);
#pragma endregion 

#pragma region 自定义参数标签页
	const QString paramCateName = "自定义参数";
	const QString paramCateObjName = "paramObj";
	addRibbonCategory(paramCateName, paramCateObjName);
	SARibbonCategory * paramCate = getCategoryByObjName(paramCateObjName);

	const QString paramPanelName = "自定义参数";
    SARibbonPannel* paramPanel = addRibbonPanel(paramCate, paramPanelName);
    m_paramAct = new QAction(QIcon(":/resources/menu/Parameter.png"), ("&参数"));
	addRibbonPanelAction(paramPanel, m_paramAct,3);
    m_funcAct = new QAction(QIcon(":/resources/menu/Function.png"), ("&函数"));
	addRibbonPanelAction(paramPanel, m_funcAct, 3);
#pragma endregion 

#pragma region 几何标签页
	const QString gemoCateName = "几何";
	const QString gemoCateObjName = "gemoObj";
	addRibbonCategory(gemoCateName, gemoCateObjName);
	SARibbonCategory * gemoCate = getCategoryByObjName(gemoCateObjName);

	const QString gemoPanelName = "导入导出";
    SARibbonPannel* gemoPanel = addRibbonPanel(gemoCate, gemoPanelName);
    m_importGemoAct = new QAction(QIcon(":/resources/menu/ImportGeometry.png"), ("&导入"));
	addRibbonPanelAction(gemoPanel, m_importGemoAct, 2);
    m_exportGemoAct = new QAction(QIcon(":/resources/menu/ExportGeometry.png"), ("&导出"));
	addRibbonPanelAction(gemoPanel, m_exportGemoAct, 2);

	const QString gemoPanelName2 = "几何操作";
    SARibbonPannel* gemoPanel2 = addRibbonPanel(gemoCate, gemoPanelName2);
    m_circleGemoAct = new QAction(QIcon(":/resources/menu/Round.png"), ("&圆"));
	addRibbonPanelAction(gemoPanel2, m_circleGemoAct, 1);
    m_squareGemoAct = new QAction(QIcon(":/resources/menu/Square.png"), ("&正方形"));
	addRibbonPanelAction(gemoPanel2, m_squareGemoAct, 1);
    m_ellipseGemoAct = new QAction(QIcon(":/resources/menu/Elipse.png"), ("&椭圆"));
	addRibbonPanelAction(gemoPanel2, m_ellipseGemoAct, 1);
    m_polygonGemoAct = new QAction(QIcon(":/resources/menu/Polygon.png"), ("&多边形"));
	addRibbonPanelAction(gemoPanel2, m_polygonGemoAct, 1);
    m_rectangleGemoAct = new QAction(QIcon(":/resources/menu/Rectangle.png"), ("&矩形"));
	addRibbonPanelAction(gemoPanel2, m_rectangleGemoAct, 1);
    m_pointGemoAct = new QAction(QIcon(":/resources/menu/Point.png"), ("&点"));
	addRibbonPanelAction(gemoPanel2, m_pointGemoAct, 1);
    m_arcGemoAct = new QAction(QIcon(":/resources/menu/ArcCurve.png"), ("&圆弧"));
	const QString arcName = "圆弧";
	QList<QAction*> gemoActionList;
	QList<QString> gemoNameList;
    m_threeBsrCurveAct = new QAction(QIcon(":/resources/menu/Curve1.png"), ("&三次贝塞尔曲线"));
	const QString threeBsrCurveName = "三次贝塞尔曲线";
    m_interCurveAct = new QAction(QIcon(":/resources/menu/Curve2.png"), ("&插值曲线"));
	const QString interCurveName = "插值曲线";
    m_lineCurveAct = new QAction(QIcon(":/resources/menu/LineSegment.png"), ("&线段"));
	const QString lineCurveName = "线段";
    m_paramCurveAct = new QAction(QIcon(":/resources/menu/Curve4.png"), ("&参数化曲线"));
	const QString paramCurveName = "参数化曲线";
    m_twoBsrCurveAct = new QAction(QIcon(":/resources/menu/Curve3.png"), ("&二次贝塞尔曲线"));
	const QString twoBsrCurveName = "二次贝塞尔曲线";
	m_parabolaAct = new QAction(QIcon(":/resources/menu/parabola.png"), ("&抛物线"));
	const QString parabolaName = "抛物线";
	m_hyperbolaAct = new QAction(QIcon(":/resources/menu/hyperbola.png"), ("&双曲线"));
	const QString hyperbolaName = "双曲线";
	gemoActionList << m_threeBsrCurveAct << m_interCurveAct << m_lineCurveAct << m_paramCurveAct << m_twoBsrCurveAct<< m_parabolaAct<< m_hyperbolaAct;
	gemoNameList << threeBsrCurveName << interCurveName << lineCurveName << paramCurveName << twoBsrCurveName<< parabolaName<< hyperbolaName;
	addRibbonPanelCombox(gemoPanel2, m_arcGemoAct, arcName, gemoActionList, gemoNameList,2);
    m_delGemoAct = new QAction(QIcon(":/resources/menu/Delete.png"), ("&删除"));
	addRibbonPanelAction(gemoPanel2, m_delGemoAct, 3);
	m_cuboidGemoAct = new QAction(QIcon(":/resources/menu/cuboid.png"), ("&长方体"));
	addRibbonPanelAction(gemoPanel2, m_cuboidGemoAct, 1);
	m_sphereGemoAct = new QAction(QIcon(":/resources/menu/sphere.png"), ("&球体"));
	addRibbonPanelAction(gemoPanel2, m_sphereGemoAct, 1);
	m_coneGemoAct = new QAction(QIcon(":/resources/menu/cone.png"), ("&圆锥体"));
	addRibbonPanelAction(gemoPanel2, m_coneGemoAct, 1);
	m_ringGemoAct = new QAction(QIcon(":/resources/menu/circleRing.png"), ("&圆环"));
	addRibbonPanelAction(gemoPanel2, m_ringGemoAct, 1);
	m_cylinderGemoAct = new QAction(QIcon(":/resources/menu/cylinder.png"), ("&圆柱体"));
	addRibbonPanelAction(gemoPanel2, m_cylinderGemoAct, 1);
	m_spiralGemoAct = new QAction(QIcon(":/resources/menu/spiral.png"), ("&螺旋"));
	addRibbonPanelAction(gemoPanel2, m_spiralGemoAct, 1);



	const QString gemoPanelName3 = "几何显示";
    SARibbonPannel* gemoPanel3 = addRibbonPanel(gemoCate, gemoPanelName3);
    m_solidViewAct = new QAction(QIcon(":/resources/menu/Square-Yellow.png"), ("&实体显示"));
	addRibbonPanelAction(gemoPanel3, m_solidViewAct, 3);
    m_lineViewAct = new QAction(QIcon(":/resources/menu/LineGraph.png"), ("&线图显示"));
	addRibbonPanelAction(gemoPanel3, m_lineViewAct, 3);
    m_transmissionViewAct = new QAction(QIcon(":/resources/menu/Transmission.png"), ("&透射显示"));
	addRibbonPanelAction(gemoPanel3, m_transmissionViewAct, 3);
	m_showFaceViewAct = new QAction(QIcon(":/resources/menu/showFace.png"), ("&显示面"));
	addRibbonPanelAction(gemoPanel3, m_showFaceViewAct, 3);
	m_showLineVViewAct = new QAction(QIcon(":/resources/menu/showLine.png"), ("&显示线"));
	addRibbonPanelAction(gemoPanel3, m_showLineVViewAct, 2);
	m_showPointViewAct = new QAction(QIcon(":/resources/menu/showPoint.png"), ("&显示点"));
	addRibbonPanelAction(gemoPanel3, m_showPointViewAct, 2);
	
    m_measureViewAct = new QAction(QIcon(":/resources/menu/Measure.png"), ("&测量"));
	addRibbonPanelAction(gemoPanel3, m_measureViewAct, 3);
	m_measurePositionViewAct = new QAction(QIcon(":/resources/menu/positionMeasure.png"), ("&位置测量"));
	addRibbonPanelAction(gemoPanel3, m_measurePositionViewAct, 1);
	m_measureCentroidViewAct = new QAction(QIcon(":/resources/menu/centroidMeasure.png"), ("&质心测量"));
	addRibbonPanelAction(gemoPanel3, m_measureCentroidViewAct, 1);
	m_measureDistanceViewAct = new QAction(QIcon(":/resources/menu/distanceMeasure.png"), ("&距离测量"));
	addRibbonPanelAction(gemoPanel3, m_measureDistanceViewAct, 1);

    const QString gemoPanelName4 = "几何运算";
    SARibbonPannel* gemoPanel4 = addRibbonPanel(gemoCate, gemoPanelName4);

    m_fuseAct = new QAction(QIcon(":/resources/menu/Square-Yellow.png"), ("&Boolen"));
    connect(m_fuseAct, &QAction::triggered, this, &UiFrame::onBoolenClicked);
    addRibbonPanelAction(gemoPanel4, m_fuseAct, 3);
    m_cutAct = new QAction(QIcon(":/resources/menu/Square-Yellow.png"), ("&cut"));
    addRibbonPanelAction(gemoPanel4, m_cutAct, 3);
    m_commonAct = new QAction(QIcon(":/resources/menu/Square-Yellow.png"), ("&common"));
    addRibbonPanelAction(gemoPanel4, m_commonAct, 3);


	QStringList chooseList;
	chooseList << "无" << "点选" << "线选" << "面选" << "域选" << "仅显示"<<"反显示";
	const QString btnChooseWidgetName = "geom choose";
	const QString btnChooseWidgetText = "选择模式";
	const QString labelChooseName = "geom choose";
	const QString labelChooseText = "选择模式";
	const QString comboxChooseName = "choose combox";
	m_chooseGeomCombox = addRibbonPanelBtnWidget(gemoPanel3, btnChooseWidgetText, btnChooseWidgetText, labelChooseText, labelChooseName, comboxChooseName, chooseList, 3);
#pragma endregion

#pragma region 网格标签页
	const QString gridCateName = "网格";
	const QString gridCateObjName = "gridObj";
	addRibbonCategory(gridCateName, gridCateObjName);
	SARibbonCategory * gridCate = getCategoryByObjName(gridCateObjName);
	const QString gridPanelName = "导入导出";
    SARibbonPannel* gridPanel = addRibbonPanel(gridCate, gridPanelName);
    m_importGridAct = new QAction(QIcon(":/resources/menu/importGrid.png"), ("&导入"));
	addRibbonPanelAction(gridPanel, m_importGridAct, 2);
    m_exportGridAct = new QAction(QIcon(":/resources/menu/exportGrid.png"), ("&导出"));
	addRibbonPanelAction(gridPanel, m_exportGridAct, 2);
	const QString gridPanelName2 = "网格操作";
    SARibbonPannel* gridPanel2 = addRibbonPanel(gridCate, gridPanelName2);
    /*m_triangleGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&自由三角形网格"));
	addRibbonPanelAction(gridPanel2, m_triangleGridAct, 3);
    m_quadGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&自由四边形网格"));
	addRibbonPanelAction(gridPanel2, m_quadGridAct, 3);
	m_tetrahedronGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&自由四面体网格"));
	addRibbonPanelAction(gridPanel2, m_tetrahedronGridAct, 3);
	m_hexahedronGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&六面体网格"));
	addRibbonPanelAction(gridPanel2, m_hexahedronGridAct, 3);*/
	m_curvedSurfaceGridAct = new QAction(QIcon(":/resources/menu/surfaceMesh.png"), ("&二维曲面网格"));
    addRibbonPanelAction(gridPanel2, m_curvedSurfaceGridAct, 1);
	m_entityGridAct = new QAction(QIcon(":/resources/menu/solidMesh.png"), ("&三维实体网格"));
	addRibbonPanelAction(gridPanel2, m_entityGridAct, 1);
    m_resizeGemoAct = new QAction(QIcon(":/resources/menu/resizeGridSize.png"), ("&调整大小"));
	const QString resizeName = "调整大小";
	QList<QAction*> gridActionList;
	QList<QString> gridNameList;

	m_sizeGridAct = new QAction(QIcon(":/resources/menu/gridSize.png"), ("&大小"));
	const QString sizeGridName = "大小";
    m_jxGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&极细"));
	const QString jxGridName = "极细";
    m_cxGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&超细"));
	const QString cxGridName = "超细";
    m_gxGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&更细"));
	const QString gxGridName = "更细";
    m_xhGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&细化"));
	const QString xhGridName = "细化";
    m_cgGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&常规"));
	const QString cgGridName = "常规";
    m_chGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&粗化"));
	const QString chGridName = "粗化";
    m_gcGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&更粗"));
	const QString gcGridName = "更粗";
    m_ccGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&超粗"));
	const QString ccGridName = "超粗";
    m_jcGridAct = new QAction(QIcon(":Resource/Ribbon/new.png"), ("&极粗"));
	const QString jcGridName = "极粗";
	/*gridActionList << m_jxGridAct << m_cxGridAct
		<< m_gxGridAct << m_xhGridAct << m_cgGridAct
		<< m_chGridAct << m_gcGridAct << m_ccGridAct << m_jcGridAct;
	gridNameList << jxGridName << cxGridName
		<< gxGridName << xhGridName << cgGridName
		<< chGridName << gcGridName << ccGridName << jcGridName;*/
	gridActionList << m_sizeGridAct;
	gridNameList << sizeGridName;
	addRibbonPanelCombox(gridPanel2, m_resizeGemoAct, resizeName, gridActionList, gridNameList, 1);
	m_showGridAct = new QAction(QIcon(":/resources/menu/showGrid.png"), ("&网格显示"));
	addRibbonPanelAction(gridPanel2, m_showGridAct, 3);
	m_drawGridAct = new QAction(QIcon(":/resources/menu/drawGrid.png"), ("&绘制"));
	addRibbonPanelAction(gridPanel2, m_drawGridAct, 3);
    m_resetGridAct = new QAction(QIcon(":/resources/menu/resetGrid.png"), ("&重置 "));
	addRibbonPanelAction(gridPanel2, m_resetGridAct, 2);
    m_countInfoGridAct = new QAction(QIcon(":/resources/menu/statisticsInfo.png"), ("&统计信息"));
	addRibbonPanelAction(gridPanel2, m_countInfoGridAct, 2);
#pragma endregion

#pragma region 边界条件标签页
	const QString boundCateName = "边界条件";
	const QString boundCateObjName = "boundObj";
	addRibbonCategory(boundCateName, boundCateObjName);
	SARibbonCategory * boundCate = getCategoryByObjName(boundCateObjName);
	const QString boundPanelName = "边界条件";
    SARibbonPannel* boundPanel = addRibbonPanel(boundCate, boundPanelName);
    m_solidBoundAct = new QAction(QIcon(":/resources/menu/boundary_solid.png"), ("&域"));
	addRibbonPanelAction(boundPanel, m_solidBoundAct, 3);
    m_faceBoundAct = new QAction(QIcon(":/resources/menu/boundary_face.png"), ("&边界"));
	addRibbonPanelAction(boundPanel, m_faceBoundAct, 3);
    m_lineBoundAct = new QAction(QIcon(":/resources/menu/boundary_line.png"), ("&边"));
	addRibbonPanelAction(boundPanel, m_lineBoundAct, 3);
    m_pointBoundAct = new QAction(QIcon(":/resources/menu/boundary_point.png"), ("&点"));
	addRibbonPanelAction(boundPanel, m_pointBoundAct, 3);
	connect(m_solidBoundAct, &QAction::triggered, this, [this]() {EventManager::instance().sendEvent(PublicConstants::kActionBoundarySolid); });
	connect(m_faceBoundAct, &QAction::triggered, this, [this]() {EventManager::instance().sendEvent(PublicConstants::kActionBoundaryFace); });
	connect(m_lineBoundAct, &QAction::triggered, this, [this]() {EventManager::instance().sendEvent(PublicConstants::kActionBoundaryLine); });
	connect(m_pointBoundAct, &QAction::triggered, this, [this]() {EventManager::instance().sendEvent(PublicConstants::kActionBoundaryPoint); });
#pragma endregion 

#pragma region 结果标签页
	const QString resultCateName = "结果";
	const QString resultCateObjName = "resultObj";
	addRibbonCategory(resultCateName, resultCateObjName);
	SARibbonCategory * resultCate = getCategoryByObjName(resultCateObjName);
	const QString resultPanelName = "后处理操作";
    SARibbonPannel* resultPanel = addRibbonPanel(resultCate, resultPanelName);
    m_nephogramResultAct = new QAction(QIcon(":/resources/menu/Nephogram.png"), ("&云图"));
    connect(m_nephogramResultAct, &QAction::triggered, this, &UiFrame::onNephogramClicked);
	addRibbonPanelAction(resultPanel, m_nephogramResultAct, 2);

    m_vectorsResultAct = new QAction(QIcon(":/resources/menu/Vector.png"), ("&矢量"));
    connect(m_vectorsResultAct, &QAction::triggered, this, &UiFrame::onVectorsClicked);
	addRibbonPanelAction(resultPanel, m_vectorsResultAct, 2);

    m_sectionResultAct = new QAction(QIcon(":/resources/menu/Slice.png"), ("&切面"));
    connect(m_sectionResultAct, &QAction::triggered, this, &UiFrame::onSectionClicked);
	addRibbonPanelAction(resultPanel, m_sectionResultAct, 2);

    m_diceResultAct = new QAction(QIcon(":/resources/menu/Block.png"), ("&切块"));
    connect(m_diceResultAct, &QAction::triggered, this, &UiFrame::onDiceResultClicked);
	addRibbonPanelAction(resultPanel, m_diceResultAct, 2);

    m_isosurfacesResultAct = new QAction(QIcon(":/resources/menu/Isosurface.png"), ("&等值面"));
    connect(m_isosurfacesResultAct, &QAction::triggered, this, &UiFrame::onIsosurfacesResultClicked);
	addRibbonPanelAction(resultPanel, m_isosurfacesResultAct, 2);

    m_isosurfacesSegResultAct = new QAction(QIcon(":/resources/menu/IsosurfaceSegmentation.png"), ("&等值面分割"));
    connect(m_isosurfacesSegResultAct, &QAction::triggered, this, &UiFrame::onIsosurfacesSegClicked);
    addRibbonPanelAction(resultPanel, m_isosurfacesSegResultAct, 2);

    m_xyPlotResultAct = new QAction(QIcon(":/resources/menu/Curve.png"), ("&曲线"));
    connect(m_xyPlotResultAct, &QAction::triggered, this, &UiFrame::onXYPlotResultClicked);
	addRibbonPanelAction(resultPanel, m_xyPlotResultAct, 3);

    m_animationResultAct = new QAction(QIcon(":/resources/menu/Animation.png"), ("&动画"));
    connect(m_animationResultAct, &QAction::triggered, this, &UiFrame::onAnimationResultClicked);
	addRibbonPanelAction(resultPanel, m_animationResultAct, 3);

    m_pickPhysicalResultAct = new QAction(QIcon(":/resources/menu/PickUp.png"), ("&拾取物理量"));
    connect(m_pickPhysicalResultAct, &QAction::triggered, this, &UiFrame::onPickPhysicalResultClicked);
	addRibbonPanelAction(resultPanel, m_pickPhysicalResultAct, 3);

    m_derivativeResultAct = new QAction(QIcon(":/resources/menu/DerivedValue2.png"), ("&派生值"));
    connect(m_derivativeResultAct, &QAction::triggered, this, &UiFrame::onDerivativeResultClicked);
	const QString derivativeName = "派生值";
	QList<QAction*> resActionList;
	QList<QString> resNameList;
    m_maxResultAct = new QAction(QIcon(":/resources/menu/MaximumValue.png"), ("&最大值"));
	const QString m_maxResultName = "最大值";
    m_minResultAct = new QAction(QIcon(":/resources/menu/MinimumValue.png"), ("&最小值"));
	const QString m_minResultName = "最小值";
    m_averResultAct = new QAction(QIcon(":/resources/menu/Vector.png"), ("&均值"));
	const QString m_averResultName = "均值";
	resActionList << m_maxResultAct << m_minResultAct << m_paramCurveAct;
	resNameList << m_maxResultName << m_minResultName << m_averResultName;
	addRibbonPanelCombox(resultPanel, m_derivativeResultAct, derivativeName, resActionList, resNameList, 2);
    m_dataExpResultAct = new QAction(QIcon(":/resources/menu/Vector.png"), ("&数据导出"));
    connect(m_dataExpResultAct, &QAction::triggered, this, &UiFrame::onDataExpResultClicked);
	addRibbonPanelAction(resultPanel, m_dataExpResultAct, 2);
#pragma endregion 

#pragma region 显示标签页
	const QString viewCateName = "显示";
	const QString viewCateObjName = "viewObj";
	addRibbonCategory(viewCateName, viewCateObjName);
	SARibbonCategory * viewCate = getCategoryByObjName(viewCateObjName);
	const QString viewPanelName = "显示";
    SARibbonPannel* viewPanel = addRibbonPanel(viewCate, viewPanelName);

	m_x1ViewAct = new QAction(QIcon(":/resources/icon/view_right.png"), ("&+X"));
	addRibbonPanelAction(viewPanel, m_x1ViewAct, 2);
	m_x2ViewAct = new QAction(QIcon(":/resources/icon/view_left.png"), ("&-X"));
	addRibbonPanelAction(viewPanel, m_x2ViewAct, 2);
	m_y1ViewAct = new QAction(QIcon(":/resources/icon/view_top.png"), ("&+Y"));
	addRibbonPanelAction(viewPanel, m_y1ViewAct, 2);
	m_y2ViewAct = new QAction(QIcon(":/resources/icon/view_bottom.png"), ("&-Y"));
	addRibbonPanelAction(viewPanel, m_y2ViewAct, 2);
	m_z1ViewAct = new QAction(QIcon(":/resources/icon/view_front.png"), ("&+Z"));
	addRibbonPanelAction(viewPanel, m_z1ViewAct, 2);
	m_z2ViewAct = new QAction(QIcon(":/resources/icon/view_back.png"), ("&-Z"));
	addRibbonPanelAction(viewPanel, m_z2ViewAct, 2);
	m_resetCameraViewAct = new QAction(QIcon(":/resources/icon/autofit.png"), ("&重置相机"));
	addRibbonPanelAction(viewPanel, m_resetCameraViewAct, 3);
	m_resetWidgetViewAct = new QAction(QIcon(":/resources/icon/reset_default_setting.png"), ("&恢复默认布局"));
	addRibbonPanelAction(viewPanel, m_resetWidgetViewAct, 3);
	connect(m_resetWidgetViewAct, &QAction::triggered, this, [this]() {this->onResetWidgetClicked();});
	const QString staffText = "标尺显示";
	const QString staffObjName = "staffObj";
	m_staffCheckBox = addRibbonPanelCheckBox(viewPanel, staffText, staffObjName, 1);
	m_staffCheckBox->setChecked(true);
	const QString lightingText = "光照";
	const QString lightingObjName = "lightingObj";
	m_lightCheckBox = addRibbonPanelCheckBox(viewPanel, lightingText, lightingObjName, 1);
	m_lightCheckBox->setChecked(false);
	QLabel* label = new QLabel("透明度:");
	transparencySlider = new QSlider(Qt::Horizontal);
	transparencySlider->setRange(1, 100);
	transparencySlider->setValue(100);
	transparencyLabel = new QLabel("100");
	addRibbonPanelSlider(viewPanel, label, transparencyLabel, transparencySlider, 1);
	connect(transparencySlider, &QSlider::valueChanged, this, &UiFrame::updateTransparencyLabel);
    
	QStringList themeList;
	themeList << "亮色主题" << "暗色主题";
	const QString btnWidgetName = "color theme";
	const QString btnWidgetText = "颜色主题";
	const QString labelName = "color theme";
	const QString labelText = "颜色主题";
	const QString comboxName = "color combox";
	m_colorThemeCombox = addRibbonPanelBtnWidget(viewPanel, btnWidgetText, btnWidgetText, labelText, labelName, comboxName, themeList,3);
	connect(m_colorThemeCombox, &QComboBox::currentTextChanged, this, [this](const QString & text) {this->onColorThemeChanged(text); });
#pragma endregion 

#pragma region 代理模型标签页
	const QString modelCateName = "代理模型";
	const QString modelCateObjName = "modelObj";
	addRibbonCategory(modelCateName, modelCateObjName);
	SARibbonCategory * modelCate = getCategoryByObjName(modelCateObjName);
	const QString modelPanelName = "代理模型";
    SARibbonPannel* modelPanel = addRibbonPanel(modelCate, modelPanelName);
    m_dataSamplingModelAct = new QAction(QIcon(":/resources/menu/DataSampling.png"), ("&数据采样"));
	const QString dataSamplingName = "数据采样";
	QList<QAction*> modelActionList;
	QList<QString> modelNameList;
    m_zjSamplingModelAct = new QAction(QIcon(":/resources/menu/OrthogonalSampling.png"), ("&正交采样"));
    connect(m_zjSamplingModelAct, &QAction::triggered, this, &UiFrame::onZJSamplingModelClicked);
	const QString m_zjSamplingModelName = "正交采样";

    m_ldclfSamplingModelAct = new QAction(QIcon(":/resources/menu/LatinHypercubeSampling.png"), ("&拉丁超立方采样"));
    connect(m_ldclfSamplingModelAct, &QAction::triggered, this, &UiFrame::onLdclfSamplingModelClicked);
	const QString m_ldclfSamplingModelName = "拉丁超立方采样";

	modelActionList << m_zjSamplingModelAct << m_ldclfSamplingModelAct;
	modelNameList << m_zjSamplingModelName << m_ldclfSamplingModelName;
	addRibbonPanelCombox(modelPanel, m_dataSamplingModelAct, dataSamplingName, modelActionList, modelNameList, 3);
    m_dataEvalModelAct = new QAction(QIcon(":/resources/menu/DatasetEvaluation.png"), ("&数据集评估"));
    connect(m_dataEvalModelAct, &QAction::triggered, this, &UiFrame::onDataEvalModelClicked);
	addRibbonPanelAction(modelPanel, m_dataEvalModelAct, 3);
    m_dnnModelAct = new QAction(QIcon(":/resources/menu/DNN.png"), ("&DNN"));
    connect(m_dnnModelAct, &QAction::triggered, this, &UiFrame::onDNNModelClicked);
	addRibbonPanelAction(modelPanel, m_dnnModelAct, 2);
    m_podModelAct = new QAction(QIcon(":/resources/menu/POD.png"), ("&POD"));
    connect(m_podModelAct, &QAction::triggered, this, &UiFrame::onPODModelClicked);
	addRibbonPanelAction(modelPanel, m_podModelAct, 2);
#pragma endregion 

}


void UiFrame::addMenuAction(QAction* action)
{
	m_applicationMenu->addAction(action);
}

void UiFrame::addMenuSeparator()
{
	m_applicationMenu->addSeparator();
}

void UiFrame::addRibbonCategory(const QString& catName, const QString& objName)
{
	SARibbonCategory* categoryMain = m_pSARibbon->addCategoryPage(catName);
	categoryMain->setObjectName(objName);
	m_mRibbonCategoryMap.insert(objName, categoryMain);
}

SARibbonCategory * UiFrame::getCategoryByObjName(const QString & objName)
{
	if (m_mRibbonCategoryMap.contains(objName))
	{
		return m_mRibbonCategoryMap[objName];
	}
	return nullptr;
}

SARibbonPannel* UiFrame::addRibbonPanel(SARibbonCategory * cate, const QString& name)
{
	if (!cate) return nullptr;
    SARibbonPannel* pannel = cate->addPannel(name);
	if (!m_mRibbonPannelMap.contains(cate))
	{
        QList<SARibbonPannel*> _list;
		_list.append(pannel);
		m_mRibbonPannelMap.insert(cate,_list);
	}
	else
	{
		m_mRibbonPannelMap[cate].append(pannel);
	}
	return pannel;
}

void UiFrame::addRibbonPanelCombox(SARibbonPannel * panel, QAction* mainAction,
	const QString & mainActionName, QList<QAction*> actions, const QList<QString> objNames, int actionType)
{
	if (!panel) return;
	if (actions.size() != objNames.size()) return;
	SARibbonMenu* menu = new SARibbonMenu(this);
	for (size_t i = 0; i < actions.length(); i++)
	{
		actions[i]->setObjectName(objNames[i]);
	}
	menu->addActions(actions);
	QVariant actionData(mainActionName);
	mainAction->setData(actionData);
	mainAction->setMenu(menu);
	switch (actionType)
	{
	case 1:
		panel->addSmallAction(mainAction, QToolButton::MenuButtonPopup);
		break;
	case 2:
		panel->addMediumAction(mainAction, QToolButton::MenuButtonPopup);
		break;
	case 3:
		panel->addLargeAction(mainAction, QToolButton::MenuButtonPopup);
		break;
	default:
		break;
	}
}

QCheckBox* UiFrame::addRibbonPanelCheckBox(SARibbonPannel * panel, const QString & text, const QString & objName, int actionType)
{
	if (!panel) return new QCheckBox() ;
    SARibbonCheckBox* checkBox = new SARibbonCheckBox(this);
	checkBox->setText(text);
	checkBox->setObjectName(objName);
	checkBox->setWindowTitle(checkBox->text());
	switch (actionType)
	{
	case 1:
		panel->addSmallWidget(checkBox);
		break;
	case 2:
		panel->addMediumWidget(checkBox);
		break;
	case 3:
		panel->addLargeWidget(checkBox);
		break;
	default:
		break;
	}
	return checkBox;
}

QComboBox* UiFrame::addRibbonPanelBtnWidget(SARibbonPannel * panel, const QString & btnWidgetText,const QString & btnWidgetObjName,
	const QString & labelText, const QString & labelObjName, 
	const QString & comboxObjName, const QStringList& comboxNames , int actionType)
{
	if (!panel) return new QComboBox();
	SARibbonButtonGroupWidget* groupWidget = new SARibbonButtonGroupWidget(panel);
	groupWidget->setMinimumWidth(170);
	groupWidget->setContentsMargins(0, 0, 0, 0);
	groupWidget->setObjectName(btnWidgetObjName);
	QLabel* label = new QLabel(groupWidget);
	label->setText(labelText);
	label->setObjectName(labelObjName);
	groupWidget->addWidget(label);
	SARibbonComboBox* combox = new SARibbonComboBox(groupWidget);
	combox->setAutoFillBackground(true);
	combox->setObjectName(comboxObjName);
	combox->addItems(comboxNames);
	combox->setMinimumWidth(100);
	groupWidget->setWindowTitle(btnWidgetText);
	groupWidget->addWidget(combox);
	switch (actionType)
	{
	case 1:
        panel->addWidget(groupWidget, SARibbonPannelItem::Small);
		break;
	case 2:
        panel->addWidget(groupWidget, SARibbonPannelItem::Medium);
		break;
	case 3:
        panel->addWidget(groupWidget, SARibbonPannelItem::Large);
		break;
	default:
		break;
	}
	return combox;
}

bool UiFrame::addRibbonPanelAction(SARibbonPannel * panel, QAction * action, int actionType)
{
	if (!panel|| !action) return false;
	switch (actionType)
	{
	case 1:
		panel->addSmallAction(action);
		break;
	case 2:
		panel->addMediumAction(action);
		break;
	case 3:
		panel->addLargeAction(action);
		break;
	default:
		break;
	}
	if (!m_mRibbonPannelActions.contains(panel))
	{
		QList<QAction*> _list;
		_list.append(action);
		m_mRibbonPannelActions.insert(panel, _list);
	}
	else
	{
		m_mRibbonPannelActions[panel].append(action);
	}
	return true;
}

void UiFrame::addRibbonPanelSlider(SARibbonPannel * panel, QLabel * label1, QLabel * label2, QSlider * slider, int actionType)
{
	if (!panel)return;
	QWidget* container = new QWidget();
	QHBoxLayout* layout = new QHBoxLayout(container);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(5); 
	layout->addWidget(label1);
	layout->addWidget(slider);
	layout->addWidget(label2);
	slider->setFixedWidth(120);
	label2->setFixedWidth(40);
	switch (actionType)
	{
	case 1:
        panel->addWidget(container, SARibbonPannelItem::Small);
		break;
	case 2:
        panel->addWidget(container, SARibbonPannelItem::Medium);
		break;
	case 3:
        panel->addWidget(container, SARibbonPannelItem::Large);
		break;
	default:
		break;
	}
	
}

QList<QAction*> UiFrame::getPanelAllAcition(SARibbonPannel * panel)
{
	if (!m_mRibbonPannelActions.contains(panel)) return QList<QAction*>();
	return m_mRibbonPannelActions[panel];
}

QList<SARibbonPannel*> UiFrame::getCategoryAllPanel(SARibbonCategory * cate)
{
    if (!m_mRibbonPannelMap.contains(cate)) return QList<SARibbonPannel*>();
	return m_mRibbonPannelMap[cate];
}

void UiFrame::setRibbonPanelEnabled(SARibbonPannel * panel, bool isEnable)
{
	if (!panel)return;
	panel->setEnabled(isEnable);
}

void UiFrame::setRibbonPanelVisible(SARibbonPannel * panel, bool isVisible)
{
	if (!panel)return;
	panel->setVisible(isVisible);
}

void UiFrame::setRibbonCategoryEnabled(SARibbonCategory * cate, bool isEnable)
{
	if (!cate)return;
	cate->setEnabled(isEnable);
}

void UiFrame::setRibbonCategoryVisible(SARibbonCategory * cate, bool isVisible)
{
	if (!cate)return;
	cate->setVisible(isVisible);
}

void UiFrame::onNewProjectClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionNewProject);
}

void UiFrame::onNephogramClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessNephogram);
}

void UiFrame::onVectorsClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessVector);
}

void UiFrame::onSectionClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessSection);
}

void UiFrame::onDiceResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessDice);
}

void UiFrame::onIsosurfacesResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessIsosurface);
}

void UiFrame::onIsosurfacesSegClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessIsosurfaceSegmentation);
}

void UiFrame::onXYPlotResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessXyPlot);
}

void UiFrame::onAnimationResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessAnimation);
}

void UiFrame::onPickPhysicalResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessPickPhysical);
}

void UiFrame::onDerivativeResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessDerivative);
}

void UiFrame::onDataExpResultClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessDataExport);
}

void UiFrame::onZJSamplingModelClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionSurrogateModelOrthogonal);
}

void UiFrame::onLdclfSamplingModelClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionSurrogateModelLatin);
}

void UiFrame::onDataEvalModelClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionSurrogateModelDataEvaluation);
}

void UiFrame::onDNNModelClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionSurrogateModelDnn);
}

void UiFrame::onPODModelClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionSurrogateModelPod);
}

void UiFrame::updateTransparencyLabel(int value)
{
	transparencyLabel->setText(QString::number(value));
}

void UiFrame::onResetWidgetClicked()
{
	if (m_dockManager && !m_defaultDockState.isEmpty())
	{
		m_dockManager->restoreState(m_defaultDockState);
	}
}


void UiFrame::onBoolenClicked()
{
    EventManager::instance().sendEvent(PublicConstants::kActionPostProcessOccBoolean);
}
void UiFrame::onColorThemeChanged(const QString & text)
{
	if (text == "亮色主题")
	{
	    return;
	}
	else if (text == "暗色主题")
	{
	    return;
	}
	else
	{
	    return;
	}
}
