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
#include <QDirIterator>
#include <QMessageBox>
#include <QTimer>
#include <QVariant>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace
{
bool mergeDirectoryMissingFiles(const QString& sourcePath, const QString& targetPath)
{
    const QDir source(sourcePath);
    if (!source.exists())
        return true;
    if (!QDir().mkpath(targetPath))
        return false;

    QDirIterator iterator(sourcePath, QDir::AllEntries | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QFileInfo sourceInfo(iterator.next());
        const QString relative = source.relativeFilePath(sourceInfo.absoluteFilePath());
        const QString target = QDir(targetPath).filePath(relative);
        if (sourceInfo.isDir())
        {
            if (!QDir().mkpath(target))
                return false;
        }
        else if (!QFileInfo::exists(target))
        {
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) ||
                !QFile::copy(sourceInfo.absoluteFilePath(), target))
                return false;
        }
    }
    return true;
}

bool removeControlFileUsingLegacyInput(const QString& filePath)
{
    QFile file(filePath);
    if (!file.exists())
        return false;
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    content.replace('\\', '/');
    if (!content.contains(QStringLiteral("/PREP/"), Qt::CaseInsensitive))
        return false;

    file.close();
    return QFile::remove(filePath);
}

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
            Q_UNUSED(data);
            qInfo() << "m_outputService";

            m_outputService = ServiceRegistry::instance().getService<OutputService>();
            qInfo() << "end m_outputService";

            QTimer::singleShot(0, this,
                               [this]()
                               {
                                   this->newProject();
                               });
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
    if (dimmed)
    {
        node->setForeground(0, QBrush(QColor("#8a95a5")));
    }
    else
    {
        node->setData(0, Qt::ForegroundRole, QVariant());
    }
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

    // The selected XML defines the project root. The enclosing folder may be renamed
    // independently from the project display name stored in the manifest.
    m_projectTempFolder = fileInfo.absolutePath();
    m_projectPath = m_projectTempFolder;
    PublicVariables::instance().set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,
                                    "project_dir");
    QDir projectDir(m_projectTempFolder);
    const bool foldersReady = projectDir.exists() &&
                              projectDir.mkpath(QString::fromUtf8(
                                  PublicConstants::kProjectInputDirectoryName));
    if (!foldersReady || !migrateLegacyProjectDataFiles() || !migrateLegacySolverFiles())
    {
        m_outputService->outputLog("工程文件有误,打开失败");
        return;
    }
    // todo 获取工程文件里面的所有内容 读取各自模块存取的数据 注意:怎么存怎么取!!!
    m_projectEnginePath = fileInfo.absoluteFilePath();
    QStringList files /*= JlCompress::extractDir(smprjo_path, m_projectTempFolder)*/;

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

    if (!QDir(m_projectTempFolder).mkpath(
            QString::fromUtf8(PublicConstants::kProjectDataDirectoryName)))
    {
        qWarning() << "failed to create project data directory";
        return;
    }

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
    migrateLegacyProjectDataFiles();
    removeLegacyRootSolverFiles();
}

void ProjectManager::newProject()
{
    ProjectSetupDialog dialog;
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    if (dialog.shouldOpenProject())
    {
        openProject();
        return;
    }

    m_projectName = dialog.projectName();
    m_projectPath = dialog.projectPath();
    if (m_projectName == "" || m_projectPath == "")
    {
        return;
    }

    m_projectEnginePath =
        QDir(QDir(m_projectPath).filePath(m_projectName)).filePath(m_projectName + ".xml");
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

    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        preProcessingService->newProject();
    }
    if (auto* meshModuleControllerService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshModuleControllerService->newProject();
    }

    if (auto* materialManagerService =
        ServiceRegistry::instance().getService<MaterialManagerService>())
    {
        materialManagerService->newProject();
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

void ProjectManager::updateIiidirDatForProject(const QString& projectDir, bool useInputAsRoot) const
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
    const QString inputDir = ensureTrailingSlash(QDir(projectDir).filePath(
        QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)));
    const QString rootDir = useInputAsRoot ? inputDir : baseDir;

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
            .arg(rootDir, inputDir);

    QFile file(iiidirPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        qWarning() << "无法写入 IIIDIR.DAT:" << iiidirPath << file.errorString();
        return;
    }
    file.write(content.toLocal8Bit());
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
            if (!dir.mkpath(dir.filePath(
                    QString::fromUtf8(PublicConstants::kProjectInputDirectoryName))))
            {
                return false;
            }
            return migrateLegacyProjectDataFiles() && migrateLegacySolverFiles();
        }
    }

    if (!dir.mkpath(tempPath))
    {
        return false;
    }

    if (!dir.mkpath(dir.filePath(
            QString::fromUtf8(PublicConstants::kProjectInputDirectoryName))))
    {
        return false;
    }

    return migrateLegacyProjectDataFiles() && migrateLegacySolverFiles();
}

bool ProjectManager::migrateLegacySolverFiles() const
{
    const QDir project(m_projectTempFolder);
    const QString legacyName = QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName);
    const QString inputName = QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName);
    const QString projectInput = project.filePath(
        QString::fromUtf8(PublicConstants::kProjectInputDirectoryName));
    const QString legacyProjectInput = project.filePath(legacyName);

    if (!mergeDirectoryMissingFiles(legacyProjectInput, projectInput))
        return false;
    if (QDir(legacyProjectInput).exists() && !QDir(legacyProjectInput).removeRecursively())
        return false;

    const QString manifestName =
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName);
    const QFileInfoList directories =
        project.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& directory : directories)
    {
        QDir workspace(directory.absoluteFilePath());
        if (!workspace.exists(manifestName))
            continue;

        const QString legacyInput = workspace.filePath(legacyName);
        const QString analysisInput = workspace.filePath(inputName);
        if (!mergeDirectoryMissingFiles(legacyInput, analysisInput))
            return false;
        if (QDir(legacyInput).exists() && !QDir(legacyInput).removeRecursively())
            return false;

        const QStringList mainControlFiles = {
            QStringLiteral("IFILL.DAT"), QStringLiteral("ISEEP.DAT"),
            QStringLiteral("ICAVE.DAT"), QStringLiteral("ISLOP.DAT"),
            QStringLiteral("IGEOS.DAT"), QStringLiteral("IMID_GEOS.DAT")};
        bool removedLegacyControl = false;
        for (const QString& name : mainControlFiles)
            removedLegacyControl =
                removeControlFileUsingLegacyInput(workspace.filePath(name)) ||
                removedLegacyControl;

        if (removedLegacyControl)
        {
            QFile::remove(workspace.filePath(QStringLiteral("MESG_IAUTO.DAT")));
            QFile::remove(workspace.filePath(QStringLiteral("MESG_ITASK.DAT")));
            QFile::remove(workspace.filePath(QStringLiteral("MESG064.DAT")));
        }
    }

    removeLegacyRootSolverFiles();
    return QDir().mkpath(projectInput);
}

void ProjectManager::removeLegacyRootSolverFiles() const
{
    const QDir project(m_projectTempFolder);
    const QStringList names = {
        QStringLiteral("IFILL.DAT"),      QStringLiteral("ISEEP.DAT"),
        QStringLiteral("ICAVE.DAT"),      QStringLiteral("ISLOP.DAT"),
        QStringLiteral("IGEOS.DAT"),      QStringLiteral("IMID_GEOS.DAT"),
        QStringLiteral("MESG_IAUTO.DAT"), QStringLiteral("MESG_ITASK.DAT"),
        QStringLiteral("MESG064.DAT")};
    for (const QString& name : names)
        QFile::remove(project.filePath(name));
}

bool ProjectManager::migrateLegacyProjectDataFiles() const
{
    const QDir project(m_projectTempFolder);
    const QStringList relativePaths = {
        QString::fromUtf8(PublicConstants::kPostProcessingProjectFileName),
        QString::fromUtf8(PublicConstants::kPreProcessingProjectFileName),
        QString::fromUtf8(PublicConstants::kMeshProjectFileName),
        QString::fromUtf8(PublicConstants::kMaterialProjectFileName),
        QString::fromUtf8(PublicConstants::kPhysicalFieldProjectFileName),
        QString::fromUtf8(PublicConstants::kMeshSnapshotFileName),
        QString::fromUtf8(PublicConstants::kMeshMapFileName),
        QString::fromUtf8(PublicConstants::kSolverProjectFileName),
        QString::fromUtf8(PublicConstants::kSurrogateProjectFileName)};

    for (const QString& relativePath : relativePaths)
    {
        const QString targetPath = project.filePath(relativePath);
        const QString sourcePath = project.filePath(QFileInfo(relativePath).fileName());
        if (!QFileInfo::exists(sourcePath))
            continue;
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath()))
            return false;
        if (QFileInfo::exists(targetPath))
        {
            // Once the centralized file exists it is authoritative. A root-level copy
            // can only be residue written by an older plugin and must not replace it.
            if (!QFile::remove(sourcePath))
                return false;
            continue;
        }
        if (!QFile::rename(sourcePath, targetPath))
        {
            if (!QFile::copy(sourcePath, targetPath) || !QFile::remove(sourcePath))
                return false;
        }
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
    xml.writeTextElement("PreProcessing", QString::fromUtf8(
        PublicConstants::kPreProcessingProjectFileName));
    xml.writeTextElement("PostProcessing", QString::fromUtf8(
        PublicConstants::kPostProcessingProjectFileName));
    xml.writeTextElement("Mesh", QString::fromUtf8(PublicConstants::kMeshProjectFileName));
    xml.writeTextElement("Material", QString::fromUtf8(
        PublicConstants::kMaterialProjectFileName));
    xml.writeTextElement("PhysicalField", QString::fromUtf8(
        PublicConstants::kPhysicalFieldProjectFileName));
    xml.writeTextElement("Solver", QString::fromUtf8(PublicConstants::kSolverProjectFileName));
    xml.writeTextElement("Surrogate", QString::fromUtf8(
        PublicConstants::kSurrogateProjectFileName));
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
            if (xml.name().toString() == "Name")
            {
                m_projectName = xml.readElementText().trimmed();
            }
            else if (xml.name().toString() == "Value")
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
