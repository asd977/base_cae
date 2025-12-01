#include "PreProcessingWidget.h"
#include "ui_PreProcessingWidget.h"
#include <vtkAutoInit.h>
#include <vtkRenderWindow.h>
#include <vtkLight.h>
#include <vtkLightCollection.h>
#include <OccShapeToVtk.h>

VTK_MODULE_INIT(vtkInteractionStyle);
VTK_MODULE_INIT(vtkRenderingFreeType);
#if VTK_MAJOR_VERSION > 7
#include <OccBooleanToolWidget.h>
#include <QVTKOpenGLNativeWidget.h>

#include <mesh/BoundaryConditions.h>
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkRenderingVolumeOpenGL2);
#else
#include <QVTKWidget2.h>
VTK_MODULE_INIT(vtkRenderingOpenGL);
VTK_MODULE_INIT(vtkRenderingVolumeOpenGL);
#endif

PreProcessingWidget::PreProcessingWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PreProcessingWidget)
{
    ui->setupUi(this);
	m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
	m_outputService = ServiceRegistry::instance().getService<OutputService>();
	m_projectService = ServiceRegistry::instance().getService<ProjectManagerService>();
    m_renderer = vtkSmartPointer<vtkRenderer>::New();
	//m_projectService->addNodeToProjectTree(item, "666", "888", "");
    m_renderer->SetBackground(231.0 / 255.0, 242.0 / 255.0, 255.0 / 255.0);
    m_renderer->SetBackground2(1.0,1.0,1.0);
    m_renderer->SetGradientBackground(true);

    vtkSmartPointer<vtkRenderWindowInteractor> getInteractor();

    m_renderWindow =  ui->vtkWidget->renderWindow();
    m_renderWindow->AddRenderer(m_renderer);

    m_renderWindowInteractor = ui->vtkWidget->interactor();
    m_renderWindowInteractor->SetRenderWindow(m_renderWindow);

	m_picker = vtkSmartPointer<GemoPickerInteractorStyle>::New();
	m_renderWindowInteractor->SetInteractorStyle(m_picker);
	m_picker->SetDefaultRenderer(m_renderer);
	connect(m_picker, SIGNAL(selectGemoPoint(vtkActor *, int, int)), this, SLOT(onSelectGemoPoint(vtkActor *, int, int)));
	connect(m_picker, SIGNAL(selectGemoLine(vtkActor *, int, int)), this, SLOT(onSelectGemoLine(vtkActor *, int, int)));
	connect(m_picker, SIGNAL(selectGemoFace(vtkActor *, int, int)), this, SLOT(onSelectGemoFace(vtkActor *, int, int)));
	connect(m_picker, SIGNAL(selectGemoSolid(vtkActor *, int, int)), this, SLOT(onSelectGemoSolid(vtkActor *, int, int)));
    QToolBar *m_toolBar = new QToolBar(ui->vtkWidget);
    m_toolBar->setOrientation(Qt::Vertical);
    m_toolBar->setIconSize(QSize(30, 30));
    m_toolBar->setAllowedAreas(Qt::LeftToolBarArea);
    m_fitView = new QAction(QIcon(":/resources/icon/autofit.png"), tr("FitView"), m_toolBar);
    m_XplusView = new QAction(QIcon(":/resources/icon/view_right.png"), tr("X+"), m_toolBar);
    m_XminView = new QAction(QIcon(":/resources/icon/view_left.png"), tr("X-"), m_toolBar);
    m_YplusView = new QAction(QIcon(":/resources/icon/view_top.png"), tr("Y+"), m_toolBar);
    m_YminView = new QAction(QIcon(":/resources/icon/view_bottom.png"), tr("Y-"), m_toolBar);
    m_ZplusView = new QAction(QIcon(":/resources/icon/view_front.png"), tr("Z+"), m_toolBar);
    m_ZminView = new QAction(QIcon(":/resources/icon/view_back.png"), tr("Z-"), m_toolBar);
    m_perspective = new QAction(QIcon(":/resources/icon/perspective.png"), tr("Perspective"), m_toolBar);
	connect(m_fitView, &QAction::triggered, this, [=]() {this->setView(ViewType::FitView); m_renderer->Render(); });
	connect(m_XplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::XPlusView); m_renderer->Render(); });
	connect(m_XminView, &QAction::triggered, this, [=]() {this->setView(ViewType::XMinusView); m_renderer->Render(); });
	connect(m_YplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::YPlusView); m_renderer->Render(); });
	connect(m_YminView, &QAction::triggered, this, [=]() {this->setView(ViewType::YMinusView); m_renderer->Render(); });
	connect(m_ZplusView, &QAction::triggered, this, [=]() {this->setView(ViewType::ZPlusView); m_renderer->Render(); });
	connect(m_ZminView, &QAction::triggered, this, [=]() {this->setView(ViewType::ZMinusView); m_renderer->Render(); });
	connect(m_perspective, &QAction::triggered, [=] {
		auto camera = m_renderer->GetActiveCamera();
		if (camera->GetParallelProjection())
		{
			camera->ParallelProjectionOff();
			m_perspective->setText(tr("Parallel"));
			m_perspective->setIcon(QIcon(":/resources/icon/parallel.png"));
		}
		else
		{
			camera->ParallelProjectionOn();
			m_perspective->setText(tr("Perspective"));
			m_perspective->setIcon(QIcon(":/resources/icon/perspective.png"));
		}
		m_renderWindow->Render(); });

    m_toolBar->addActions(this->getViewActions());
	initRibbonActionConn();
	createOrientationMarkerWidget();
}

PreProcessingWidget::~PreProcessingWidget()
{
    delete ui;
}

QList<QAction *> PreProcessingWidget::getViewActions() const
{
    return { m_fitView, m_XplusView, m_XminView, m_YplusView, m_YminView, m_ZplusView, m_ZminView , m_perspective,m_configView };
}

void PreProcessingWidget::initRibbonActionConn()
{
#pragma region 几何的action槽函数
	connect(m_uiFrameService->getImportGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryImportWidget(); });
	connect(m_uiFrameService->getCuboidGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryBoxWidget(); });
	connect(m_uiFrameService->getSphereGemoAct(), &QAction::triggered, this, [this]() {this->newGeometrySphereWidget(); });
	connect(m_uiFrameService->getConeGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryConeWidget(); });
	connect(m_uiFrameService->getRingGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryTorusWidget(); });
	connect(m_uiFrameService->getCylinderGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryCylinderWidget(); });
	connect(m_uiFrameService->getSpiralGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryHelixWidget(); });
	connect(m_uiFrameService->getCircleGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryCircleWidget(); });
	connect(m_uiFrameService->getSquareGemoAct(), &QAction::triggered, this, [this]() {this->newGeometrySquareWidget(); });
	connect(m_uiFrameService->getEllipseGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryEllipseWidget(); });
	connect(m_uiFrameService->getPolygonGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryPolygonWidget(); });
	connect(m_uiFrameService->getRectangleGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryRectangleWidget(); });
	connect(m_uiFrameService->getPointGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryPointWidget(); });
	connect(m_uiFrameService->getArcGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryArcWidget(); });
	connect(m_uiFrameService->getThreeBsrCurveAct(), &QAction::triggered, this, [this]() {this->newGeometryThreeBsrWidget(); });
	connect(m_uiFrameService->getTwoBsrCurveActt(), &QAction::triggered, this, [this]() {this->newGeometryTwoBsrWidget(); });
	connect(m_uiFrameService->getLineGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryLineWidget(); });
	connect(m_uiFrameService->getInerCurveGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryInterCurveWidget(); });
	connect(m_uiFrameService->getParamCurveGemoAct(), &QAction::triggered, this, [this]() {this->newGeometryParamCurveWidget(); });
	connect(m_uiFrameService->getParabolaAct(), &QAction::triggered, this, [this]() {this->newGeometryParabolaEdgeWidget(); });
	connect(m_uiFrameService->getHyperbolaAct(), &QAction::triggered, this, [this]() {this->newGeometryHyperbolaWidget(); });

	connect(m_uiFrameService->getExportGemoAc(), &QAction::triggered, this, [this]() {this->exportGeom(); });
	connect(m_uiFrameService->getSolidViewActt(), &QAction::triggered, this, [this]() {this->solidViewActor(); });
	connect(m_uiFrameService->getLineViewAct(), &QAction::triggered, this, [this]() {this->lineViewActor(); });
	connect(m_uiFrameService->getTransmissionViewAct(), &QAction::triggered, this, [this]() {this->transmissionViewActor(); });
	connect(m_uiFrameService->getDelGemoAct(), &QAction::triggered, this, [this]() {this->deleteCreateGemo(); });

	connect(m_uiFrameService->getShowFaceViewAct(), &QAction::triggered, this, [this]() {this->onShowFace(); });
	connect(m_uiFrameService->getShowLineViewAct(), &QAction::triggered, this, [this]() {this->onShowLine(); });
	connect(m_uiFrameService->getShowPointViewAct(), &QAction::triggered, this, [this]() {this->onShowPoint(); });
	connect(m_uiFrameService->getMeasureViewAct(), &QAction::triggered, this, [this]() {this->onMeasureView(); });
	connect(m_uiFrameService->getMeasurePositionViewAct(), &QAction::triggered, this, [this]() {this->onMeasurePositionView(); });
	connect(m_uiFrameService->getMeasureCentroidViewAct(), &QAction::triggered, this, [this]() {this->onMeasureCentroidView(); });
	connect(m_uiFrameService->getMeasureDistanceViewAct(), &QAction::triggered, this, [this]() {this->onMeasureDistanceView(); });
	connect(m_uiFrameService->getChooseGeomCombox(),QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this](int index) { this->onSetSelectType(index); });
#pragma endregion 
#pragma region 网格的action槽函数
	connect(m_uiFrameService->getDrawGridAct(), &QAction::triggered, this, [this]() {this->drawGrid(); });
	connect(m_uiFrameService->getCountInfoGridAct(), &QAction::triggered, this, [this]() {this->countGridInfo(); });
	connect(m_uiFrameService->getImportGridAct(), &QAction::triggered, this, [this]() {this->newGridImportWidget(); });
	connect(m_uiFrameService->getExportGridAct(), &QAction::triggered, this, [this]() {this->exportGrid(); });
	connect(m_uiFrameService->getShowGridAct(), &QAction::triggered, this, [this]() {this->gridViewActor(); });
	connect(m_uiFrameService->getCurvedSurfaceGridAct(), &QAction::triggered, this, [this]() {this->newTetrahedronGridWidget(); });
	connect(m_uiFrameService->getEntityGridAct(), &QAction::triggered, this, [this]() {this->newHexahedronGridWidget(); });
#pragma endregion 
#pragma region 显示的action槽函数
	connect(m_uiFrameService->getX1ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::XPlusView); m_renderer->Render(); });
	connect(m_uiFrameService->getX2ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::XMinusView); m_renderer->Render(); });
	connect(m_uiFrameService->getY1ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::YPlusView); m_renderer->Render(); });
	connect(m_uiFrameService->getY2ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::YMinusView); m_renderer->Render(); });
	connect(m_uiFrameService->getZ1ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::ZPlusView); m_renderer->Render(); });
	connect(m_uiFrameService->getZ2ViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::ZMinusView); m_renderer->Render(); });
	connect(m_uiFrameService->getResetViewAct(), &QAction::triggered, this, [this]() {this->setView(ViewType::FitView); m_renderer->Render(); });
	connect(m_uiFrameService->getLightCheckViewAct(), &QCheckBox::stateChanged, this, [this](int state) {this->onLightCheckBoxStateChanged(state); });
	connect(m_uiFrameService->getTransSliderAct(), &QSlider::valueChanged, this, [this](int value) {this->onTransValueChanged(value); });
#pragma endregion


    EventManager::instance().registerHandler(PublicConstants::ACTION_POSTPROCESS_OCCBOOLEN, [this](const QVariantMap &data) {
        this->newOccBoolenToolWidget();
    });

    const char PAGE_PREPROCESS_OCCBOOLEN[] = "occboolen";
#pragma region 接收边界条件模块传输的信息
EventManager::instance().registerHandler(PublicConstants::SHOW_GEOM_SOLID_MODEL, [this](const QVariantMap &data) {
	this->solidViewActor();
});

EventManager::instance().registerHandler(PublicConstants::SET_GEOM_SELECT_TYPE, [this](const QVariantMap &data) {
	int type = data["data"].toInt();
	this->onSetSelectType(type, false);
});

EventManager::instance().registerHandler(PublicConstants::GET_GRID_NODE_INDEX, [this](const QVariantMap &data) {
	int index = data["data"].toInt();
	int type = data["type"].toInt();
	this->getGridDataByGridIndex(type, index);
});

EventManager::instance().registerHandler(PublicConstants::CLEAR_COLOR_BY_GEOM_INDEX, [this](const QVariantMap &data) {
	int index = data["data"].toInt();
	int type = data["type"].toInt();
	QString shape = data["shape"].toString();
	this->resetGeomColorByIndex(type, index, shape);
});


#pragma endregion 
}

bool PreProcessingWidget::importGeom(const QString & filePath,const QString & uuid)
{
    // if (filePath.isEmpty()) return false;
    // qDebug() << "导入的几何文件路径: " << filePath;
    // bool isSuccess = false;
    // TopoDS_Shape* shape = new TopoDS_Shape();
    // if (filePath.endsWith(".step", Qt::CaseInsensitive) || filePath.endsWith(".stp", Qt::CaseInsensitive))
    // {
    // 	isSuccess  = GemoRWUtilInstance::getInstance()->readOneShapeStepFile(filePath, shape);
    // }
    // else if (filePath.endsWith(".iges", Qt::CaseInsensitive) || filePath.endsWith(".igs", Qt::CaseInsensitive))
    // {
    // 	isSuccess = GemoRWUtilInstance::getInstance()->readOneShapeIgesFile(filePath, shape);
    // }
    // else if (filePath.endsWith(".brep", Qt::CaseInsensitive))
    // {
    // 	isSuccess = GemoRWUtilInstance::getInstance()->readOneShapeBrepFile(filePath, shape);
    // }
    // if (isSuccess)
    // {
    // 	removeUuidData(uuid);
    // 	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // 	GeomShapeObject* _object = new GeomShapeObject(shape);
    // 	gemoInstance->appendGeomShapeObject(_object);
    // 	int shapeIndex = gemoInstance->getIndexByGeomShapeObject(_object);
    // 	if (shapeIndex > -1)
    // 	{
    // 		auto actors = gemoInstance->getAllActor(shapeIndex);
    // 		for (size_t i = 0; i < actors.length(); i++)
    // 		{
    // 			addGeomActor(actors[i]);
    // 		}
    // 		addUuidData(uuid, shape, _object, actors);
    // 		createTemporaryStepFile(createTemporaryStepFilePath());
    // 		onSetSelectType(m_selectGemoType);
    // 		reFresh();
    // 		m_outputService->outputLog(QString("几何导入成功!【%1】").arg(filePath));
    // 		return true;
    // 	}
    // }
	return false;
}

bool PreProcessingWidget::exportGeom()
{
	if (m_uuidGeomShape.size() <= 0) return false;
	QString filePath = QFileDialog::getSaveFileName(
		this,
		("导出几何"),
		QDir::homePath(),
		"Geometry Files (*.step *.stp *.iges *.igs *.brep)"
	);

	if (filePath.isEmpty()) return false;
	QStringList validExtensions = { ".step", ".stp", ".iges", ".igs", ".brep" };
	bool hasValidExtension = false;
	QString extension;
	for (const QString& ext : validExtensions) 
	{
		if (filePath.endsWith(ext, Qt::CaseInsensitive))
		{
			hasValidExtension = true;
			extension = ext.toLower();
			break;
		}
	}

	if (!hasValidExtension) 
	{
		filePath = filePath.section('.', 0, 0);
		filePath += ".step";
		extension = ".step";
	}

	bool success = false;
	QVector<TopoDS_Shape*> shapes;
	auto keys = m_uuidGeomShape.keys();
	for (const auto key : keys)
	{
		shapes.append(m_uuidGeomShape[key]);
	}
	if (extension == ".iges" || extension == ".igs")
	{
		success = GemoRWUtilInstance::getInstance()->writeMultipleShapesIgesFile(filePath, shapes);
	}
	else if (extension == ".brep") 
	{
		success = GemoRWUtilInstance::getInstance()->writeMultipleShapesBrepFile(filePath, shapes);
	}
	else
	{
        // success = GemoRWUtilInstance::getInstance()->writeMultipleShapesStepFile(filePath, shapes);
	}
	if (success) 
	{
		m_outputService->outputLog(QString("几何导出成功!【%1】").arg(filePath));
	}
	else 
	{
		m_outputService->outputLog(QString("几何导出失败!【%1】").arg(filePath));
	}
}

void PreProcessingWidget::solidViewActor()
{
	setAllGridActorVisibility(false);
	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();

}

void PreProcessingWidget::lineViewActor()
{
	for (auto const & actor : m_mainGeomActor)
	{
		if (actor)
		{
			actor->GetProperty()->SetRepresentationToWireframe();
		}
	}
	reFresh();
}

void PreProcessingWidget::onSetSelectType(int type,bool isReset)
{
	m_selectGemoType = type;
	if (isReset)
	{
	  resetGeomColor();
	}
	if (type == 0)
	{ 
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectNone);
	}
	else if (type == 1)
	{ //点
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectVertex);
		m_picker->setPickList(m_mainGeomActor);
	}
	else if (type == 2)
	{ //线
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectPolyLine);
		m_picker->setPickList(m_mainGeomActor);
	}
	else if (type == 3)
	{ //面
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectSurface);
		m_picker->setPickList(m_mainGeomActor);
	}
	else if (type == 4)
	{ //体
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectVolume);
		m_picker->setPickList(m_mainGeomActor);
	}
	else if (type == 5)
	{ //仅显
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectVolume);
		m_picker->setPickList(m_mainGeomActor);
	}
	else if (type == 6)
	{ //反显
		m_picker->setSelectMode(GUI::SelectGeomMode::SelectVolume);
		m_picker->setPickList(m_mainGeomActor);
	}
	else
	{
		return;
	}
}

void PreProcessingWidget::onGeomShapeSelectedSig(GeomShapeObject * object, int index)
{
	qDebug() << "当前几何:" << object << ",当前索引:" << index;
	if (m_measureSelectionType == 0)
	{
		calculateGeomPointPos(object, index);
	}
	else if (m_measureSelectionType == 1)
	{
		calculateGeomPointsCentroid(object, index);
	}
	else if (m_measureSelectionType == 2)
	{
		calculateGeomFirstPointDistance(object, index);
	}
	else if (m_measureSelectionType == 3)
	{
		calculateGeomSecondPointOrLineDistance(object, index);
	}
}

void PreProcessingWidget::onSelectGemoPoint(vtkActor * actor, int cellIndex, int pointIndex)
{
    // GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // gemoInstance->onSelectGemoPoint(actor, cellIndex, pointIndex);
}

void PreProcessingWidget::onSelectGemoLine(vtkActor * actor, int cellIndex, int pointIndex)
{
    // GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // gemoInstance->onSelectGemoLine(actor, cellIndex, pointIndex);
}

void PreProcessingWidget::onSelectGemoFace(vtkActor * actor, int cellIndex, int pointIndex)
{
    // GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // gemoInstance->onSelectGemoFace(actor, cellIndex, pointIndex);
}

void PreProcessingWidget::onSelectGemoSolid(vtkActor * actor, int cellIndex, int pointIndex)
{
    // if (m_selectGemoType == 4)
    // {
    // 	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // 	gemoInstance->onSelectGemoSolid(actor, cellIndex, pointIndex);
    // }
    // else if (m_selectGemoType == 5)
    // {
    // 	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // 	gemoInstance->onOnlyShowGemoSolid(actor, cellIndex, pointIndex);
    // }
    // else if (m_selectGemoType == 6)
    // {
    // 	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // 	gemoInstance->onOnlyNotShowGemoSolid(actor, cellIndex, pointIndex);
    // }
	
}

void PreProcessingWidget::resetGeomColor()
{
    // GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // gemoInstance->resetAllGeomShapeObjectColor();
	reFresh();
}

void PreProcessingWidget::transmissionViewActor()
{
	static bool isAllShow = false;
	for (auto const & actor : m_mainGeomActor)
	{
		if (isAllShow)
		{
			if (actor)
			{
				actor->GetProperty()->SetOpacity(1);
				m_uiFrameService->getTransLabelAct()->setText(QString::number(100));
				m_uiFrameService->getTransSliderAct()->setValue(100);
			}
			
		}
		else
		{
			if (actor)
			{
				actor->GetProperty()->SetOpacity(0.5);
				m_uiFrameService->getTransLabelAct()->setText(QString::number(50));
				m_uiFrameService->getTransSliderAct()->setValue(50);
			}
		}
	}
	isAllShow = !isAllShow;
	reFresh();
}

void PreProcessingWidget::addGeomActor(const vtkSmartPointer<vtkActor> actor)
{
	m_renderer->AddActor(actor);
	m_mainGeomActor.append(actor);
}

void PreProcessingWidget::addGeomActorList(const QList<vtkSmartPointer<vtkActor>> actors)
{
	for (auto const & actor:actors)
	{
		m_renderer->AddActor(actor);
		m_mainGeomActor.append(actor);
	}
}

void PreProcessingWidget::addGridActor(const vtkSmartPointer<vtkActor> actor)
{
	m_renderer->AddActor(actor);
	m_mainGridActor.append(actor);
}

void PreProcessingWidget::addGridActorList(const QList<vtkSmartPointer<vtkActor>> actors)
{
	for (auto const & actor : actors)
	{
		m_renderer->AddActor(actor);
		m_mainGridActor.append(actor);
	}
}

void PreProcessingWidget::removeActor(vtkSmartPointer<vtkActor> actor)
{
	m_renderer->RemoveActor(actor);
}

void PreProcessingWidget::addActor(vtkSmartPointer<vtkActor> actor)
{
    m_renderer->AddActor(actor);
}

void PreProcessingWidget::removeAllGeomActor()
{
	for (auto const & actor : m_mainGeomActor)
	{
		removeActor(actor);
		m_mainGeomActor.removeFirst();
	}
}

void PreProcessingWidget::removeUuidData(const QString & uuid)
{
	removeActorByUuid(uuid);
	removeGeomObjectByUuid(uuid);
	removeGeomShapeByUuid(uuid);
}

void PreProcessingWidget::addUuidData(const QString & uuid, TopoDS_Shape* shape,
	GeomShapeObject* object, QList<vtkActor*> actors)
{
	m_uuidGeomShape.insert(uuid, shape);
	m_uuidGeomObject.insert(uuid, object);
	m_uuidActMap.insert(uuid, actors);
}

void PreProcessingWidget::removeActorByUuid(const QString & uuid)
{
	if (m_uuidActMap.size() <= 0)return;
	if (m_uuidActMap.contains(uuid))
	{
		auto actors = m_uuidActMap[uuid];
		for (auto act:actors)
		{
			removeActor(act);
			m_mainGeomActor.removeOne(act);
		}
		m_uuidActMap.remove(uuid);
	}
	return;
}

void PreProcessingWidget::removeGeomShapeByUuid(const QString & uuid)
{
	if (m_uuidGeomShape.size() <= 0)return;
	if (m_uuidGeomShape.contains(uuid))
	{
		auto shape = m_uuidGeomShape[uuid];
		m_uuidGeomShape.remove(uuid);
		if (shape)
		{
			delete shape;
			shape = nullptr;
		}

	}
	return;
}

void PreProcessingWidget::removeGeomObjectByUuid(const QString & uuid)
{
    // if (m_uuidGeomObject.size() <= 0)return;
    // if (m_uuidGeomObject.contains(uuid))
    // {
    // 	auto object = m_uuidGeomObject[uuid];
    // 	m_uuidGeomObject.remove(uuid);
    // 	GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // 	gemoInstance->removeFirstGeomShapeObject(object);
    // }
	return;
}

void PreProcessingWidget::setActorVisibility(bool isShow, vtkSmartPointer<vtkActor> actor)
{
	if (!actor)return;
	actor->SetVisibility(isShow);
}

void PreProcessingWidget::setAllGeomActorVisibility(bool isShow)
{
	for (auto const & actor : m_mainGeomActor)
	{
		setActorVisibility(isShow,actor);
	}
}

void PreProcessingWidget::setAllGridActorVisibility(bool isShow)
{
	for (auto const & actor : m_mainGridActor)
	{
		setActorVisibility(isShow, actor);
	}
}

void PreProcessingWidget::removeAllGridActor()
{
	for (auto const & actor : m_mainGridActor)
	{
		removeActor(actor);
		m_mainGridActor.removeFirst();
	}
}

void PreProcessingWidget::removeAllActor()
{
    m_renderer->RemoveAllViewProps();
}

void PreProcessingWidget::onShowFace()
{

}

void PreProcessingWidget::onShowLine()
{

}

void PreProcessingWidget::onShowPoint()
{

}

void PreProcessingWidget::onMeasureView()
{

}

void PreProcessingWidget::onMeasurePositionView()
{
	PointPickingDialog* dialog = new PointPickingDialog(this);
	connect(dialog, &PointPickingDialog::pickingToggled, [this](bool enabled)
	{
		onMeasurePosBtnToggled(enabled);
	});
	dialog->show();
}

void PreProcessingWidget::onMeasureCentroidView()
{
	CentroidPickingDialog* dialog = new CentroidPickingDialog(this);
	connect(dialog, &CentroidPickingDialog::pickingToggled, [this](bool enabled)
	{
		onMeasureCentroidBtnToggled(enabled);
	});
	connect(dialog, &CentroidPickingDialog::calculateCentroidRequested, [this]()
	{
		onCalculateCentroidRequested();
	});
	dialog->show();
}

void PreProcessingWidget::onMeasureDistanceView()
{
	DistancePickingDialog* dialog = new DistancePickingDialog(this);

	connect(dialog, &DistancePickingDialog::pointSelectionToggled, [this](bool enabled)
	{
		onMeasureDistancePointBtnToggled(enabled);
	});

	connect(dialog, &DistancePickingDialog::pointOrLineSelectionToggled, [this](bool enabled,QString type)
	{
		onMeasureDistanceLineBtnToggled(enabled,type);
	});

	connect(dialog, &DistancePickingDialog::calculateDistanceRequested, [this]()
	{
		onCalculateDistanceRequested();
	});
	dialog->show();
}

void PreProcessingWidget::resetGeomColorByIndex(int type, int index,const QString& uuid)
{
    // GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    // gemoInstance->resetGeomShapeColorByIndex(type,index, uuid);
	m_renderWindow->Render();
}

void PreProcessingWidget::onMeasurePosBtnToggled(bool isConnect)
{
	if (isConnect)
	{
		onSetSelectType(1);
		connect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
	}
	else
	{
		onSetSelectType(0);
		disconnect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
		m_measurePointPair.first = nullptr;
		m_measurePointPair.second = 0;
	}
	m_measureSelectionType = MeasureSelectionType::SinglePoint;
}

void PreProcessingWidget::onMeasureCentroidBtnToggled(bool isConnect)
{
	if (isConnect)
	{
		onSetSelectType(1);
		connect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
	}
	else
	{
		onSetSelectType(0);
		disconnect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
		m_centroidPointsPair.clear();
	}
	m_measureSelectionType = MeasureSelectionType::CentroidPoints;
}

void PreProcessingWidget::onMeasureDistancePointBtnToggled(bool isConnect)
{
	if (isConnect)
	{
		onSetSelectType(1);
		connect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
	}
	else
	{
		onSetSelectType(0);
		disconnect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
		m_distanceFirstPointPair.first = nullptr;
		m_distanceFirstPointPair.second = 0;
	}
	m_measureSelectionType = MeasureSelectionType::PointToPoint;
}

void PreProcessingWidget::onMeasureDistanceLineBtnToggled(bool isConnect,const QString& type )
{
	if (isConnect)
	{
		m_distancePickingType = type;
		if (m_distancePickingType == "点")
		{
			onSetSelectType(1);
		}
		else if (m_distancePickingType == "线")
		{
			onSetSelectType(2);
		}
		connect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
	}
	else
	{
		onSetSelectType(0);
		disconnect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(geomShapeSelectedSig(GeomShapeObject *, int)), this, SLOT(onGeomShapeSelectedSig(GeomShapeObject *, int)));
		m_distanceSecondPointPair.first = nullptr;
		m_distanceSecondPointPair.second = 0;
		m_distanceLine.first = nullptr;
		m_distanceLine.second = 0;
	}
	m_measureSelectionType = MeasureSelectionType::PointToLine;
}

void PreProcessingWidget::onCalculateCentroidRequested()
{
    // if (m_centroidPointsPos.size() < 1 || m_centroidPointsPair.size() < 1) return;
    // double xSum = 0.0, ySum = 0.0, zSum = 0.0;
    // int count = m_centroidPointsPos.size();
    // for (const gp_Pnt& p : m_centroidPointsPos)
    // {
    // 	xSum += p.X();
    // 	ySum += p.Y();
    // 	zSum += p.Z();
    // }
    // gp_Pnt pnt =  gp_Pnt(xSum / count, ySum / count, zSum / count);
    // QString x = QString::number(pnt.X(), 'f', 6);
    // QString y = QString::number(pnt.Y(), 'f', 6);
    // QString z = QString::number(pnt.Z(), 'f', 6);
    // m_outputService->outputLog(QString("当前点集质心坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
    // for (size_t i = 0; i < m_centroidPointsPair.size(); i++)
    // {
    // 	QPair<GeomShapeObject *, int> firstpair = m_centroidPointsPair[i];
    // 	GeomShapeObject *geoset = firstpair.first;
    // 	int index = firstpair.second;
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomPoint(geoset, index, false);
    // }
    // m_centroidPointsPair.clear();
    // m_centroidPointsPos.clear();
}

void PreProcessingWidget::onCalculateDistanceRequested()
{
	// 使用容差判断是否为零点
	const double tolerance = Precision::Confusion();
	bool isFirstPosZero = (std::abs(m_distanceFirstPos.X()) < tolerance &&
		std::abs(m_distanceFirstPos.Y()) < tolerance &&
		std::abs(m_distanceFirstPos.Z()) < tolerance);
	if (m_distancePickingType=="点")
	{
		m_distanceEdge.Nullify();
		//m_outputService->outputLog(QString("当前距离测量线元素已清空。"));
		bool isSecondPosZero = (std::abs(m_distanceSecondPos.X()) < tolerance &&
			std::abs(m_distanceSecondPos.Y()) < tolerance &&
			std::abs(m_distanceSecondPos.Z()) < tolerance);
		if (isFirstPosZero || isSecondPosZero)
		{
			m_outputService->outputLog(QString("当前点点距离测量元素不足"));
			return;
		}
		double x1 = m_distanceFirstPos.X();
		double y1 = m_distanceFirstPos.Y();
		double z1 = m_distanceFirstPos.Z();
		double x2 = m_distanceSecondPos.X();
		double y2 = m_distanceSecondPos.Y();
		double z2 = m_distanceSecondPos.Z();
		double dx = qAbs(x2 - x1);
		double dy = qAbs(y2 - y1);
		double dz = qAbs(z2 - z1);
		QString x = QString::number(dx, 'f', 6);
		QString y = QString::number(dy, 'f', 6);
		QString z = QString::number(dz, 'f', 6);
		double d = m_distanceSecondPos.Distance(m_distanceFirstPos);
		m_outputService->outputLog(QString("当前点点距离测量结果:【X距离:%1m】 【Y距离:%2m】 【Z距离:%3m】 【总距离:%4m】")
			.arg(x)
			.arg(y)
			.arg(z)
			.arg(d)
		);
		m_distanceFirstPos.SetCoord(0, 0, 0);
		m_distanceSecondPos.SetCoord(0, 0, 0);
		m_outputService->outputLog(QString("当前点点距离测量元素已清空,请重新选择。"));
	}
	else if (m_distancePickingType == "线")
	{
		m_distanceSecondPos.SetCoord(0, 0, 0);
		//m_outputService->outputLog(QString("当前距离测量第二顶点元素已清空。"));
		if (isFirstPosZero || m_distanceEdge.IsNull())
		{
			m_outputService->outputLog(QString("当前点线距离测量元素不足"));
			return;
		}
		QList<double> _list = distancePointToEdge(m_distanceFirstPos, m_distanceEdge);
		if (_list.size() == 0)
		{
			m_outputService->outputLog(QString("当前点线距离测量计算错误。"));
			return;
		}
		QString x = QString::number(_list[0], 'f', 6);
		QString y = QString::number(_list[1], 'f', 6);
		QString z = QString::number(_list[2], 'f', 6);
		QString d = QString::number(_list[3], 'f', 6);
		m_outputService->outputLog(QString("当前点线距离测量结果:【X距离:%1m】 【Y距离:%2m】 【Z距离:%3m】 【总距离:%4m】")
			.arg(x)
			.arg(y)
			.arg(z)
			.arg(d)
		);
		m_distanceFirstPos.SetCoord(0, 0, 0);
		m_distanceEdge.Nullify();
		m_outputService->outputLog(QString("当前距离测量元素已清空,请重新选择。"));
	}
	
}

void PreProcessingWidget::deleteCreateGemo()
{
}

bool PreProcessingWidget::createTemporaryStepFile(const QString & tempPath)
{
    // QVector<TopoDS_Shape*> vecShape;
    // for (auto shape : m_uuidGeomShape)
    // {
    // 	vecShape << shape;
    // }
 //    m_uuidActMap;
    // bool success = GemoRWUtilInstance::getInstance()->writeMultipleShapesStepFile(tempPath, vecShape);
    // if (success)
    // {
    // 	m_curGeomPath = tempPath;
    // }
    return true;
}

QString PreProcessingWidget::createTemporaryStepFilePath()
{
	QString	exelPath = QCoreApplication::applicationDirPath();
	const QString tempPath = exelPath + "/../tempGeom/";
	QDir dir = QDir(tempPath);
	if (!dir.exists())
	{
		bool flag = dir.mkpath(tempPath);
	}
	QString _path = tempPath + "1.step";
	return _path;
}

void PreProcessingWidget::afterCreateSimpleGeom(TopoDS_Shape* shape,const QString &uuid)
{
    removeUuidData(uuid);
    qDebug()<<uuid;
    GeomManager::GeomManagerInstance* gemoInstance = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* object = new GeomShapeObject(uuid,shape);
    gemoInstance->appendGeomShapeObject(object);

    if (!shape || shape->IsNull())
        return;
    m_renderer->AddActor(object->getSurfActor());
    m_renderer->AddActor(object->getEdgeActor());
    m_renderer->ResetCamera();
    m_renderer->GetRenderWindow()->Render();


}

void PreProcessingWidget::reFresh()
{
	m_renderer->ResetCamera();
	m_renderWindow->Render();
}

void PreProcessingWidget::showEvent(QShowEvent *event)
{
    if (m_renderWindowInteractor)
    {
        m_renderWindow->Render();
    }
}

void PreProcessingWidget::createOrientationMarkerWidget()
{
	vtkSmartPointer<vtkAxesActor> axes = vtkSmartPointer<vtkAxesActor>::New();
	vtkOrientationMarkerWidget *orientationMarkerWidget =
		vtkOrientationMarkerWidget::New();
	orientationMarkerWidget->SetOutlineColor(0.9300, 0.5700, 0.1300);
	orientationMarkerWidget->SetOrientationMarker(axes);
	orientationMarkerWidget->SetInteractor(m_renderWindowInteractor);
	orientationMarkerWidget->SetViewport(0.0, 0.0, 0.2, 0.2); // 调整大小和位置
	orientationMarkerWidget->SetEnabled(true);
	orientationMarkerWidget->InteractiveOff();
}

void PreProcessingWidget::setView(ViewType type)
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
	reFresh();
}

void PreProcessingWidget::setViewValue(double x1, double x2, double x3, double y1, double y2, double y3, double z1, double z2, double z3)
{
	vtkCamera* camera = m_renderer->GetActiveCamera();
	camera->SetViewUp(y1, y2, y3);
	camera->SetPosition(z1, z2, z3);
	camera->SetFocalPoint(x1, x2, x3);
}

void PreProcessingWidget::fitView()
{
	m_renderer->ResetCamera();
}

void PreProcessingWidget::calculateGeomPointPos(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_measurePointPair.first != nullptr)
    // {
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomPoint(m_measurePointPair.first, m_measurePointPair.second, false);
    // 	m_measurePointPair.first = nullptr;
    // 	m_measurePointPair.second = index;
    // }
    // m_measurePointPair.first = object;
    // m_measurePointPair.second = index;
    // TopoDS_Shape *shape = object->getShape();
    // TopExp_Explorer ptExp(*shape, TopAbs_VERTEX);
    // for (int k = 0; k < index; ++k)
    // 	ptExp.Next();
    // const TopoDS_Vertex &vertex = TopoDS::Vertex(ptExp.Current());
    // gp_Pnt pt = BRep_Tool::Pnt(vertex);
    // double point[3];
    // point[0] = pt.X();
    // point[1] = pt.Y();
    // point[2] = pt.Z();
    // QString x = QString::number(point[0], 'f', 6);
    // QString y = QString::number(point[1], 'f', 6);
    // QString z = QString::number(point[2], 'f', 6);
    // m_outputService->outputLog(QString("当前选择点位置坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
}

void PreProcessingWidget::calculateGeomPointsCentroid(GeomShapeObject * object, int index)
{
	if (object == nullptr && index < 0)return;
	TopoDS_Shape *shape = object->getShape();
	TopExp_Explorer ptExp(*shape, TopAbs_VERTEX);
	for (int k = 0; k < index; ++k)
		ptExp.Next();
	const TopoDS_Vertex &vertex = TopoDS::Vertex(ptExp.Current());
	gp_Pnt pt = BRep_Tool::Pnt(vertex);
	QString x = QString::number(pt.X(), 'f', 6);
	QString y = QString::number(pt.Y(), 'f', 6);
	QString z = QString::number(pt.Z(), 'f', 6);
	qDebug()<<QString("质心点坐标:【%1,%2,%3】").arg(x).arg(y).arg(z);
	m_centroidPointsPos << pt;
	QPair<GeomShapeObject *, int> tempPair{ object, index };
	m_centroidPointsPair << tempPair;
}

void PreProcessingWidget::calculateGeomFirstPointDistance(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_distanceFirstPointPair.first != nullptr)
    // {
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomPoint(m_distanceFirstPointPair.first, m_distanceFirstPointPair.second, false);
    // 	m_distanceFirstPointPair.first = nullptr;
    // 	m_distanceFirstPointPair.second = index;
    // }
    // m_distanceFirstPointPair.first = object;
    // m_distanceFirstPointPair.second = index;
    // TopoDS_Shape *shape = object->getShape();
    // TopExp_Explorer ptExp(*shape, TopAbs_VERTEX);
    // for (int k = 0; k < index; ++k)
    // 	ptExp.Next();
    // const TopoDS_Vertex &vertex = TopoDS::Vertex(ptExp.Current());
    // gp_Pnt pt = BRep_Tool::Pnt(vertex);
    // m_distanceFirstPos = pt;
    // double point[3];
    // point[0] = pt.X();
    // point[1] = pt.Y();
    // point[2] = pt.Z();
    // QString x = QString::number(point[0], 'f', 6);
    // QString y = QString::number(point[1], 'f', 6);
    // QString z = QString::number(point[2], 'f', 6);
    // m_outputService->outputLog(QString("当前距离测量选择第一个点的位置坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
}

void PreProcessingWidget::calculateGeomSecondPointOrLineDistance(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_distancePickingType == "点")
    // {
    // 	if (m_distanceSecondPointPair.first != nullptr)
    // 	{
    // 		GeomManager::GeomManagerInstance::getInstance()->highLightGeomPoint(m_distanceSecondPointPair.first, m_distanceSecondPointPair.second, false);
    // 		m_distanceSecondPointPair.first = nullptr;
    // 		m_distanceSecondPointPair.second = index;
    // 	}
    // 	m_distanceSecondPointPair.first = object;
    // 	m_distanceSecondPointPair.second = index;
    // 	TopoDS_Shape *shape = object->getShape();
    // 	TopExp_Explorer ptExp(*shape, TopAbs_VERTEX);
    // 	for (int k = 0; k < index; ++k)
    // 		ptExp.Next();
    // 	const TopoDS_Vertex &vertex = TopoDS::Vertex(ptExp.Current());
    // 	gp_Pnt pt = BRep_Tool::Pnt(vertex);
    // 	m_distanceSecondPos = pt;
    // 	double point[3];
    // 	point[0] = pt.X();
    // 	point[1] = pt.Y();
    // 	point[2] = pt.Z();
    // 	QString x = QString::number(point[0], 'f', 6);
    // 	QString y = QString::number(point[1], 'f', 6);
    // 	QString z = QString::number(point[2], 'f', 6);
    // 	m_outputService->outputLog(QString("当前距离测量选择第二个点的位置坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
    // }
    // else if(m_distancePickingType == "线")
    // {
    // 	if (m_distanceLine.first != nullptr)
    // 	{
    // 		GeomManager::GeomManagerInstance::getInstance()->highLightGeomLine(m_distanceLine.first, m_distanceLine.second, false);
    // 		m_distanceLine.first = nullptr;
    // 		m_distanceLine.second = index;
    // 	}
    // 	m_distanceLine.first = object;
    // 	m_distanceLine.second = index;
    // 	TopoDS_Shape *shape = m_distanceLine.first->getShape();
    // 	TopExp_Explorer edgeExp(*shape, TopAbs_EDGE);
    // 	for (int indexExp = 0; edgeExp.More(); edgeExp.Next(), ++indexExp)
    // 	{
    // 		if (m_distanceLine.second == indexExp)
    // 		{
    // 			const TopoDS_Shape &edgeShape = edgeExp.Current();
    // 			if (edgeShape.IsNull())return;
    // 			const TopoDS_Edge &oneedge = TopoDS::Edge(edgeShape);
    // 			m_distanceEdge = oneedge;
    // 			Standard_Real edgeLength = GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(oneedge));
    // 			if (edgeLength < 0)return;
    // 			m_outputService->outputLog(QString("当前位置测量选择线的长度:【%1m】,索引:【%2】")
    // 				.arg(edgeLength).arg(index));
    // 		}
    // 	}
    // }
	
}

QList<double> PreProcessingWidget::distancePointToEdge(const gp_Pnt & point, const TopoDS_Edge & edge)
{
	QList<double> _list;
	BRepAdaptor_Curve curve(edge);
	if (!curve.Is3DCurve()) 
	{
		qDebug() << "Edge is not a 3D curve.";
		return _list;
	}
	// 获取底层的 Geom_Curve
	Handle(Geom_Curve) geomCurve = curve.Curve().Curve();
	// 投影点到曲线
	GeomAPI_ProjectPointOnCurve projector(point, geomCurve);
	if (projector.NbPoints() == 0)
	{
		qDebug() << "Projection failed, no points found!";
		return _list;
	}
	// 获取最近的投影点
	gp_Pnt projectedPoint = projector.NearestPoint();
	double dx = std::abs(projectedPoint.X() - point.X());
	double dy = std::abs(projectedPoint.Y() - point.Y());
	double dz = std::abs(projectedPoint.Z() - point.Z());
	double totalDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
	_list << dx << dy << dz << totalDistance;
	return _list;
}

void PreProcessingWidget::calculateGeomTwoPointLength(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // QPair<GeomShapeObject *, int> tempPair{ object, index };
    // m_pointSelectList.push_back(tempPair);
    // if (m_pointSelectList.size() > 2)
    // {
    // 	QPair<GeomShapeObject *, int> firstpair = m_pointSelectList.front();
    // 	GeomShapeObject *geoset = firstpair.first;
    // 	int index = firstpair.second;
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomPoint(geoset, index, false);
    // 	m_pointSelectList.removeFirst();
    // }
    // TopoDS_Shape *shape = object->getShape();
    // TopExp_Explorer ptExp(*shape, TopAbs_VERTEX);
    // for (int k = 0; k < index; ++k)
    // 	ptExp.Next();
    // const TopoDS_Vertex &vertex = TopoDS::Vertex(ptExp.Current());
    // gp_Pnt pt = BRep_Tool::Pnt(vertex);
    // gp_Pnt bef(m_point[0], m_point[1], m_point[2]);
    // if (m_pointSelectList.size() != 2)
    // {
    // 	m_point[0] = pt.X();
    // 	m_point[1] = pt.Y();
    // 	m_point[2] = pt.Z();
    // 	QString x = QString::number(m_point[0], 'f', 6);
    // 	QString y = QString::number(m_point[1], 'f', 6);
    // 	QString z = QString::number(m_point[2], 'f', 6);
    // 	m_outputService->outputLog(QString("坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
    // 	return;
    // }
    // double d = bef.Distance(pt);
    // m_point[0] = pt.X();
    // m_point[1] = pt.Y();
    // m_point[2] = pt.Z();
    // QString x = QString::number(m_point[0], 'f', 6);
    // QString y = QString::number(m_point[1], 'f', 6);
    // QString z = QString::number(m_point[2], 'f', 6);
    // m_outputService->outputLog(QString("坐标:【%1,%2,%3】").arg(x).arg(y).arg(z));
    // m_outputService->outputLog(QString("距离:【%1】").arg(d));
}

void PreProcessingWidget::calculateGeomCurveLength(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_line.first != nullptr)
    // {
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomLine(m_line.first, m_line.second, false);
    // 	m_line.first = nullptr;
    // 	m_line.second = index;
    // }
    // m_line.first = object;
    // m_line.second = index;
    // TopoDS_Shape *shape = m_line.first->getShape();
    // TopExp_Explorer edgeExp(*shape, TopAbs_EDGE);
    // for (int indexExp = 0; edgeExp.More(); edgeExp.Next(), ++indexExp)
    // {
    // 	if (m_line.second == indexExp)
    // 	{
    // 		const TopoDS_Shape &edgeShape = edgeExp.Current();
    // 		if (edgeShape.IsNull())return;
    // 		const TopoDS_Edge &oneedge = TopoDS::Edge(edgeShape);
    // 		Standard_Real edgeLength = GCPnts_AbscissaPoint::Length(BRepAdaptor_Curve(oneedge));
    // 		if (edgeLength < 0)return;
    // 		m_outputService->outputLog(QString("长度:【%1】").arg(edgeLength));
    // 	}
    // }
}

void PreProcessingWidget::calculateGeomFaceArea(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_face.first != nullptr)
    // {
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomFace(m_face.first, m_face.second, false);
    // 	m_face.first = nullptr;
    // 	m_face.second = index;
    // }
    // m_face.first = object;
    // m_face.second = index;
    // TopoDS_Shape *shape = object->getShape();
    // TopExp_Explorer faceExp(*shape, TopAbs_FACE);
    // for (int indexExp = 0; faceExp.More(); faceExp.Next(), ++indexExp)
    // {
    // 	if (index == indexExp)
    // 	{
    // 		const TopoDS_Shape &faceShape = faceExp.Current();
    // 		if (faceShape.IsNull())return;
    // 		const TopoDS_Face &oneface = TopoDS::Face(faceShape);
    // 		GProp_GProps aSurfaceProps;
    // 		BRepGProp::SurfaceProperties(oneface, aSurfaceProps);
    // 		double area = aSurfaceProps.Mass();
    // 		if (area < 0)return;
    // 		m_outputService->outputLog(QString("面积:【%1】").arg(area));
    // 	}
    // }
	
}

void PreProcessingWidget::calculateGeomSolidVolume(GeomShapeObject * object, int index)
{
    // if (object == nullptr && index < 0)return;
    // if (m_solid.first != nullptr)
    // {
    // 	GeomManager::GeomManagerInstance::getInstance()->highLightGeomSolid(m_solid.first, m_solid.second, false);
    // 	m_solid.first = nullptr;
    // 	m_solid.second = index;
    // }
    // m_solid.first = object;
    // m_solid.second = index;
    // TopoDS_Shape *shape = object->getShape();
    // TopExp_Explorer solidExp(*shape, TopAbs_SOLID);
    // for (int indexExp = 0; solidExp.More(); solidExp.Next(), ++indexExp)
    // {
    // 	if (index == indexExp)
    // 	{
    // 		const TopoDS_Shape &solidShape = solidExp.Current();
    // 		if (solidShape.IsNull())return;
    // 		GProp_GProps aSolidProps;
    // 		BRepGProp::VolumeProperties(solidShape, aSolidProps);
    // 		double volume = aSolidProps.Mass();
    // 		if (volume < 0)return;
    // 		m_outputService->outputLog(QString("体积:【%1】").arg(volume));
    // 	}
    // }
}

bool PreProcessingWidget::writeMultipleShapesStepFile(const QString& filePath)
{
    GeomManager::GeomManagerInstance *m_geomMgr = GeomManager::GeomManagerInstance::getInstance();
    // 创建一个复合形状
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    QHash<QString, GeomShapeObject *> shapes = m_geomMgr->getShapMap();
    // 将所有输入形状添加到复合形状中
    for (const auto& shape : shapes)
    {
        if (!shape->getShape()->IsNull())
        {
            builder.Add(compound, *shape->getShape());
        }
    }

    STEPControl_Writer stepWriter;
    IFSelect_ReturnStatus status = stepWriter.Transfer(compound, STEPControl_AsIs);
    if (status != IFSelect_RetDone)
    {
        return false;
    }

    status = stepWriter.Write(filePath.toUtf8().constData());
    if (status != IFSelect_RetDone) {
        return false;
    }
    return true;
}

bool PreProcessingWidget::drawGrid()
{
    QString filePath = "C:/test/grid-test.step";
    writeMultipleShapesStepFile(filePath);
	if (!m_meshRefinementWidget && !m_meshRefinement3DWidget)return false;
    QString geomPath = filePath;
	if (geomPath == "")
	{
		m_outputService->outputLog("网格绘制失败!");
		return false;
	}
	m_outputService->outputLog("网格绘制中...请稍等!");
	removeAllGridActor();
	// 设置网格划分类型 默认为体网格划分
	int gridType = 3;
	if (m_meshRefinementWidget)
	{
		gridType = 2;
	}
	setGridParams(gridType);
	SMThreadingTool::SMThreading* worker = new SMThreadingTool::SMThreading(this);
	worker->postTask([=]() {m_meshProcessor.loadOCCFile(geomPath); });
	connect(worker, &SMThreadingTool::SMThreading::allTasksFinished, this, &PreProcessingWidget::onDrawGridTaskFinished);
	worker->start();
}

void PreProcessingWidget::setGridParams(int type)
{
	if (type == 2)
	{
		int minSize = m_meshRefinementWidget->getMinUnitSize();
		int maxSize = m_meshRefinementWidget->getMaxUnitSize();
		int sizeFactor = m_meshRefinementWidget->getSizeFactor();
		int smooth = m_meshRefinementWidget->getSmoothingIterations();
		bool fixShape = m_meshRefinementWidget->getFixShape();

		QString orderType = m_meshRefinementWidget->getOrderType();
		int orderValue;
		if (orderType == "一阶")
		{
			orderValue = 1;
		}
		else
		{
			orderValue = 2;
		}

		QString gridType = m_meshRefinementWidget->getGridType();
		int gridValue;
		if (gridType == "三角形")
		{
			gridValue = 1;
		}
		else
		{
			gridValue = 2;
		}
	
		QString SelectedMethod = m_meshRefinementWidget->getSelectedMethod();
		int methodValue;
		if (SelectedMethod == "Automatic")
		{
			methodValue = 2;
		}
		else if (SelectedMethod == "Delaunay")
		{
			methodValue = 5;
		}
		else if (SelectedMethod == "Frontal")
		{
			methodValue = 6;
		}
		else if (SelectedMethod == "Mesh Adapt")
		{
			methodValue = 1;
		}
		else if (SelectedMethod == "Delaunay for quad")
		{
			methodValue = 8;
		}
		else if (SelectedMethod == "Packing for parallelograms")
		{
			methodValue = 9;
		}
		m_meshProcessor.setGmshLengthMin(minSize);
		m_meshProcessor.setGmshLengthMax(maxSize);
		m_meshProcessor.setGmshSizeFactor(sizeFactor);
		m_meshProcessor.setGmshSmoothing(smooth);
		m_meshProcessor.setGmshDimension(2);
		m_meshProcessor.setElementOrder(orderValue);
		m_meshProcessor.setGirdType(gridValue);
		m_meshProcessor.setAlgorithm(methodValue);
		m_meshProcessor.setFixShape(fixShape);
	}
	else
	{
		int minSize = m_meshRefinement3DWidget->getMinUnitSize();
		int maxSize = m_meshRefinement3DWidget->getMaxUnitSize();
		int sizeFactor = m_meshRefinement3DWidget->getSizeFactor();
		bool fixShape = m_meshRefinement3DWidget->getFixShape();

		QString orderType = m_meshRefinement3DWidget->getOrderType();
		int orderValue;
		if (orderType == "一阶")
		{
			orderValue = 1;
		}
		else
		{
			orderValue = 2;
		}

		QString gridType = m_meshRefinement3DWidget->getGridType();
		int gridValue;
		if (gridType == "四面体")
		{
			gridValue = 3;
		}
		else
		{
			gridValue = 4;
		}
		QString SelectedMethod = m_meshRefinement3DWidget->getSelectedMethod();
		int methodValue;
		if (SelectedMethod == "Delaunay")
		{
			methodValue = 1;
		}
		else if (SelectedMethod == "New Delaunay")
		{
			methodValue = 2;
		}
		
		else if (SelectedMethod == "Frontal")
		{
			methodValue = 4;
		}
		else if (SelectedMethod == "Frontal Delaunay")
		{
			methodValue = 3;
		}
		else if (SelectedMethod == "MMG3D")
		{
			methodValue = 7;
		}
		else if (SelectedMethod == "R-tree")
		{
			methodValue = 9;
		}
		else if (SelectedMethod == "HXT")
		{
			methodValue = 10;
		}
		m_meshProcessor.setGmshLengthMin(minSize);
		m_meshProcessor.setGmshLengthMax(maxSize);
		m_meshProcessor.setGmshSizeFactor(sizeFactor);
		m_meshProcessor.setGmshDimension(3);
		m_meshProcessor.setElementOrder(orderValue);
		m_meshProcessor.setGirdType(gridValue);
		m_meshProcessor.setAlgorithm(methodValue);
		m_meshProcessor.setFixShape(fixShape);
	}
	
}

void PreProcessingWidget::countGridInfo()
{
	auto infoList = m_meshProcessor.getGridInfo();
	m_outputService->outputLog(QString("网格节点数:%1,点单元数:%2,线单元数:%3,面单元数:%4,体单元数:%5,")
		.arg(infoList[0])
		.arg(infoList[1])
		.arg(infoList[2])
		.arg(infoList[3])
		.arg(infoList[4])
	);
}

bool PreProcessingWidget::importGrid(const QString & filePath, const QString & uuid)
{
	if (filePath.isEmpty()) return false;
	qDebug() << "导入的网格文件路径: " << filePath;
	bool isSuccess = false;
	if (filePath.endsWith(".vtk", Qt::CaseInsensitive))
	{
		// 直接处理
		importGmshGrid(filePath);
		m_outputService->outputLog(QString("网格导入成功!【%1】").arg(filePath));
	}
	else
	{
		isSuccess = m_meshProcessor.loadImportGridFile(filePath);
		if (!isSuccess)return false;
		QString vtkPath = m_meshProcessor.getVtkPath();
		if (vtkPath == "")
		{
			m_outputService->outputLog(QString("网格导入失败!【%1】").arg(filePath));
			return false;
		}
		importGmshGrid(vtkPath);
		m_outputService->outputLog(QString("网格导入成功!【%1】").arg(filePath));
	}
	return true;
}

bool PreProcessingWidget::exportGrid()
{
	QString filePath = QFileDialog::getSaveFileName(
		this,
		("导出网格"),
		QDir::homePath(),
		"Grid Files (*.nas *.cgns *.stl *.vtk *.inp *.msh)"
	);

	if (filePath.isEmpty()) return false;
	QStringList validExtensions = { ".nas", ".cgns", ".stl", ".vtk", ".inp" ".msh" };
	bool hasValidExtension = false;
	QString extension;
	for (const QString& ext : validExtensions)
	{
		if (filePath.endsWith(ext, Qt::CaseInsensitive))
		{
			hasValidExtension = true;
			extension = ext.toLower();
			break;
		}
	}
	if (!hasValidExtension)
	{
		filePath = filePath.section('.', 0, 0);
        filePath += ".msh";
        extension = ".msh";
	}
	bool success = false;
	success  = m_meshProcessor.writeGridFile(filePath);
	if (success)
	{
		m_outputService->outputLog(QString("网格导出成功!【%1】").arg(filePath));
	}
	else
	{
		m_outputService->outputLog(QString("网格导出失败!【%1】").arg(filePath));
	}
	return true;
}

void PreProcessingWidget::importGmshGrid(const QString & vtkPath)
{
	vtkSmartPointer<vtkUnstructuredGridReader> reader = vtkSmartPointer<vtkUnstructuredGridReader>::New();
	reader->SetFileName(vtkPath.toUtf8());
	reader->Update();
	vtkSmartPointer<vtkUnstructuredGrid> unstructuredGrid = reader->GetOutput();
	vtkSmartPointer<vtkDataSetMapper> surfaceMapper = vtkSmartPointer<vtkDataSetMapper>::New();
	surfaceMapper->SetInputData(unstructuredGrid);

	// 设置表面颜色
	vtkSmartPointer<vtkActor> surfaceActor = vtkSmartPointer<vtkActor>::New();
	surfaceActor->SetMapper(surfaceMapper);
	surfaceActor->GetProperty()->SetColor(0.8, 0.8, 0.8); 
	surfaceActor->GetProperty()->SetOpacity(1);         
	surfaceActor->GetProperty()->SetEdgeVisibility(false);
	surfaceActor->GetProperty()->SetRepresentationToSurface();
	surfaceMapper->ScalarVisibilityOff();

	vtkSmartPointer<vtkDataSetMapper> wireframeMapper = vtkSmartPointer<vtkDataSetMapper>::New();
	wireframeMapper->SetInputData(unstructuredGrid);

	//设置线框属性（蓝色细线）
	vtkSmartPointer<vtkActor> wireframeActor = vtkSmartPointer<vtkActor>::New();
	wireframeActor->SetMapper(wireframeMapper);
	wireframeActor->GetProperty()->SetRepresentationToWireframe(); 
	wireframeActor->GetProperty()->SetColor(0, 0.0, 1);
	wireframeActor->GetProperty()->SetLineWidth(1.5);             
	wireframeActor->GetProperty()->LightingOff();                 
	wireframeActor->GetProperty()->SetOpacity(1.0);
	wireframeMapper->ScalarVisibilityOff();

	// 提取点数据
	vtkSmartPointer<vtkVertexGlyphFilter> vertexGlyphFilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
	vertexGlyphFilter->SetInputData(unstructuredGrid);
	vertexGlyphFilter->Update();

	// 创建点映射器
	vtkSmartPointer<vtkPolyDataMapper> pointMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
	pointMapper->SetInputConnection(vertexGlyphFilter->GetOutputPort());

	// 设置点属性（红色大点）
	vtkSmartPointer<vtkActor> pointActor = vtkSmartPointer<vtkActor>::New();
	pointActor->SetMapper(pointMapper);
	pointActor->GetProperty()->SetColor(0.0, 1.0, 0.0); 
	pointActor->GetProperty()->SetPointSize(2.0);      
	pointActor->GetProperty()->LightingOff();
	pointMapper->ScalarVisibilityOff();

	setAllGeomActorVisibility(false);
	// 添加顺序很重要：先加表面，再加线框，最后加点（点在最上层）
	addGridActor(surfaceActor);
	addGridActor(wireframeActor);
	addGridActor(pointActor);
	reFresh();
}

void PreProcessingWidget::gridViewActor()
{
	setAllGeomActorVisibility(false);
	setAllGridActorVisibility(true);
	reFresh();
}

bool PreProcessingWidget::onDrawGridTaskFinished()
{
	QString vtkPath = m_meshProcessor.getVtkPath();
	if (vtkPath == "")
	{
		m_outputService->outputLog("网格绘制失败!");
		m_bDrawGrid = false;
		return false;
	}
    // importGmshGrid(vtkPath);
    BoundaryConditions::instance().loadMesh(m_meshProcessor.getMshPath().toStdString());
    vtkSmartPointer<vtkActor> actor = BoundaryConditions::instance().actor();
    removeAllActor();
    addActor(actor);

    // 3. 配置拾取（只做一次）
    BoundaryConditions::instance().setInteractor(ui->vtkWidget->renderWindow()->GetInteractor());
    BoundaryConditions::instance().setPickMode(BCSelectMode::Face);
    BoundaryConditions::instance().enablePicking(false);

    m_outputService->outputLog("网格绘制成功!");
	m_bDrawGrid = true;
	return true;
}

bool PreProcessingWidget::getGridIsDraw()
{
	connect(GeomManager::GeomManagerInstance::getInstance(), SIGNAL(selectedGeomShapeIndex(GeomShapeObject *, int, int)), this,
		SLOT(onSelectedGeomShapeIndex(GeomShapeObject *, int, int)));
	return m_bDrawGrid;
}

void PreProcessingWidget::onSelectedGeomShapeIndex(GeomShapeObject * shape, int type, int _index)
{
	int index = _index + 1;
	QVariantMap payload;
	switch (type)
	{
	 case 1 :
		 m_iNewSelPointGeomIndex = index;
		 payload["data"] = m_iNewSelPointGeomIndex;
		 payload["shape"] = shape->getUUid();
		 EventManager::instance().sendEvent(PublicConstants::UPDATE_GEOM_POINT_INDEX, payload);
		 break;
	 case 2:
		 m_iNewSelLineGeomIndex = index;
		 payload["data"] = m_iNewSelLineGeomIndex;
		 payload["shape"] = shape->getUUid();
		 EventManager::instance().sendEvent(PublicConstants::UPDATE_GEOM_LINE_INDEX, payload);
		 break;
	 case 3:
		 m_iNewSelFaceGeomIndex = index;
		 payload["data"] = m_iNewSelFaceGeomIndex;
		 payload["shape"] = shape->getUUid();
         qDebug()<<"shape->getUUid() "<<shape->getUUid()<<" "<<m_iNewSelFaceGeomIndex;
		 EventManager::instance().sendEvent(PublicConstants::UPDATE_GEOM_FACE_INDEX, payload);
		 break;
	 case 4:
		 m_iNewSelSolidGeomIndex = index;
		 payload["data"] = m_iNewSelSolidGeomIndex;
		 payload["shape"] = shape->getUUid();
		 EventManager::instance().sendEvent(PublicConstants::UPDATE_GEOM_SOLID_INDEX, payload);
		 break;
	}
}

int PreProcessingWidget::getNewSelGeomIndex(int type)
{
	int index = -1;
	switch (type)
	{
	case 1:
		index = m_iNewSelPointGeomIndex;
		break;
	case 2:
		index = m_iNewSelLineGeomIndex;
		break;
	case 3:
		index = m_iNewSelFaceGeomIndex;
		break;
	case 4:
		index = m_iNewSelSolidGeomIndex;
		//getGridDataByGridIndex(1, index);
		break;
	default:
		break;
	}
	return index;
}

std::vector<int> PreProcessingWidget::getGridDataByGridIndex(int type, int index)
{
	if (type == 1)
	{
		std::vector<int> targetPoints = m_meshProcessor.getPointNodes(index);
		QString message = "";
		for (const auto & data : targetPoints)
		{
			std::vector<int> _data = m_meshProcessor.getPointGridNodeIndex(data);
			message = message + QString::number(data);
			message = message + ",";
		}
		m_outputService->outputLog(QStringLiteral("Current point grid node index:%1").arg(message));
		return targetPoints;
	}
	else if (type == 2)
	{
		std::vector<int> targetPoints = m_meshProcessor.getLineElements(index);
		QString message = "";
		for (const auto & data : targetPoints)
		{
			std::vector<int> _data = m_meshProcessor.getLineGridNodeIndex(data);
			message = message + QString::number(data);
			message = message + ",";
		}
		m_outputService->outputLog(QStringLiteral("Current line grid node index:%1").arg(message));
		return targetPoints;
	}
	else if (type == 3)
	{
		std::vector<int> targetPoints = m_meshProcessor.getSurfaceElements(index);
		QString message = "";
		for (const auto & data : targetPoints)
		{
			std::vector<int> _data = m_meshProcessor.getSurfaceGridNodeIndex(data);
			message = message + QString::number(data);
			message = message + ",";
		}
        qDebug()<<"Current face index: "<<index<<" "<<m_meshProcessor.getGMeshFaceIndex(index);
		m_outputService->outputLog(QStringLiteral("Current face grid node index:%1").arg(message));
		return targetPoints;
	}
	else if (type == 4)
	{
		std::vector<int> targetPoints = m_meshProcessor.getVolumeElements(index);
		QString message = "";
		for (const auto & data : targetPoints)
		{
			std::vector<int> _data = m_meshProcessor.getVolumeGridNodeIndex(data);
			message = message + QString::number(data);
			message = message + ",";
		}
		m_outputService->outputLog(QStringLiteral("Current solid grid node index:%1").arg(message));
		return targetPoints;
	}
	
}

void PreProcessingWidget::onTreeDeletePage(const QString & uuid)
{
	removeUuidData(uuid);
	reFresh();
	m_outputService->outputLog(QString("几何删除成功!"));
}

BasePage* PreProcessingWidget::newTetrahedronGridWidget(QString title)
{
	if (!m_gridNode)return nullptr;
	if (hasDirectChildNamed(m_gridNode, title)) return nullptr;
	MeshRefinementWidget* meshRefinementWidget = new MeshRefinementWidget();
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)meshRefinementWidget);
	m_gridNode->addChild(node);
	m_meshRefinementWidget = meshRefinementWidget;
	m_meshRefinement3DWidget = nullptr;
	return m_meshRefinementWidget;
}

BasePage* PreProcessingWidget::newHexahedronGridWidget(QString title)
{
	if (!m_gridNode)return nullptr;
	if (hasDirectChildNamed(m_gridNode, title)) return nullptr;
	MeshRefinement3DWidget* meshRefinement3DWidget = new MeshRefinement3DWidget();
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)meshRefinement3DWidget);
	m_gridNode->addChild(node);
	m_meshRefinement3DWidget = meshRefinement3DWidget;
	m_meshRefinementWidget = nullptr;
	return m_meshRefinement3DWidget;
}

BasePage* PreProcessingWidget::newGeometryImportWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	GeometryImportWidget* geometryImportWidget = new GeometryImportWidget();
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)geometryImportWidget);
	m_geomNode->addChild(node);
	QString Uuid = geometryImportWidget->getUuid();
	connect(geometryImportWidget, &GeometryImportWidget::fileImported, this, [this](QString path,QString uuid) {this->importGeom(path, uuid); });
	return geometryImportWidget;
}

BasePage * PreProcessingWidget::newGridImportWidget(QString title)
{
	if (!m_gridNode)return nullptr;
	GridImportWidget* gridImportWidget = new GridImportWidget();
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)gridImportWidget);
	m_gridNode->addChild(node);
	QString Uuid = gridImportWidget->getUuid();
	connect(gridImportWidget, &GridImportWidget::fileImported, this, [this](QString path, QString uuid) {this->importGrid(path, uuid); });
	return gridImportWidget;
}

BasePage* PreProcessingWidget::newGeometryBoxWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	BoxDialog* widget = new BoxDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &BoxDialog::parametersSubmitted, this, [this](QString uuid,double length, double width, double height, const gp_Pnt &position)
	{
		this->onBoxParamSubmitted(uuid,length, width, height, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometrySphereWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	SphereDialog* widget = new SphereDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &SphereDialog::parametersSubmitted, this, [this](QString uuid,double radius,const gp_Pnt &position)
	{
		this->onSphereParamSubmitted(uuid,radius,position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryConeWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	ConeDialog* widget = new ConeDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &ConeDialog::parametersSubmitted, this, [this](QString uuid, double bottomRadiu, double topRadiu, double height, const gp_Pnt &position)
	{
		this->onConeParamSubmitted(uuid, bottomRadiu, topRadiu, height, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryTorusWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	TorusDialog* widget = new TorusDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &TorusDialog::parametersSubmitted, this, [this](QString uuid, double majorRadius, double minorRadius, const gp_Pnt &position)
	{
		this->onTorusParamSubmitted(uuid, majorRadius, minorRadius, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryCylinderWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	CylinderDialog* widget = new CylinderDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &CylinderDialog::parametersSubmitted, this, [this](QString uuid, double radius, double height, const gp_Pnt &position)
	{
		this->onCylinderParamSubmitted(uuid, radius, height, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryHelixWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	HelixDialog* widget = new HelixDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &HelixDialog::parametersSubmitted, this, [this](QString uuid, double radius, double height, double pitch, const gp_Pnt& position)
	{
		this->onHelixParamSubmitted(uuid, radius, height, pitch,position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryCircleWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	CircleDialog* widget = new CircleDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &CircleDialog::parametersSubmitted, this, [this](QString uuid, double radius, const gp_Pnt &position)
	{
		gp_Dir dir(0, 0, 1);
		gp_Ax2 axis(position, dir);
		this->onCircleParamSubmitted(uuid, radius, axis);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometrySquareWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	SquareDialog* widget = new SquareDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &SquareDialog::parametersSubmitted, this, [this](QString uuid, double length, const gp_Pnt2d &position)
	{
		this->onSquareParamSubmitted(uuid, length, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryEllipseWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	EllipseDialog* widget = new EllipseDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &EllipseDialog::parametersSubmitted, this, [this](QString uuid, double majorRadiu, double minorRadius, const gp_Pnt2d& position)
	{
		this->onEllipseParamSubmitted(uuid, majorRadiu, minorRadius, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryPolygonWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	PolygonDialog* widget = new PolygonDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &PolygonDialog::parametersSubmitted, this, [this](QString uuid, double radius, int sides, const gp_Pnt2d& position)
	{
		this->onPolygonParamSubmitted(uuid, radius, sides, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryRectangleWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	RectangleDialog* widget = new RectangleDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
    connect(widget, &RectangleDialog::parametersSubmitted, this, [this](QString uuid, double length, double width, const gp_Pnt2d& position)
	{
        this->onRectangleParamSubmitted(uuid, length, width, position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryPointWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	PointDialog* widget = new PointDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &PointDialog::parametersSubmitted, this, [this](QString uuid, const gp_Pnt2d& position)
	{
		this->onPointParamSubmitted(uuid,position);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryArcWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	ArcDialog* widget = new ArcDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &ArcDialog::parametersSubmitted, this, [this](QString uuid, double radius, double startAngle, 
		double endAngle,const gp_Pnt2d& position)
	{
		gp_Pnt center3d = gp_Pnt(position.X(), position.Y(), 0);
		this->onArcParamSubmitted(uuid, radius, startAngle, endAngle, center3d);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryThreeBsrWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	Bezier3Dialog* widget = new Bezier3Dialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &Bezier3Dialog::parametersSubmitted, this, [this](QString uuid, std::vector<gp_Pnt2d> _vec)
	{
		this->onThreeBsrParamSubmitted(uuid, _vec);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryTwoBsrWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	Bezier2Dialog* widget = new Bezier2Dialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &Bezier2Dialog::parametersSubmitted, this, [this](QString uuid, std::vector<gp_Pnt2d> _vec)
	{
		this->onTwoBsrParamSubmitted(uuid, _vec);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryLineWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	LineDialog* widget = new LineDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &LineDialog::parametersSubmitted, this, [this](QString uuid, gp_Pnt2d startPoint, gp_Pnt2d endPoint)
	{
		this->onLineParamSubmitted(uuid, startPoint, endPoint);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryInterCurveWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	InterpolationDialog* widget = new InterpolationDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &InterpolationDialog::parametersSubmitted, this, [this](QString uuid, std::vector<gp_Pnt2d> _vec)
	{
		this->onInterCurveParamSubmitted(uuid, _vec);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryParamCurveWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	ParametricCurveDialog* widget = new ParametricCurveDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &ParametricCurveDialog::parametersSubmitted, this, [=](QString uuid, 
		int x, int y, int z,
		double tmin, double tmax, int numPoints)
	{
		auto xFunc = widget->parseFunction2(x);
		auto yFunc = widget->parseFunction2(y);
		auto zFunc = widget->parseFunction2(z);
		TopoDS_Shape* shape = GeometryCreator::getInstance()->createParametricCurve(xFunc, yFunc, zFunc, tmin, tmax, numPoints);
		if (!shape)return;
		afterCreateSimpleGeom(shape, uuid);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryParabolaEdgeWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	ParabolaDialog* widget = new ParabolaDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &ParabolaDialog::parametersSubmitted, this, [this](QString uuid, double focal, double min, double max, int points)
	{
		this->onParabolaParamSubmitted(uuid, focal,min,max,points);
	});
	return widget;
}

BasePage* PreProcessingWidget::newGeometryHyperbolaWidget(QString title)
{
	if (!m_geomNode)return nullptr;
	HyperbolaDialog* widget = new HyperbolaDialog(this);
	ProjectNode* node = new ProjectNode(title, QUuid::createUuid().toString(), (BasePage*)widget);
	m_geomNode->addChild(node);
	connect(widget, &HyperbolaDialog::parametersSubmitted, this, [this](QString uuid, double major, double focal, double min, double max, int points)
	{
		this->onHyperbolaParamSubmitted(uuid, major, focal, min, max, points);
	});
	return widget;
}

void PreProcessingWidget::onBoxParamSubmitted(const QString& uuid,double length, double width, double height, const gp_Pnt &position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createBox(length, width, height, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onSphereParamSubmitted(const QString& uuid, double radius, const gp_Pnt &position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createSphere(radius, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onConeParamSubmitted(const QString& uuid, double bottomRadiu, double topRadiu, double height, const gp_Pnt &position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createCone(bottomRadiu, topRadiu, height,position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onTorusParamSubmitted(const QString& uuid, double majorRadius, double minorRadius, const gp_Pnt& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createTorus(majorRadius, minorRadius, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onCylinderParamSubmitted(const QString& uuid, double radius, double height, const gp_Pnt& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createCylinder(radius, height, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onHelixParamSubmitted(const QString& uuid, double radius, double height,double pitch, const gp_Pnt& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createHelix(radius, height, pitch,position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onCircleParamSubmitted(const QString& uuid, double radius, const gp_Ax2& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createCircle(position,radius);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onSquareParamSubmitted(const QString& uuid, double length, const gp_Pnt2d& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createSquareWire(length, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onEllipseParamSubmitted(const QString& uuid, double majorRadiu, double minorRadius, const gp_Pnt2d& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createEllipse2d(majorRadiu, minorRadius, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onPolygonParamSubmitted(const QString& uuid, double radius, int sides, const gp_Pnt2d& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createPolygonWire(radius, sides, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onRectangleParamSubmitted(const QString& uuid, double length, double width, const gp_Pnt2d& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createRectangleWire(length, width, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onPointParamSubmitted(const QString& uuid, const gp_Pnt2d& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createPoint2d(position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onArcParamSubmitted(const QString& uuid, double radius , double startAngle, double endAngle,
	const gp_Pnt& position)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createArcEdge(radius, startAngle, endAngle, position);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onThreeBsrParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createBezier3Curve2d(_vec);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onTwoBsrParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createBezier2Curve2d(_vec);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onLineParamSubmitted(const QString& uuid, gp_Pnt2d startPoint, gp_Pnt2d endPoint)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createLine2d(startPoint, endPoint);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onInterCurveParamSubmitted(const QString& uuid, std::vector<gp_Pnt2d> _vec)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createInterpolationCurve2d(_vec);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onParabolaParamSubmitted(const QString& uuid, double focal, double min,double max, int points)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createParabolaEdge(focal, min, max, points);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onHyperbolaParamSubmitted(const QString& uuid, double major, double focal, double min, double max, int points)
{
	TopoDS_Shape* shape = GeometryCreator::getInstance()->createHyperbolaEdge(major,focal, min, max, points);
	if (!shape)return;
	afterCreateSimpleGeom(shape, uuid);
}

void PreProcessingWidget::onLightCheckBoxStateChanged(int state)
{
	if (!m_savedLight)
	{
		vtkLightCollection* lights = m_renderer->GetLights();
		int numberOfLights = lights->GetNumberOfItems();
		lights->InitTraversal(); // 初始化遍历
		m_savedLight = lights->GetNextItem(); // 获取第一个（默认）光源
	}
	if (state == Qt::Checked)
	{
	   if (m_savedLight)
       {
			m_renderer->AddLight(m_savedLight);
	   }
	}
	else
	{
		m_renderer->RemoveAllLights();
	}
	m_renderWindow->Render();
}

void PreProcessingWidget::onTransValueChanged(int value)
{
	for (auto const & actor : m_mainGeomActor)
	{
		if (actor)
		{
			float opacity = static_cast<float>(value) / 100.0f;
			actor->GetProperty()->SetOpacity(opacity);
		}
	}
	m_renderWindow->Render();
}

void PreProcessingWidget::newOccBoolenToolWidget()
{
    if (!m_geomNode)return;
    OccBooleanToolWidget* widget = new OccBooleanToolWidget(this);
    ProjectNode* node = new ProjectNode("boolen", QUuid::createUuid().toString(), (BasePage*)widget);
    m_geomNode->addChild(node);
    return;
}

bool PreProcessingWidget::hasDirectChildNamed(QTreeWidgetItem* parent, const QString& name)
{
	if (!parent) return false;  // 安全检查：父项为空返回 false

	for (int i = 0; i < parent->childCount(); ++i) {
		QTreeWidgetItem* child = parent->child(i);
		if (child->text(0) == name) {  // 假设名称在第 0 列
			return true;
		}
	}
	return false;
}



