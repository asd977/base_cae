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
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QDirIterator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QSettings>
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
    connect(m_projectTreeWidget, &ProjectTreeWidget::projectRenameRequested, this,
            [this](const QString& requestedName)
            {
                const QString newName = requestedName.trimmed();
                if (!m_rootNode || newName.isEmpty() || newName == m_projectName)
                    return;

                const QString oldName = m_projectName;
                m_projectName = newName;
                m_rootNode->setName(newName);
                m_rootNode->setText(0, newName);

                // 根节点的 DashboardPage 可能已经创建，立即同步标题，避免必须切换页面
                // 才看到新名称。
                if (BasePage* page = m_rootNode->getPage())
                {
                    if (QWidget* widget = page->getWidget())
                    {
                        if (QLabel* header = widget->findChild<QLabel*>(
                                QStringLiteral("pageHeaderLabel")))
                        {
                            header->setText(newName + QStringLiteral(" - 管理面板"));
                        }
                    }
                }

                // 仅更新 manifest 中的 <Name>，不重命名工程目录和工程 .xml 文件。
                // 如果写入失败则回滚 UI 和内存状态，避免树名称与磁盘不一致。
                if (!writeProjectManifest())
                {
                    m_projectName = oldName;
                    m_rootNode->setName(oldName);
                    m_rootNode->setText(0, oldName);
                    if (BasePage* page = m_rootNode->getPage())
                    {
                        if (QWidget* widget = page->getWidget())
                        {
                            if (QLabel* header = widget->findChild<QLabel*>(
                                    QStringLiteral("pageHeaderLabel")))
                            {
                                header->setText(oldName + QStringLiteral(" - 管理面板"));
                            }
                        }
                    }
                    QMessageBox::warning(nullptr, QStringLiteral("重命名工程"),
                                         QStringLiteral("工程名称保存失败，已恢复原名称。"));
                    return;
                }

                if (m_outputService)
                {
                    m_outputService->outputLog(
                        QStringLiteral("工程名称已修改为：%1（工程路径保持不变）").arg(newName));
                }
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

void ProjectManager::closeCurrentProjectModules()
{
    // Modules keep direct pointers to their tree roots. Release them while the
    // old tree is still valid; clearing the tree first leaves dangling roots.
    if (auto* postProcessingService =
            ServiceRegistry::instance().getService<PostProcessingService>())
    {
        postProcessingService->closeProject();
    }
    if (auto* solverService = ServiceRegistry::instance().getService<SolverService>())
    {
        solverService->closeProject();
    }
    if (auto* boundaryService =
            ServiceRegistry::instance().getService<BoundaryConditionsService>())
    {
        boundaryService->closeProject();
    }
    if (auto* physicalFieldService =
            ServiceRegistry::instance().getService<PhysicalFieldService>())
    {
        physicalFieldService->closeProject();
    }
    if (auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        meshService->closeProject();
    }
    if (auto* preProcessingService = ServiceRegistry::instance().getService<PreProcessingService>())
    {
        preProcessingService->closeProject();
    }
}

bool ProjectManager::confirmProjectSwitch(bool* currentProjectSaved)
{
    if (currentProjectSaved)
        *currentProjectSaved = true;
    if (!m_rootNode || m_projectTempFolder.isEmpty())
        return true;

    QMessageBox messageBox(QMessageBox::Question, QStringLiteral("打开其他工程"),
                           QStringLiteral("当前工程将被关闭，是否先保存当前修改？"),
                           QMessageBox::NoButton);
    auto* saveButton = messageBox.addButton(QStringLiteral("保存并打开"),
                                            QMessageBox::AcceptRole);
    auto* discardButton = messageBox.addButton(QStringLiteral("不保存直接打开"),
                                               QMessageBox::DestructiveRole);
    auto* cancelButton = messageBox.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
    messageBox.setDefaultButton(saveButton);
    messageBox.exec();

    if (messageBox.clickedButton() == cancelButton)
        return false;
    if (messageBox.clickedButton() == discardButton)
    {
        if (currentProjectSaved)
            *currentProjectSaved = false;
        return true;
    }

    QStringList failedModules;
    if (!saveCurrentProject(&failedModules))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("当前工程保存失败"),
            QStringLiteral("以下内容未能保存，已取消工程切换：\n%1")
                .arg(failedModules.join(QStringLiteral("、"))));
        return false;
    }
    return true;
}

bool ProjectManager::saveCurrentProject(QStringList* failedModules)
{
    QStringList failures;
    if (m_projectTempFolder.isEmpty() ||
        !QDir(m_projectTempFolder).mkpath(
            QString::fromUtf8(PublicConstants::kProjectDataDirectoryName)))
    {
        failures << QStringLiteral("工程目录");
    }
    else
    {
        if (auto* service = ServiceRegistry::instance().getService<PostProcessingService>();
            service && !service->saveProject())
            failures << QStringLiteral("后处理");
        if (auto* service = ServiceRegistry::instance().getService<PreProcessingService>();
            service && !service->saveProject())
            failures << QStringLiteral("几何");
        if (auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            service && !service->saveProject())
            failures << QStringLiteral("网格");
        if (auto* service = ServiceRegistry::instance().getService<MaterialManagerService>();
            service && !service->saveProject())
            failures << QStringLiteral("材料");
        if (auto* service = ServiceRegistry::instance().getService<BoundaryConditionsService>();
            service && !service->saveProject())
            failures << QStringLiteral("边界条件");
        if (auto* service = ServiceRegistry::instance().getService<SolverService>();
            service && !service->saveProject())
            failures << QStringLiteral("计算分析");
        if (auto* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
            service && !service->saveProject())
            failures << QStringLiteral("约束与荷载");
        if (!writeProjectManifest())
            failures << QStringLiteral("工程索引");
        if (!migrateLegacyProjectDataFiles(m_projectTempFolder))
            failures << QStringLiteral("工程数据整理");
        removeLegacyRootSolverFiles(m_projectTempFolder);
    }

    failures.removeDuplicates();
    if (failedModules)
        *failedModules = failures;
    return failures.isEmpty();
}

bool ProjectManager::loadCurrentProjectModules(QStringList* failedModules)
{
    QStringList failures;
    if (auto* service = ServiceRegistry::instance().getService<GlobalParameterService>())
        service->newProject();
    if (auto* service = ServiceRegistry::instance().getService<PreProcessingService>();
        service && !service->loadProject())
        failures << QStringLiteral("几何");
    if (auto* service = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        service && !service->loadProject())
        failures << QStringLiteral("网格");
    if (auto* service = ServiceRegistry::instance().getService<MaterialManagerService>();
        service && !service->loadProject())
        failures << QStringLiteral("材料");
    if (auto* service = ServiceRegistry::instance().getService<PhysicalFieldService>();
        service && !service->loadProject())
        failures << QStringLiteral("约束与荷载");
    if (auto* service = ServiceRegistry::instance().getService<BoundaryConditionsService>();
        service && !service->loadProject())
        failures << QStringLiteral("边界条件");
    if (auto* service = ServiceRegistry::instance().getService<SolverService>();
        service && !service->loadProject())
        failures << QStringLiteral("计算分析");
    if (auto* service = ServiceRegistry::instance().getService<PostProcessingService>();
        service && !service->loadProject())
        failures << QStringLiteral("后处理");

    failures.removeDuplicates();
    if (failedModules)
        *failedModules = failures;
    return failures.isEmpty();
}

void ProjectManager::applyProjectContext(const QString& manifestPath, const QString& projectName,
                                         const QVariantMap& globalValues)
{
    auto& variables = PublicVariables::instance();
    const QStringList oldProjectKeys = variables.keysForOwner(QStringLiteral("project_dir"));
    for (const QString& key : oldProjectKeys)
        variables.remove(key, QStringLiteral("project_dir"));

    const QFileInfo manifestInfo(manifestPath);
    m_projectName = projectName.trimmed().isEmpty() ? manifestInfo.baseName()
                                                    : projectName.trimmed();
    m_projectTempFolder = manifestInfo.absolutePath();
    m_projectPath = m_projectTempFolder;
    m_projectEnginePath = manifestInfo.absoluteFilePath();
    variables.set(PublicConstants::kProjectDirectoryKey, m_projectTempFolder,
                  QStringLiteral("project_dir"));

    for (auto it = globalValues.constBegin(); it != globalValues.constEnd(); ++it)
    {
        if (it.key() == QString::fromLatin1(PublicConstants::kProjectDirectoryKey))
            continue;
        const QString existingOwner = variables.ownerOf(it.key());
        const QString owner = !existingOwner.isEmpty()
                                  ? existingOwner
                                  : it.key() == QString::fromLatin1(PublicConstants::kMeshDimension)
                                        ? QStringLiteral("mesh")
                                        : QStringLiteral("project_dir");
        variables.set(it.key(), it.value(), owner);
    }
}

bool ProjectManager::activateProject(const QString& manifestPath, const QString& projectName,
                                     const QVariantMap& globalValues,
                                     QStringList* failedModules)
{
    applyProjectContext(manifestPath, projectName, globalValues);
    updateIiidirDatForProject(m_projectTempFolder, false);

    m_projectTreeWidget->buildDefaultProjectTree(m_projectName);
    m_rootNode = m_projectTreeWidget->m_rootNode;
    m_projectTreeWidget->blockSignal(true);
    const bool loaded = loadCurrentProjectModules(failedModules);
    m_projectTreeWidget->blockSignal(false);
    return loaded;
}

bool ProjectManager::prepareProjectDirectory(const QString& projectDir,
                                             QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    QDir directory(projectDir);
    if (!directory.exists())
        return fail(QStringLiteral("工程目录不存在。"));
    if (!directory.mkpath(QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)))
        return fail(QStringLiteral("工程目录不可写，无法准备工程数据。"));
    if (!migrateLegacyProjectDataFiles(projectDir) || !migrateLegacySolverFiles(projectDir))
        return fail(QStringLiteral("旧版工程数据整理失败，请关闭正在占用工程文件的程序。"));

    const QStringList jsonFiles = {
        QString::fromUtf8(PublicConstants::kPreProcessingProjectFileName),
        QString::fromUtf8(PublicConstants::kMeshProjectFileName),
        QString::fromUtf8(PublicConstants::kMaterialProjectFileName),
        QString::fromUtf8(PublicConstants::kPhysicalFieldProjectFileName),
        QString::fromUtf8(PublicConstants::kSolverProjectFileName),
        QString::fromUtf8(PublicConstants::kPostProcessingProjectFileName)};
    QJsonObject meshRoot;
    for (const QString& relativePath : jsonFiles)
    {
        QFile file(directory.filePath(relativePath));
        if (!file.exists())
            continue;
        if (!file.open(QIODevice::ReadOnly))
            return fail(QStringLiteral("无法读取工程数据：%1").arg(relativePath));
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject())
            return fail(QStringLiteral("工程数据格式错误：%1").arg(relativePath));
        if (relativePath == QString::fromUtf8(PublicConstants::kMeshProjectFileName))
            meshRoot = document.object();
    }

    if (meshRoot.value(QStringLiteral("has_grid")).toBool(false))
    {
        const QString snapshot = meshRoot.value(QStringLiteral("snapshot_file"))
                                     .toString(QString::fromUtf8(
                                         PublicConstants::kMeshSnapshotFileName));
        const QFileInfo snapshotInfo(directory.filePath(snapshot));
        if (!snapshotInfo.exists() || snapshotInfo.size() <= 0)
            return fail(QStringLiteral("工程记录了网格，但网格数据文件缺失。"));
    }
    return true;
}

void ProjectManager::openProject()
{
    QFileDialog dialog(nullptr, "选择工程文件");
    dialog.setNameFilter("Project Files (*.xml)");
    dialog.setFilter(QDir::Files);

    const QString configFile =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config.ini"));
    const QSettings settings(configFile, QSettings::IniFormat);
    QString initialDirectory =
        settings.value(QStringLiteral("ProjectSetupDialog/defaultPath")).toString().trimmed();
    if (!QDir(initialDirectory).exists())
    {
        const QFileInfo currentProjectInfo(m_projectEnginePath);
        initialDirectory = currentProjectInfo.exists() ? currentProjectInfo.absolutePath()
                                                       : QDir::homePath();
    }
    dialog.setDirectory(initialDirectory);

    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;

    const QFileInfo targetInfo(dialog.selectedFiles().first());
    const QString targetManifest = targetInfo.absoluteFilePath();
    const QString currentCanonical = QFileInfo(m_projectEnginePath).canonicalFilePath();
    const QString targetCanonical = targetInfo.canonicalFilePath();
    if (!currentCanonical.isEmpty() && currentCanonical == targetCanonical)
    {
        QMessageBox::information(nullptr, QStringLiteral("工程已打开"),
                                 QStringLiteral("选择的就是当前工程。"));
        return;
    }

    QString targetName;
    QVariantMap targetGlobals;
    if (!readProjectManifest(targetManifest, &targetName, &targetGlobals))
    {
        QMessageBox::warning(nullptr, QStringLiteral("无法打开工程"),
                             QStringLiteral("选择的工程索引文件无法读取或格式不正确。"));
        return;
    }

    bool currentProjectSaved = true;
    if (!confirmProjectSwitch(&currentProjectSaved))
        return;

    QString prepareError;
    if (!prepareProjectDirectory(targetInfo.absolutePath(), &prepareError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("无法打开工程"), prepareError);
        return;
    }

    const bool hadCurrentProject = m_rootNode && !m_projectEnginePath.isEmpty();
    const QString previousManifest = m_projectEnginePath;
    const QString previousName = m_projectName;
    QVariantMap previousGlobals =
        PublicVariables::instance().snapshotForOwner(QStringLiteral("project_dir"));
    const QVariantMap meshGlobals =
        PublicVariables::instance().snapshotForOwner(QStringLiteral("mesh"));
    for (auto it = meshGlobals.constBegin(); it != meshGlobals.constEnd(); ++it)
        previousGlobals.insert(it.key(), it.value());

    closeCurrentProjectModules();
    QStringList failedModules;
    if (activateProject(targetManifest, targetName, targetGlobals, &failedModules))
    {
        if (m_outputService)
            m_outputService->outputLog(QStringLiteral("已打开工程：%1").arg(targetInfo.absolutePath()));
        return;
    }

    closeCurrentProjectModules();
    QStringList rollbackFailures;
    bool restored = false;
    if (hadCurrentProject && QFileInfo::exists(previousManifest))
    {
        QString ignoredError;
        prepareProjectDirectory(QFileInfo(previousManifest).absolutePath(), &ignoredError);
        restored = activateProject(previousManifest, previousName, previousGlobals,
                                   &rollbackFailures);
    }

    QString message = QStringLiteral("新工程的以下内容加载失败：%1。")
                          .arg(failedModules.join(QStringLiteral("、")));
    if (restored)
    {
        message += currentProjectSaved
                       ? QStringLiteral("\n已恢复原工程。")
                       : QStringLiteral("\n已从磁盘恢复原工程，未保存的修改无法恢复。");
    }
    else
    {
        message += QStringLiteral("\n原工程也未能恢复，请重新打开工程。");
        if (!rollbackFailures.isEmpty())
            message += QStringLiteral("\n恢复失败内容：%1。")
                           .arg(rollbackFailures.join(QStringLiteral("、")));
    }
    QMessageBox::warning(nullptr, QStringLiteral("工程切换失败"), message);
}

void ProjectManager::saveProject()
{
    QStringList failedModules;
    if (!saveCurrentProject(&failedModules) && m_outputService)
    {
        m_outputService->outputLog(
            QStringLiteral("工程保存失败：%1").arg(failedModules.join(QStringLiteral("、"))));
    }
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

    const QString projectName = dialog.projectName();
    const QString projectPath = dialog.projectPath();
    if (projectName.isEmpty() || projectPath.isEmpty())
    {
        return;
    }

    if (!confirmProjectSwitch(nullptr))
        return;

    if (!createProjectFilesFolder(projectPath, projectName, true))
    {
        return;
    }

    const QString projectDir = QDir(projectPath).filePath(projectName);
    const QString manifestPath = QDir(projectDir).filePath(projectName + QStringLiteral(".xml"));
    closeCurrentProjectModules();
    applyProjectContext(manifestPath, projectName, QVariantMap{});
    m_projectTreeWidget->buildDefaultProjectTree(projectName);
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

    QStringList failedModules;
    if (!saveCurrentProject(&failedModules))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("工程创建未完成"),
            QStringLiteral("工程已打开，但以下内容未能初始保存：%1")
                .arg(failedModules.join(QStringLiteral("、"))));
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
    return m_rootNode != nullptr && !m_projectTempFolder.isEmpty();
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
    QDir dir(QDir::cleanPath(dirPath));
    if (!dir.exists())
        return true;
    return dir.removeRecursively();
}

bool ProjectManager::createProjectFilesFolder(const QString& projectPath,
                                              const QString& projectName,
                                              bool askOverwrite)
{
    if (projectPath.isEmpty() || projectName.isEmpty())
    {
        return false;
    }

    const QString tempPath = QDir(projectPath).filePath(projectName);

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
                QMessageBox::critical(
                    nullptr, QStringLiteral("无法覆盖工程"),
                    QStringLiteral("无法清理已有工程目录，请关闭正在占用其中文件的程序后重试。"));
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
            return migrateLegacyProjectDataFiles(tempPath) &&
                   migrateLegacySolverFiles(tempPath);
        }
    }

    if (!QDir().mkpath(tempPath))
    {
        QMessageBox::critical(nullptr, QStringLiteral("无法创建工程"),
                              QStringLiteral("无法重新创建工程目录：\n%1").arg(tempPath));
        return false;
    }

    QDir projectDir(tempPath);
    if (!projectDir.mkpath(
            QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)))
    {
        return false;
    }

    return migrateLegacyProjectDataFiles(tempPath) && migrateLegacySolverFiles(tempPath);
}

bool ProjectManager::migrateLegacySolverFiles(const QString& projectDir) const
{
    const QDir project(projectDir);
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

    removeLegacyRootSolverFiles(projectDir);
    return QDir().mkpath(projectInput);
}

void ProjectManager::removeLegacyRootSolverFiles(const QString& projectDir) const
{
    const QDir project(projectDir);
    const QStringList names = {
        QStringLiteral("IFILL.DAT"),      QStringLiteral("ISEEP.DAT"),
        QStringLiteral("ICAVE.DAT"),      QStringLiteral("ISLOP.DAT"),
        QStringLiteral("IGEOS.DAT"),      QStringLiteral("IMID_GEOS.DAT"),
        QStringLiteral("MESG_IAUTO.DAT"), QStringLiteral("MESG_ITASK.DAT"),
        QStringLiteral("MESG064.DAT")};
    for (const QString& name : names)
        QFile::remove(project.filePath(name));
}

bool ProjectManager::migrateLegacyProjectDataFiles(const QString& projectDir) const
{
    const QDir project(projectDir);
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

bool ProjectManager::readProjectManifest(const QString& manifestPath, QString* projectName,
                                         QVariantMap* globalValues) const
{
    if (projectName)
        projectName->clear();
    if (globalValues)
        globalValues->clear();

    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QXmlStreamReader xml(&file);
    QString documentRoot;
    while (!xml.atEnd())
    {
        xml.readNext();
        if (xml.isStartElement())
        {
            const QString elementName = xml.name().toString();
            if (documentRoot.isEmpty())
                documentRoot = elementName;
            if ((elementName == QStringLiteral("Name") ||
                 elementName == QStringLiteral("ProjectName")) &&
                projectName && projectName->isEmpty())
            {
                *projectName = xml.readElementText().trimmed();
            }
            else if (elementName == QStringLiteral("Value"))
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
                else if (type == "bool")
                    val = text.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 ||
                          text == QStringLiteral("1");
                if (globalValues && !key.isEmpty())
                    globalValues->insert(key, val);
            }
        }
    }

    if (projectName && projectName->isEmpty())
        *projectName = QFileInfo(manifestPath).baseName();
    const bool supportedRoot = documentRoot == QStringLiteral("SmartCAEProject") ||
                               documentRoot == QStringLiteral("Project");
    return supportedRoot && !xml.hasError();
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
