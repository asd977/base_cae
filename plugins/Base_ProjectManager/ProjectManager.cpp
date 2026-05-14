#include "ProjectManager.h"

#include <PublicVariables.h>
#include <services/BoundaryConditionsService.h>
#include <services/GlobalParameterService.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhysicalFieldService.h>
#include <services/PostProcessingService.h>
#include <services/PreProcessingService.h>
#include <services/SolverService.h>
#include <services/SurrogateModelService.h>

#include <QBrush>
#include <QFile>
#include <QIcon>
#include <QMessageBox>
#include <QVariant>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace
{
ProjectNode* findNodeRecursive(ProjectNode* node, const QString& uuid)
{
    if (!node)
    {
        return nullptr;
    }
    QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
    if (nodeUuid.isEmpty())
    {
        nodeUuid = node->getUuid();
    }
    if (nodeUuid == uuid)
    {
        return node;
    }
    for (ProjectNode* child : node->getChildren())
    {
        if (auto* found = findNodeRecursive(child, uuid))
        {
            return found;
        }
    }
    return nullptr;
}
} // namespace

ProjectManager::ProjectManager()
{
    ServiceRegistry::instance().registerService<ProjectManagerService>(this);
    m_settingWidget = new SettingWidget;
    m_projectTreeWidget = new ProjectTreeWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    m_uiFrameService->addWidget(m_settingWidget, "设置");
    m_uiFrameService->addWidget(m_projectTreeWidget, "模型管理");
    QStackedWidget* stackWidget = m_settingWidget->getStackedWidget();
    stackWidget->setObjectName("projectSettingStackWidget");
    m_projectTreeWidget->setStackedWidget(stackWidget);

    this->initRibbonActionConn();

    connect(m_projectTreeWidget, &ProjectTreeWidget::deletePageSig, this,
            [this](QString uuid)
            {
                this->onDeletePageSig(uuid);
            });
    EventManager::instance().registerHandler(
        PublicConstants::kEventLoadComplete, this,
        [this](const QVariantMap& data)
        {
            qInfo() << "m_outputService";

            m_outputService = ServiceRegistry::instance().getService<OutputService>();
            qInfo() << "end m_outputService";
        });
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
    EventManager::instance().registerHandler(PublicConstants::kActionNewProject, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newProject();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionOpenProject, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->openProject();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionSaveProject, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->saveProject();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionExportCurrentDatabase, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->exportCurDatabase();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionEXitProject, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->exitProject();
                                             });

    EventManager::instance().registerHandler(PublicConstants::kEventQuitApp, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->saveProject();
                                             });
}

void ProjectManager::addTreeItem(ProjectNode* node)
{
    if (!m_rootNode || !node)
    {
        return;
    }
    node->setData(0, Qt::UserRole, QVariant::fromValue(node));
    m_rootNode->addChild(node);
}

void ProjectManager::setCurrentItem(ProjectNode* node)
{
    if (!m_projectTreeWidget || !node)
    {
        return;
    }
    if (auto* treeWidget = m_projectTreeWidget->getTreeWidget())
    {
        treeWidget->setCurrentItem(node);
    }
}

ProjectNode* ProjectManager::findNodeByUuid(const QString& uuid)
{
    if (!m_rootNode || uuid.isEmpty())
    {
        return nullptr;
    }
    return findNodeRecursive(m_rootNode, uuid);
}

void ProjectManager::setNodeDimmed(const QString& uuid, bool dimmed)
{
    ProjectNode* node = findNodeByUuid(uuid);
    if (!node)
    {
        return;
    }
    const QIcon icon = dimmed ? QIcon(":/resources/icon/geom-unused.svg")
                              : QIcon(":/resources/icon/geom-used.svg");
    node->setIcon(0, icon);
    node->setData(0, ProjectNode::RoleDimmed, dimmed);
    node->setForeground(0, QBrush(Qt::black));
}

void ProjectManager::openProject()
{
    // todo 打开工程文件
    QFileDialog dialog(nullptr, "选择工程文件");
    dialog.setNameFilter("Project Files (*.xml)");
    dialog.setFilter(QDir::Files);
    QString smprjo_path = "";
    if (dialog.exec() == QDialog::Accepted)
    {
        smprjo_path = dialog.selectedFiles().first();
    }
    if (smprjo_path == "")
        return;
    QFileInfo fileInfo(smprjo_path);
    if (!readProjectManifest(smprjo_path))
    {
        m_projectName = fileInfo.baseName();
    }
    if (m_projectName.isEmpty())
    {
        m_projectName = fileInfo.baseName();
    }

    QDir baseDir = fileInfo.dir();
    if (baseDir.dirName() == m_projectName)
    {
        baseDir.cdUp();
    }
    m_projectPath = baseDir.absolutePath();

    if (!createProjectFilesFolder(false))
    {
        m_outputService->outputLog("工程文件有误,打开失败");
        return;
    }
    // todo 获取工程文件里面的所有内容 读取各自模块存取的数据 注意:怎么存怎么取!!!
    m_projectEnginePath = QDir(m_projectTempFolder).filePath(m_projectName + ".xml");
    QStringList files /*= JlCompress::extractDir(smprjo_path, m_projectTempFolder)*/;

    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,
                                    "project_dir");
    updateIiidirDatForProject(m_projectTempFolder, false);

    m_projectTreeWidget->buildDefaultProjectTree(m_projectName);
    m_rootNode = m_projectTreeWidget->m_rootNode;
    m_projectTreeWidget->blockSignal(true);

    // 按“新建工程”一致的顺序恢复模块，保证树节点顺序稳定。
    //    if (auto* globalParameterService =
    //    ServiceRegistry::instance().getService<GlobalParameterService>())
    //    {
    //        globalParameterService->newProject();
    //    }

    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        preProcessingService->loadProject();
    }
    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshService->loadProject();
    }
    if (auto* materialManagerService =
            ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        materialManagerService->loadProject();
    }
    if (auto* physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>())
    {
        physicalFieldService->loadProject();
    }
    if (auto* boundaryConditionsService =
            ServiceRegistry::instance().getService<BoundaryConditionsService>())
    {
        boundaryConditionsService->loadProject();
    }
    if (auto* solverService = ServiceRegistry::instance().getService<SolverService>())
    {
        solverService->loadProject();
    }
    if (auto* postProcessingService =
            ServiceRegistry::instance().getService<PostProcessingService>())
    {
        postProcessingService->loadProject();
    }

    // 代理模型暂不参与打开恢复。
    // if (auto* surrogateService =
    // ServiceRegistry::instance().getService<SurrogateModelService>())
    // {
    //     surrogateService->loadProject();
    // }

    m_projectTreeWidget->blockSignal(false);
}

void ProjectManager::saveProject()
{
    if (m_projectTempFolder.isEmpty())
        return;

    if (auto* postProcessingService =
            ServiceRegistry::instance().getService<PostProcessingService>())
    {
        postProcessingService->saveProject();
    }

    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        preProcessingService->saveProject();
    }

    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshService->saveProject();
    }

    if (auto* materialManagerService =
            ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        materialManagerService->saveProject();
    }

    if (auto* boundaryConditionsService =
            ServiceRegistry::instance().getService<BoundaryConditionsService>())
    {
        boundaryConditionsService->saveProject();
    }

    if (auto* solverService = ServiceRegistry::instance().getService<SolverService>())
    {
        solverService->saveProject();
    }

    if (auto* physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>())
    {
        physicalFieldService->saveProject();
    }

    // 代理模型暂不参与工程保存。
    // if (auto* surrogateService =
    // ServiceRegistry::instance().getService<SurrogateModelService>())
    // {
    //     surrogateService->saveProject();
    // }

    writeProjectManifest();
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

    m_projectEnginePath =
        QDir(m_projectPath).filePath(m_projectName + "/" + m_projectName + ".xml");
    if (!createProjectFilesFolder(true))
    {
        return;
    }
    m_projectTreeWidget->buildDefaultProjectTree(m_projectName);
    m_rootNode = m_projectTreeWidget->m_rootNode;
    m_projectTreeWidget->blockSignal(true);
    if (auto* globalParameterService =
            ServiceRegistry::instance().getService<GlobalParameterService>())
    {
        globalParameterService->newProject();
    }
    if (auto* materialManagerService =
            ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        materialManagerService->newProject();
    }
    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        preProcessingService->newProject();
    }
    if (auto* meshModuleControllerService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshModuleControllerService->newProject();
    }

    if (auto* physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>())
    {
        physicalFieldService->newProject();
    }
    if (auto* boundaryConditionsService =
            ServiceRegistry::instance().getService<BoundaryConditionsService>())
    {
        boundaryConditionsService->newProject();
    }
    if (auto* solverService = ServiceRegistry::instance().getService<SolverService>())
    {
        solverService->newProject();
    }
    if (auto* postProcessingService =
            ServiceRegistry::instance().getService<PostProcessingService>())
    {
        postProcessingService->newProject();
    }
    updateIiidirDatForProject(m_projectTempFolder, false);
    // 代理模型暂不参与新建工程。
    // if (auto* surrogateModelService =
    // ServiceRegistry::instance().getService<SurrogateModelService>())
    // {
    //     surrogateModelService->newProject();
    // }

    m_projectTreeWidget->blockSignal(false);
    QTreeWidget* treeWidget = m_projectTreeWidget->m_treeWidget;
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
    return true;
}

void ProjectManager::onImportGeomAct()
{
    /*const QString type = "gemo";
    QString name = "几何导入";
    PreProcessingService *preProcessingService =
    ServiceRegistry::instance().getService<PreProcessingService>(); BasePage* page
    = preProcessingService->newGeometryImportWidget(); treeChildFactory(type,
    page, name);*/
}

void ProjectManager::onCuboidGemoAct()
{
    /*const QString type = "gemo";
    QString name = "长方体";
    PreProcessingService *preProcessingService =
    ServiceRegistry::instance().getService<PreProcessingService>(); BasePage* page
    = preProcessingService->newGeometryBoxWidget(); treeChildFactory(type, page,
    name);*/
}

void ProjectManager::onSphereGemoAct()
{
    /*const QString type = "gemo";
    QString name = "球体";
    PreProcessingService *preProcessingService =
    ServiceRegistry::instance().getService<PreProcessingService>(); BasePage* page
    = preProcessingService->newGeometrySphereWidget(); treeChildFactory(type,
    page, name);*/
}

void ProjectManager::updateIiidirDatForProject(const QString& projectDir, bool usePrepAsRoot) const
{
    const QString iiidirPath = QStringLiteral("D:/IIIDIR.DAT");
    const auto ensureTrailingSlash = [](QString path) -> QString
    {
        path = QDir::toNativeSeparators(path);
        if (!path.endsWith("\\") && !path.endsWith("/"))
            path += "\\";
        return path;
    };
    const QString baseDir = ensureTrailingSlash(projectDir);
    const QString prepDir = ensureTrailingSlash(QDir(projectDir).filePath("PREP"));
    const QString rootDir = usePrepAsRoot ? prepDir : baseDir;

    const QString content =
        QString("'%1'\n"
                "'1'\n"
                "'9.81 0.0 1'\n"
                "'D:\\NASGEWIN64\\'\n"
                "        0  IDEVICE\n"
                "           0  ILOG_GZKZR\n"
                "       0  ILOG_SOLVE\n"
                "'%2'\n"
                "           8   \n")
            .arg(rootDir, prepDir);

    QFile file(iiidirPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        qWarning() << "无法写入 IIIDIR.DAT:" << iiidirPath << file.errorString();
        return;
    }
    file.write(content.toUtf8());
    file.close();
}

void ProjectManager::onCurvedSurfaceGridAct()
{
    /*const QString type = "grid";
    QString name = "二维曲面网格";
    PreProcessingService *preProcessingService =
    ServiceRegistry::instance().getService<PreProcessingService>(); BasePage* page
    = preProcessingService->newTetrahedronGridWidget(); treeChildFactory(type,
    page, name);*/
}

void ProjectManager::onEntityGridAct()
{
    /*const QString type = "grid";
    QString name = "三维实体网格";
    PreProcessingService *preProcessingService =
    ServiceRegistry::instance().getService<PreProcessingService>(); BasePage* page
    = preProcessingService->newHexahedronGridWidget(); treeChildFactory(type,
    page, name);*/
}

bool ProjectManager::removeDir(QString dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return true;
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

bool ProjectManager::createProjectFilesFolder(bool askOverwrite)
{
    if (m_projectPath.isEmpty() || m_projectName.isEmpty())
    {
        return false;
    }

    const QString tempPath = QDir(m_projectPath).filePath(m_projectName);
    m_projectTempFolder = tempPath;
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,
                                    "project_dir");

    QDir dir(tempPath);
    if (dir.exists())
    {
        if (askOverwrite)
        {
            const auto ret =
                QMessageBox::question(nullptr, tr("项目目录已存在"),
                                      tr("目录已存在：%1\n是否覆盖原有项目文件？").arg(tempPath),
                                      QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
            {
                return false;
            }
            if (!removeDir(tempPath))
            {
                return false;
            }
        }
        else
        {
            if (!dir.mkpath(dir.filePath("PREP")))
            {
                return false;
            }
            return true;
        }
    }

    if (!dir.mkpath(tempPath))
    {
        return false;
    }

    if (!dir.mkpath(dir.filePath("PREP")))
    {
        return false;
    }

    return true;
}

bool ProjectManager::writeProjectManifest() const
{
    if (m_projectEnginePath.isEmpty())
        return false;

    QFile file(m_projectEnginePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("SmartCAEProject");
    xml.writeAttribute("version", "1.0");
    xml.writeTextElement("Name", m_projectName);

    xml.writeStartElement("ProjectFiles");
    xml.writeTextElement("PreProcessing", PublicConstants::kPreProcessingProjectFileName);
    xml.writeTextElement("PostProcessing", PublicConstants::kPostProcessingProjectFileName);
    xml.writeTextElement("Mesh", PublicConstants::kMeshProjectFileName);
    xml.writeTextElement("Material", PublicConstants::kMaterialProjectFileName);
    xml.writeTextElement("Solver", PublicConstants::kSolverProjectFileName);
    xml.writeTextElement("Surrogate", PublicConstants::kSurrogateProjectFileName);
    xml.writeEndElement();

    xml.writeStartElement("GlobalValues");
    const QVariant meshDim = PublicVariables::instance().get(PublicConstants::kMeshDimension);
    if (meshDim.isValid())
    {
        xml.writeStartElement("Value");
        xml.writeAttribute("key", PublicConstants::kMeshDimension);
        xml.writeAttribute("type", "int");
        xml.writeCharacters(meshDim.toString());
        xml.writeEndElement();
    }
    const QVariant postScalar = PublicVariables::instance().get(PublicConstants::kValuePostScalar);
    if (postScalar.isValid())
    {
        xml.writeStartElement("Value");
        xml.writeAttribute("key", PublicConstants::kValuePostScalar);
        xml.writeAttribute("type", "string");
        xml.writeCharacters(postScalar.toString());
        xml.writeEndElement();
    }
    xml.writeEndElement();

    xml.writeEndElement();
    xml.writeEndDocument();
    return true;
}

bool ProjectManager::readProjectManifest(const QString& manifestPath)
{
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QXmlStreamReader xml(&file);
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement())
        {
            if (xml.name() == "Name")
            {
                m_projectName = xml.readElementText().trimmed();
            }
            else if (xml.name() == "Value")
            {
                const auto attrs = xml.attributes();
                const QString key = attrs.value("key").toString();
                const QString type = attrs.value("type").toString();
                const QString text = xml.readElementText();
                QVariant val = text;
                if (type == "int")
                    val = text.toInt();
                else if (type == "double")
                    val = text.toDouble();
                const QString owner = PublicVariables::instance().ownerOf(key);
                if (!owner.isEmpty())
                {
                    PublicVariables::instance().set(key, val, owner);
                }
                else
                {
                    PublicVariables::instance().set(key, val, "project_dir");
                }
            }
        }
    }

    return !xml.hasError();
}

bool ProjectManager::saveAllProModelData(const QString& fileName)
{
    saveProjectTreeXml();
    bool isok /*= JlCompress::compressDir(fileName, m_projectTempFolder)*/;
    if (!isok)
    {
        qInfo() << "smproj save failed!!!";
    }
    return isok;
}

void ProjectManager::saveProjectTreeXml()
{
    // QString xmlPath = m_projectTreeWidget->getProjectTreeXmlPath();
    // QFile file(xmlPath);

    //
    // 参数定义
    //    GlobalParameterService *globalParameterService =
    //    ServiceRegistry::instance().getService<GlobalParameterService>();
    //    globalParameterService->saveProject();

    //    //材料
    //    MaterialManagerService *materialManagerService =
    //    ServiceRegistry::instance().getService<MaterialManagerService>();
    //    materialManagerService->saveProject();

    // m_projectTreeWidget->saveTreeStructure(file);
}

void ProjectManager::onDeletePageSig(const QString& uuid)
{
    //// todo 页面发出信号 删除各自子模块的内容（注意：需要uuid匹配上才可以删除）
    // PreProcessingService *preProcessingService =
    // ServiceRegistry::instance().getService<PreProcessingService>();
    // preProcessingService->deletePage(uuid);
}
