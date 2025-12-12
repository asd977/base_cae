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
	EventManager::instance().registerHandler(PublicConstants::EVENT_LODA_COMPLETE, [this](const QVariantMap &data) {
		this->initRibbonActionConn();
		m_outputService = ServiceRegistry::instance().getService<OutputService>();
	});
	EventManager::instance().registerHandler(PublicConstants::EVENT_QUIT_APP, [this](const QVariantMap &data) {
		this->saveProject();
	});

    EventManager::instance().registerHandler(PublicConstants::ACTION_NEW_PROJECT, [this](const QVariantMap &data) {
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
//	connect(m_uiFrameService->getNewProAct(), &QAction::triggered, this, [this]() {this->newProject(); });
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

void ProjectManager::resetDefaultNodePointer()
{
	auto nodeMap = m_projectTreeWidget->getProjectRootNodeMap();
	if (nodeMap.size()<1)return;
	int size = nodeMap.values().size();
	QList<ProjectNode*> _nodeList = nodeMap.values();
	for (size_t i = 0; i < size; i++)
	{
		auto curNode = _nodeList[i];
		auto name = curNode->getName();
		if (name == "参数定义")
		{
			paramNode = curNode;
			continue;
		}
		else if(name == "材料")
		{
			materNode = curNode;
			continue;
		}
		else if (name == "几何")
		{
			gemoNode = curNode;
			continue;
		}
		else if (name == "网格")
		{
			gridNode = curNode;
			continue;
		}
		else if (name == "边界条件")
		{
			boundNode = curNode;
			continue;
		}
		else if (name == "物理场")
		{
			phyNode = curNode;
			continue;
		}
		else if (name == "求解")
		{
			solveNode = curNode;
			continue;
		}
		else if (name == "结果")
		{
			resNode = curNode;
			continue;
		}
		else if (name == "云图")
		{
			nephogramNode = curNode;
			continue;
		}
		else if (name == "矢量图")
		{
			vectorNode = curNode;
			continue;
		}
		else
		{
			continue;
		}
	}
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

    PublicVariables::instance().set(PublicConstants::VALUE_PROJECT_DIR, m_projectTempFolder,"project_dir");

    m_rootNode = new ProjectNode(m_projectName);
    m_projectTreeWidget->addTopLevelItem((QTreeWidgetItem*)m_rootNode);

    PostProcessingService *postProcessingService = ServiceRegistry::instance().getService<PostProcessingService>();
    postProcessingService->loadProject();

	PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	preProcessingService->loadProject();

	BoundaryConditionsService *boundaryConditionsService = ServiceRegistry::instance().getService<BoundaryConditionsService>();
	boundaryConditionsService->loadProject();

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

    qDebug()<<"111";
    m_projectEnginePath = m_projectPath + "/" + m_projectName + ".smproj";
    createProjectFilesFolder();
    qDebug()<<"222";
    m_rootNode = new ProjectNode(m_projectName);
    m_projectTreeWidget->addTopLevelItem((QTreeWidgetItem*)m_rootNode);
qDebug()<<"222";
    //参数定义
    GlobalParameterService *globalParameterService = ServiceRegistry::instance().getService<GlobalParameterService>();
    globalParameterService->newProject();
qDebug()<<"222";
    //材料
    MaterialManagerService *materialManagerService = ServiceRegistry::instance().getService<MaterialManagerService>();
    materialManagerService->newProject();
qDebug()<<"222";
    //前处理
    PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
    preProcessingService->newProject();
qDebug()<<"222";
    //物理场
    PhysicalFieldService *physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>();
    physicalFieldService->newProject();
//    //求解
//    MaterialManagerService *materialManagerService = ServiceRegistry::instance().getService<MaterialManagerService>();
//    materialManagerService->newProject();
qDebug()<<"222";
    //结果
    PostProcessingService *postProcessingService = ServiceRegistry::instance().getService<PostProcessingService>();
    postProcessingService->newProject();
qDebug()<<"222";
    //求解器
    SolverService *solverService = ServiceRegistry::instance().getService<SolverService>();
    solverService->newProject();
qDebug()<<"222";
	//代理模型
    // BoundaryConditionsService *boundaryConditionsService = ServiceRegistry::instance().getService<BoundaryConditionsService>();
    // boundaryConditionsService->newProject();
qDebug()<<"222";

//	const QString treeXmlPath = m_projectTempFolder + "projectTree.xml";
//	m_projectTreeWidget->setProjectTreeXmlPath(treeXmlPath);
//	//todo 清空项目树和堆栈窗口内容
    QTreeWidget* treeWidget = m_projectTreeWidget->m_treeWidget;
//	treeWidget->clear();
//	m_projectTreeWidget->clearStackedWidget(m_projectTreeWidget->m_stackWidget);
//	//todo 创建固定不变的树节点
//	m_projectTreeWidget->m_rootNode = new ProjectNode(m_projectName);
//	ProjectNode* rootNode = m_projectTreeWidget->m_rootNode;
//	paramNode = new ProjectNode("参数定义");
//	materNode = new ProjectNode("材料");
//	gemoNode = new ProjectNode("几何");
//	gridNode = new ProjectNode("网格");
//	boundNode = new ProjectNode("边界条件");
//	phyNode = new ProjectNode("物理场");
//	solveNode = new ProjectNode("求解");
//	resNode = new ProjectNode("结果");
//	nephogramNode = new ProjectNode("云图");
//	vectorNode = new ProjectNode("矢量图");
//	rootNode->addChild(paramNode);
//	rootNode->addChild(materNode);
//	rootNode->addChild(gemoNode);
//	rootNode->addChild(gridNode);
//	rootNode->addChild(boundNode);
//	rootNode->addChild(phyNode);
//	rootNode->addChild(solveNode);
//	rootNode->addChild(resNode);
//	resNode->addChild(nephogramNode);
//	resNode->addChild(vectorNode);
//	m_projectTreeWidget->addNodeToTree(treeWidget->invisibleRootItem(), rootNode, "");
//    connect(treeWidget, &QTreeWidget::currentItemChanged, m_projectTreeWidget, &ProjectTreeWidget::onCurItemChanged);
//    connect(treeWidget, &QTreeWidget::itemChanged, m_projectTreeWidget, &ProjectTreeWidget::onItemChanged);
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
	QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "几何");
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
    PublicVariables::instance().set(PublicConstants::VALUE_PROJECT_DIR, m_projectTempFolder,"project_dir");
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
	if (rootNodeType == "param")
	{
		if (!paramNode)return;
		judgeTreeNodeNameIsSame(paramNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		paramNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "参数定义");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "mater")
	{
		if (!materNode)return;
		judgeTreeNodeNameIsSame(materNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		materNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "材料");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "gemo")
	{
		if (!gemoNode)return;
		judgeTreeNodeNameIsSame(gemoNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		gemoNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "几何");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "grid")
	{
		if (!gridNode)return;
		QList<ProjectNode*> children = gridNode->getChildren();
		for (auto child : children)
		{
			gridNode->removeChild(child);
			QTreeWidgetItem*  item1 = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "二维曲面网格");
			QTreeWidgetItem*  item2 = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "三维实体网格");
			if (item1)
			{
				m_projectTreeWidget->deletePage(item1);
			}
			if (item2)
			{
				m_projectTreeWidget->deletePage(item2);
			}
		}
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		gridNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "网格");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "bound")
	{
		if (!boundNode)return;
		judgeTreeNodeNameIsSame(boundNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		boundNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "边界条件");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "phy")
	{
		if (!phyNode)return;
		judgeTreeNodeNameIsSame(phyNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		phyNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "物理场");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "solve")
	{
		if (!solveNode)return;
		judgeTreeNodeNameIsSame(solveNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		solveNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "求解");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "res")
	{
		if (!resNode)return;
		judgeTreeNodeNameIsSame(resNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		resNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "结果");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "nephogram")
	{
		if (!nephogramNode)return;
		judgeTreeNodeNameIsSame(nephogramNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		nephogramNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "云图");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
	else if (rootNodeType == "vector")
	{
		if (!vectorNode)return;
		judgeTreeNodeNameIsSame(vectorNode, nodeName);
		ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
		vectorNode->addChild(nodeA);
		QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "矢量图");
		if (!item) return;
		m_projectTreeWidget->addNodeToTree(item, nodeA, "");
	}
    else if (rootNodeType == "section")
    {
        if (!vectorNode)return;
        judgeTreeNodeNameIsSame(vectorNode, nodeName);
        ProjectNode* nodeA = new ProjectNode(nodeName, QUuid::createUuid().toString(), page);
        vectorNode->addChild(nodeA);
        QTreeWidgetItem*  item = m_projectTreeWidget->findItemByNameIterative(m_projectTreeWidget->m_treeWidget, "矢量图");
        if (!item) return;
        m_projectTreeWidget->addNodeToTree(item, nodeA, "");
    }
	else
	{
		return;
	}
}

void ProjectManager::onDeletePageSig(const QString & uuid)
{
	//// todo 页面发出信号 删除各自子模块的内容（注意：需要uuid匹配上才可以删除）
	//PreProcessingService *preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>();
	//preProcessingService->deletePage(uuid);
}

