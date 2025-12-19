#include "ProjectManager.h"

#include <services/GlobalParameterService.h>
#include <services/MaterialManagerService.h>
#include <services/PostProcessingService.h>
#include <services/PreProcessingService.h>
#include <services/SurrogateModelService.h>
#include <services/PhysicalFieldService.h>
#include <services/BoundaryConditionsService.h>
#include <services/SolverService.h>

#include <PublicVariables.h>
#include <QHash>
#include <QPair>

ProjectManager::ProjectManager()
{
	ServiceRegistry::instance().registerService<ProjectManagerService>(this);
    m_settingWidget = new SettingWidget;
    m_projectTreeWidget =  new ProjectTreeWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
	m_uiFrameService->addWidget(m_settingWidget,"设置");
	m_uiFrameService->addWidget(m_projectTreeWidget,"模型管理");
	QStackedWidget* stackWidget = m_settingWidget->getStackedWidget();
	m_projectTreeWidget->setStackedWidget(stackWidget);
	EventManager::instance().registerHandler(PublicConstants::kEventLoadComplete, [this](const QVariantMap &data) {
		this->initRibbonActionConn();
		m_outputService = ServiceRegistry::instance().getService<OutputService>();
	});
	EventManager::instance().registerHandler(PublicConstants::kEventQuitApp, [this](const QVariantMap &data) {
		this->saveProject();
	});

    EventManager::instance().registerHandler(PublicConstants::kActionNewProject, [this](const QVariantMap &data) {
        this->newProject();

    });

	connect(m_projectTreeWidget, &ProjectTreeWidget::deletePageSig, this, [this](QString uuid) {this->onDeletePageSig(uuid); });
}

ProjectManager::~ProjectManager()
{
	if (m_projectTreeWidget)
	{
		delete m_projectTreeWidget;
		m_projectTreeWidget = nullptr;
	}
	if (m_settingWidget)
	{
		delete m_settingWidget;
		m_settingWidget = nullptr;
	}
}

void ProjectManager::initRibbonActionConn()
{
#pragma region 文件下的action槽函数
        connect(m_uiFrameService->getNewProAct(), &QAction::triggered, this, [this]() {this->newProject(); });
	connect(m_uiFrameService->getOpenProAct(), &QAction::triggered, this, [this]() {this->openProject(); });
	connect(m_uiFrameService->getSaveProAct(), &QAction::triggered, this, [this]() {this->saveProject(); });
	connect(m_uiFrameService->getExportCurDatabaseAct(), &QAction::triggered, this, [this]() {this->exportCurDatabase(); });
	connect(m_uiFrameService->getExitAct(), &QAction::triggered, this, [this]() {this->exitProject(); });
#pragma endregion

#pragma region 文件下的action槽函数
    connect(m_uiFrameService->getParamAct(), &QAction::triggered, this, [this]() {this->onParamAct(); });
#pragma endregion

#pragma region 几何下的action槽函数
	//connect(m_uiFrameService->getImportGemoAct(), &QAction::triggered, this, [this]() {this->onImportGeomAct(); });
	//connect(m_uiFrameService->getCuboidGemoAct(), &QAction::triggered, this, [this]() {this->onCuboidGemoAct(); });
	//connect(m_uiFrameService->getSphereGemoAct(), &QAction::triggered, this, [this]() {this->onSphereGemoAct(); });
#pragma endregion 
#pragma region 网格下的action槽函数
	//connect(m_uiFrameService->getCurvedSurfaceGridAct(), &QAction::triggered, this, [this]() {this->onCurvedSurfaceGridAct(); });
	//connect(m_uiFrameService->getEntityGridAct(), &QAction::triggered, this, [this]() {this->onEntityGridAct(); });
#pragma endregion 

}

void ProjectManager::addTreeItem(ProjectNode *node)
{
    m_rootNode->addChild(node);
}

ProjectNode* ProjectManager::nodeByKey(const QString& key) const
{
        if (!m_projectTreeWidget)
        {
                return nullptr;
        }
        return m_projectTreeWidget->nodeByKey(key);
}

void ProjectManager::resetDefaultNodePointer()
{
	paramNode = m_projectTreeWidget->nodeByKey("parameters");
	materNode = m_projectTreeWidget->nodeByKey("materials");
	gemoNode = m_projectTreeWidget->nodeByKey("geometry");
	gridNode = m_projectTreeWidget->nodeByKey("mesh");
	boundNode = m_projectTreeWidget->nodeByKey("boundary");
	phyNode = m_projectTreeWidget->nodeByKey("physics");
	solveNode = m_projectTreeWidget->nodeByKey("solver");
	resNode = m_projectTreeWidget->nodeByKey("results");
	nephogramNode = m_projectTreeWidget->nodeByKey("result_cloud");
	vectorNode = m_projectTreeWidget->nodeByKey("result_vector");
}

void ProjectManager::judgeTreeNodeNameIsSame(const ProjectNode * parentNode, QString & originName)
{
	QList<ProjectNode*> nodeList = parentNode->getChildren();
	QSet<QString> nameSet;
	for (auto node : nodeList) 
	{
		nameSet.insert(node->getName());
	}
	if (!nameSet.contains(originName)) 
	{
		return;
	}
	int counter = 1;
	QString newName;
	do {
		newName = originName + QString::number(counter++);
	} while (nameSet.contains(newName)); 
	originName = newName; 
}

void ProjectManager::openProject()
{
    // todo 打开工程文件
    QFileDialog dialog(nullptr, "选择工程文件");
    dialog.setNameFilter("Project Files (*.smproj)");
    dialog.setFilter(QDir::Files);
    QString smprjo_path = "";
    if (dialog.exec() == QDialog::Accepted)
    {
        smprjo_path = dialog.selectedFiles().first();
    }
    if (smprjo_path == "") return;
    QFileInfo fileInfo(smprjo_path);
    m_projectName = fileInfo.baseName();
    if (!createProjectFilesFolder())
    {
        m_outputService->outputLog("工程文件有误,打开失败");
        return;
    }
    //todo 获取工程文件里面的所有内容 读取各自模块存取的数据 注意:怎么存怎么取!!!
    m_projectEnginePath = smprjo_path;
    QStringList files /*= JlCompress::extractDir(smprjo_path, m_projectTempFolder)*/;

    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,"project_dir");

	m_projectTreeWidget->buildDefaultProjectTree(m_projectName);
	m_rootNode = m_projectTreeWidget->m_rootNode;
	resetDefaultNodePointer();

    PostProcessingService *postProcessingService = ServiceRegistry::instance().getService<PostProcessingService>();
    postProcessingService->loadProject();

        PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
        preProcessingService->loadProject();

        BoundaryConditionsService *boundaryConditionsService = ServiceRegistry::instance().getService<BoundaryConditionsService>();
        boundaryConditionsService->loadProject();

        SurrogateModelService *surrogateModelService = ServiceRegistry::instance().getService<SurrogateModelService>();
        if (surrogateModelService)
        {
                surrogateModelService->loadProject();
        }

    //todo 读取项目模型树的数据
//	loadProTreeData();
//	resetDefaultNodePointer();
    //todo 读取其他模块的数据

}

void ProjectManager::saveProject()
{
	if (m_projectEnginePath =="")return;
//	saveAllProModelData(m_projectEnginePath);

    PostProcessingService *postProcessingService = ServiceRegistry::instance().getService<PostProcessingService>();
    postProcessingService->saveProject();

	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	preProcessingService->saveProject();

	BoundaryConditionsService *boundaryConditionsService = ServiceRegistry::instance().getService<BoundaryConditionsService>();
	boundaryConditionsService->saveProject();

    bool isok/* = JlCompress::compressDir(m_projectEnginePath, m_projectTempFolder)*/;
    if (!isok)
    {
        qDebug() << "smproj save failed!!!";
    }
}

void ProjectManager::newProject()
{
    ProjectSetupDialog dialog;
    if (dialog.exec() == QDialog::Accepted)
    {
        m_projectName = dialog.projectName();
        m_projectPath = dialog.projectPath();
    }
    if (m_projectName == "" || m_projectPath == "")
    {
        return;
    }

    m_projectEnginePath = m_projectPath + "/" + m_projectName + ".smproj";
    createProjectFilesFolder();
    m_projectTreeWidget->buildDefaultProjectTree(m_projectName);
    m_rootNode = m_projectTreeWidget->m_rootNode;
    resetDefaultNodePointer();

    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
            preProcessingService->newProject();
    }

    if (auto* postProcessingService = ServiceRegistry::instance().getService<PostProcessingService>())
    {
            postProcessingService->newProject();
    }

    if (auto* surrogateModelService = ServiceRegistry::instance().getService<SurrogateModelService>())
    {
            surrogateModelService->newProject();
    }
}

void ProjectManager::exportCurDatabase()
{
}

void ProjectManager::exitProject()
{
	saveProject();
	exit(0);
}

bool ProjectManager::isHaveProject()
{
	if (!m_projectTreeWidget) return false;
	QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "几何建模");
	if (!item) return false;
	return true;
}

void ProjectManager::onParamAct()
{
    const QString type = "param";
    QString name = "全局参数1";
    GlobalParameterService *globalParameterService = ServiceRegistry::instance().getService<GlobalParameterService>();
    BasePage* page = globalParameterService->newGlobalParameterSettingWidget();
    treeChildFactory(type, page,name);
}

void ProjectManager::onImportGeomAct()
{
	/*const QString type = "gemo";
	QString name = "几何导入";
	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	BasePage* page = preProcessingService->newGeometryImportWidget();
	treeChildFactory(type, page, name);*/
}

void ProjectManager::onCuboidGemoAct()
{
	/*const QString type = "gemo";
	QString name = "长方体";
	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	BasePage* page = preProcessingService->newGeometryBoxWidget();
	treeChildFactory(type, page, name);*/
}

void ProjectManager::onSphereGemoAct()
{
	/*const QString type = "gemo";
	QString name = "球体";
	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	BasePage* page = preProcessingService->newGeometrySphereWidget();
	treeChildFactory(type, page, name);*/
}

void ProjectManager::onCurvedSurfaceGridAct()
{
	/*const QString type = "grid";
	QString name = "二维曲面网格";
	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	BasePage* page = preProcessingService->newTetrahedronGridWidget();
	treeChildFactory(type, page, name);*/
}

void ProjectManager::onEntityGridAct()
{
	/*const QString type = "grid";
	QString name = "三维实体网格";
	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	BasePage* page = preProcessingService->newHexahedronGridWidget();
	treeChildFactory(type, page, name);*/
}

bool ProjectManager::removeDir(QString dirPath)
{
	QDir dir(dirPath);
	if (!dir.exists()) return true;
	dir.setFilter(QDir::AllEntries | QDir::NoDotAndDotDot); 
	QFileInfoList fileList = dir.entryInfoList(); 
	for (QFileInfo file : fileList)
	{
		if (file.isFile())
		{ 
			file.dir().remove(file.fileName());
		}
		else
		{ 
			removeDir(file.absoluteFilePath());
		}
	}
	return dir.rmpath(dir.absolutePath());
}

bool ProjectManager::createProjectFilesFolder()
{
	QString	exelPath = QCoreApplication::applicationDirPath();
	const QString tempPath = exelPath + "/../ProjectFolder/";
    m_projectTempFolder = tempPath;
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,"project_dir");
	removeDir(tempPath);
	QDir dir = QDir(tempPath);
	if (!dir.exists())
	{
		bool flag = dir.mkpath(tempPath);
	}
	return true;
}

bool ProjectManager::saveAllProModelData(const QString& fileName)
{
	saveProjectTreeXml();
    bool isok /*= JlCompress::compressDir(fileName, m_projectTempFolder)*/;
	if (!isok)
	{
		qDebug() << "smproj save failed!!!";
	}
	return isok;
}

void ProjectManager::saveProjectTreeXml()
{
	QString xmlPath = m_projectTreeWidget->getProjectTreeXmlPath();
	QFile file(xmlPath);

    //
    //参数定义
//    GlobalParameterService *globalParameterService = ServiceRegistry::instance().getService<GlobalParameterService>();
//    globalParameterService->saveProject();

//    //材料
//    MaterialManagerService *materialManagerService = ServiceRegistry::instance().getService<MaterialManagerService>();
//    materialManagerService->saveProject();

	m_projectTreeWidget->saveTreeStructure(file);
}

void ProjectManager::loadProTreeData()
{
	const QString treeXmlPath = m_projectTempFolder + "projectTree.xml";
	QFile file(treeXmlPath);
	if (file.exists())
	{
		QTreeWidget* treeWidget = m_projectTreeWidget->m_treeWidget;
		treeWidget->clear();
		m_projectTreeWidget->clearStackedWidget(m_projectTreeWidget->m_stackWidget);
		m_projectTreeWidget->setProjectTreeXmlPath(treeXmlPath);
		m_projectTreeWidget->loadTreeStructure(file);
		ProjectNode* rootNode = m_projectTreeWidget->m_rootNode;
		m_projectTreeWidget->addNodeToTree(treeWidget->invisibleRootItem(), rootNode, "");
		connect(treeWidget, &QTreeWidget::currentItemChanged, m_projectTreeWidget, &ProjectTreeWidget::onCurItemChanged);
		connect(treeWidget, &QTreeWidget::itemChanged, m_projectTreeWidget, &ProjectTreeWidget::onItemChanged);
	}
}

void ProjectManager::treeChildFactory(const QString& rootNodeType, BasePage* page, QString& nodeName)
{
	QHash<QString, QPair<ProjectNode*, QString>> targets;
	targets.insert("param", { paramNode, ":/resources/menu/Parameter.png" });
	targets.insert("mater", { materNode, ":/resources/menu/Block.png" });
	targets.insert("gemo", { gemoNode, ":/resources/icon/geomImport.png" });
	targets.insert("grid", { gridNode, ":/resources/icon/mesh.png" });
	targets.insert("bound", { boundNode, ":/resources/menu/boundary_face.png" });
	targets.insert("phy", { phyNode, ":/resources/icon/bound.png" });
	targets.insert("solve", { solveNode, ":/resources/icon/report.png" });
	targets.insert("res", { resNode, ":/resources/menu/Nephogram.png" });
	targets.insert("nephogram", { nephogramNode, ":/resources/menu/Nephogram.png" });
	targets.insert("vector", { vectorNode, ":/resources/menu/Vector.png" });
	targets.insert("section", { vectorNode, ":/resources/menu/Slice.png" });

	if (!targets.contains(rootNodeType))
	{
		return;
	}
	auto target = targets.value(rootNodeType);
	if (!target.first)
	{
		return;
	}
	if (rootNodeType == "grid")
	{
		const QList<ProjectNode*> children = target.first->getChildren();
		for (auto child : children)
		{
			target.first->removeChild(child);
			m_projectTreeWidget->deleteProjectNode(child);
		}
	}
	judgeTreeNodeNameIsSame(target.first, nodeName);
	m_projectTreeWidget->addLeaf(target.first, nodeName, target.second, page);
}

void ProjectManager::onDeletePageSig(const QString & uuid)
{
	//// todo 页面发出信号 删除各自子模块的内容（注意：需要uuid匹配上才可以删除）
	//PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	//preProcessingService->deletePage(uuid);
}
