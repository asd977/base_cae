
#include "PostProcessing.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <ProjectNode.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/UiFrameService.h>
#include <vtkAutoInit.h>
#include <vtkAxesActor.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkTextProperty.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenu>
#include <QSaveFile>
#include <QSharedPointer>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QVector>

#include "AnimationAviWidget.h"
#include "CurveSettingsWidget.h"
#include "DerivedValueSettingWidget.h"
#include "IsosurfaceSegmentationWidget.h"
#include "IsosurfaceSettingsWidget.h"
#include "ProbeSettingsWidget.h"
#include "PostContourReaderWidget.h"
#include "PostDisplayControlWidget.h"
#include "PostResultAutoPrepare.h"
#include "PostResultSelectionUtils.h"
#include "PostTecplotUtils.h"
#include "PostVisualUtils.h"
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingContextOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle);

namespace
{
struct PostNodeSpec
{
    const char* title;
    const char* type;
};

const QVector<PostNodeSpec>& postNodeSpecs()
{
    static const QVector<PostNodeSpec> specs = {
        {"云图", PublicConstants::kPagePostProcessNephogram},
        {"矢量", PublicConstants::kPagePostProcessVector},
        {"切面", PublicConstants::kPagePostProcessSlice},
        {"切块", PublicConstants::kPagePostProcessBlock},
        {"等值面", PublicConstants::kPagePostProcessIsosurface},
        {"等值面分割", PublicConstants::kPagePostProcessIsosurfaceSegmentation},
        {"计算曲线", PublicConstants::kPagePostProcessCurve},
        {"动画设置", PublicConstants::kPagePostProcessAnimation},
        {"派生值", PublicConstants::kPagePostProcessDerivative},
        {"洞室变形轮廓图", PublicConstants::kPagePostProcessContourReader},
    };
    return specs;
}
} // namespace

VisualWidgetService* PostProcessing::visualService()
{
    if (!m_visualService)
    {
        m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (!m_visualService)
        {
            qWarning() << "VisualWidgetService is not registered.";
        }
    }
    return m_visualService;
}

void PostProcessing::initPageFactory()
{
    m_pageFactory.clear();
    m_pageFactory[PublicConstants::kPagePostProcessNephogram] = [this](VisualWidgetService* host)
    {
        return new NephogramSettingWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessVector] = [this](VisualWidgetService* host)
    {
        return new VectorSettingWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessSlice] = [this](VisualWidgetService* host)
    {
        return new SliceSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessBlock] = [this](VisualWidgetService* host)
    {
        return new BlockSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessIsosurface] = [this](VisualWidgetService* host)
    {
        return new IsosurfaceSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessIsosurfaceSegmentation] =
        [this](VisualWidgetService* host)
    {
        return new IsosurfaceSegmentationWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessAnimation] = [this](VisualWidgetService* host)
    {
        return new AnimationAviWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessDerivative] =
        [this](VisualWidgetService* /*host*/)
    {
        return new DerivedValueSettingWidget();
    };
    m_pageFactory[PublicConstants::kPagePostProcessCurve] =
        [this](VisualWidgetService* host)
    {
        return new CurveSettingsWidget(host);
    };
    m_pageFactory[PublicConstants::kPagePostProcessContourReader] =
        [this](VisualWidgetService* /*host*/)
    {
        return new PostContourReaderWidget();
    };
}

BasePage* PostProcessing::createPageByType(const QString& type)
{
    auto it = m_pageFactory.find(type);
    return it == m_pageFactory.end() ? nullptr : it.value()(visualService());
}

void PostProcessing::configureNode(ProjectNode* node, const QString& type)
{
    if (!node)
        return;

    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, QStringLiteral("PostProcessing"));
    node->setData(0, ProjectNode::RoleKind, type);
}

ProjectNode* PostProcessing::createPostNode(const QString& type, const QString& title,
                                            const QString& uuid, bool select)
{
    if (!m_root)
        return nullptr;

    BasePage* page = createPageByType(type);
    if (!page)
    {
        qWarning() << "Unsupported post-processing page type:" << type;
        return nullptr;
    }

    const QString id = uuid.isEmpty() ? QUuid::createUuid().toString() : uuid;
    ProjectNode* node = new ProjectNode(title, id, page);
    configureNode(node, type);
    m_root->addChild(node);

    if (select)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }

    return node;
}

ProjectNode* PostProcessing::findChildByUuid(const QString& uuid) const
{
    if (uuid.isEmpty() || !m_root)
        return nullptr;

    for (int i = 0; i < m_root->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;

        QString nodeUuid = node->data(0, ProjectNode::RoleUuid).toString();
        if (nodeUuid.isEmpty())
            nodeUuid = node->getUuid();
        if (nodeUuid == uuid)
            return node;
    }

    return nullptr;
}

ProjectNode* PostProcessing::findChildByType(const QString& type) const
{
    if (type.isEmpty() || !m_root)
        return nullptr;

    for (int i = 0; i < m_root->childCount(); ++i)
    {
        auto* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;
        if (node->data(0, ProjectNode::RoleKind).toString() == type)
            return node;
    }

    return nullptr;
}

PostProcessing::PostProcessing()
{
    m_visualService = nullptr;

    ServiceRegistry::instance().registerService<PostProcessingService>(this);
    UiFrameService* uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();

    ProbeSettingsWidget* tmp = new ProbeSettingsWidget(visualService());
    if (uiFrameService)
        uiFrameService->addWidget(tmp, "物理量");

    m_displayControlWidget = new PostDisplayControlWidget(visualService());
    if (uiFrameService)
    {
        uiFrameService->addWidget(m_displayControlWidget, QStringLiteral("结果显示"));
        uiFrameService->setWidgetVisible(m_displayControlWidget, false);
    }

    initPageFactory();
    initAction();
    initTreeMenu();
}

void PostProcessing::newNephogramSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessNephogram, QStringLiteral("云图"));
}

void PostProcessing::newVectorSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessVector, QStringLiteral("矢量"));
}

void PostProcessing::newSliceSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessSlice, QStringLiteral("切面"));
}

void PostProcessing::newBlockSettingsWidget()
{
    createPostNode(PublicConstants::kPagePostProcessBlock, QStringLiteral("切块"));
}

void PostProcessing::newIsosurfaceSettingsWidget()
{
    createPostNode(PublicConstants::kPagePostProcessIsosurface, QStringLiteral("等值面"));
}

void PostProcessing::newIsosurfaceSegmentationWidget()
{
    createPostNode(PublicConstants::kPagePostProcessIsosurfaceSegmentation,
                   QStringLiteral("等值面分割"));
}

void PostProcessing::newAnimationAviWidget()
{
    createPostNode(PublicConstants::kPagePostProcessAnimation, QStringLiteral("动画设置"));
}

// void PostProcessing::newProbeSettingsWidget()
//{

//}

void PostProcessing::newDerivedValueSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessDerivative, QStringLiteral("派生值"));
}

void PostProcessing::newCurveSettingWidget()
{
    createPostNode(PublicConstants::kPagePostProcessCurve, QStringLiteral("计算曲线"));
}

void PostProcessing::newPostContourReaderWidget()
{
    createPostNode(PublicConstants::kPagePostProcessContourReader, QStringLiteral("洞室变形轮廓图"));
}

void PostProcessing::newProject()
{
    if (m_root)
        closeProject();

    PostResultSelectionUtils::clearSharedResultSelection();
    postScalarBarStyle() = PostScalarBarStyle{};
    postContourStyle() = PostContourStyle{};
    postSceneStyle() = PostSceneStyle{};
    postDisplaySettingsAuthoritative() = true;
    postFieldRangeStyles().clear();
    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    m_root = new ProjectNode("后处理");
    m_root->setData(0, ProjectNode::RoleUuid, m_root->getUuid());
    m_root->setData(0, ProjectNode::RoleModule, "PostProcessing");
    m_root->setData(0, ProjectNode::RoleKind, "PostProcessingRoot");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    if (projectManagerService)
        projectManagerService->addTreeItem(m_root);
}

void PostProcessing::initAction()
{
    const QString initialProject =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString initialMatype =
        PublicVariables::instance().get(QStringLiteral("post.result.filter.matype.checked")).toString();
    const QString initialLayer =
        PublicVariables::instance().get(QStringLiteral("post.result.filter.layer.checked")).toString();

    EventManager::instance().registerHandler(
        postResultFilterChangedEvent(), this,
        [this, lastProject = initialProject, lastMatype = initialMatype,
         lastLayer = initialLayer](const QVariantMap& values) mutable
        {
            if (!postSceneStyle().followMeshFilter)
                return;

            // 后处理筛选状态来自结果自身 MATYPE/LAYER，不再由 Mesh 数据驱动。
            const QString project =
                PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
            const QString matype = values.value(QStringLiteral("matype")).toString();
            const QString layer = values.value(QStringLiteral("layer")).toString();
            if (project == lastProject && matype == lastMatype && layer == lastLayer)
                return;

            lastProject = project;
            lastMatype = matype;
            lastLayer = layer;
            notifyPostDisplaySettingsChanged(QStringLiteral("domain-filter"));
            if (m_displayControlWidget)
                m_displayControlWidget->syncFromState();
        });

    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessDisplayControl, this,
        [this](const QVariantMap&)
        {
            if (auto* uiFrame = ServiceRegistry::instance().getService<UiFrameService>())
                if (m_displayControlWidget)
                {
                    m_displayControlWidget->syncFromState();
                    uiFrame->setWidgetVisible(m_displayControlWidget, true);
                }
        });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessNephogram, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newNephogramSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessVector, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newVectorSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessSection, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newSliceSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDice, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newBlockSettingsWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessIsosurface, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newIsosurfaceSettingsWidget();
                                             });
    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessIsosurfaceSegmentation, this,
        [this](const QVariantMap&)
        {
            this->newIsosurfaceSegmentationWidget();
        });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessXyPlot, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newCurveSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessAnimation, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newAnimationAviWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessPickPhysical, this,
                                             [this](const QVariantMap&)
                                             {
                                                 //        this->ne();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDerivative, this,
                                             [this](const QVariantMap&)
                                             {
                                                 this->newDerivedValueSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDataExport, this,
                                             [this](const QVariantMap&)
                                             {
                                                 //        this->newNephogramSettingWidget();
                                             });
    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessContourReader, this,
        [this](const QVariantMap&)
        {
            this->newPostContourReaderWidget();
        });
    EventManager::instance().registerHandler(
        PublicConstants::kEventSolverFinished, this,
        [this](const QVariantMap& data)
        {
            QString projectDir = data.value(QStringLiteral("projectDir")).toString().trimmed();
            if (projectDir.isEmpty())
            {
                projectDir = PublicVariables::instance()
                                 .get(PublicConstants::kProjectDirectoryKey)
                                 .toString()
                                 .trimmed();
            }

            projectDir = QDir(projectDir).absolutePath();
            QString resultRoot = data.value(QStringLiteral("resultRoot")).toString().trimmed();
            if (resultRoot.isEmpty())
                resultRoot = projectDir;
            resultRoot = QDir(resultRoot).absolutePath();
            if (projectDir.isEmpty() || resultRoot.isEmpty())
                return;
            prepareResultRootAsync(projectDir, resultRoot);
        });
}

void PostProcessing::prepareResultRootAsync(const QString& projectDir, const QString& resultRoot)
{
    const QString projectPath = QDir(projectDir).absolutePath();
    const QString rootPath = QDir(resultRoot).absolutePath();
    const QString key = QDir::fromNativeSeparators(rootPath).toCaseFolded();
    if (projectPath.isEmpty() || rootPath.isEmpty())
        return;

    if (m_preparingResultRoots.contains(key))
    {
        m_pendingResultRoots.insert(key);
        return;
    }

    m_preparingResultRoots.insert(key);
    const auto prepared = QSharedPointer<bool>::create(false);
    const auto summary = QSharedPointer<PostResultAutoPrepare::PreparedResultSummary>::create();
    const QString preferredResultFile =
        PostResultSelectionUtils::sharedResultSelection().resultFile;
    QThread* worker = QThread::create(
        [projectPath, rootPath, preferredResultFile, prepared, summary]()
        {
            *prepared = PostResultAutoPrepare::prepareResultRoot(projectPath, rootPath);
            if (!*prepared)
                return;

            *summary = PostResultAutoPrepare::preparedResultSummary(projectPath, rootPath);

            // 只在求解后的后处理结果已经完整生成以后预读；不在这里创建任何
            // 持久二进制副本，避免把异常/半成品结果缓存下来。
            QString warmFile = preferredResultFile;
            const QString foldedRoot =
                QDir::fromNativeSeparators(QDir(rootPath).absolutePath()).toCaseFolded();
            const QString foldedWarm =
                warmFile.isEmpty()
                    ? QString()
                    : QDir::fromNativeSeparators(QFileInfo(warmFile).absoluteFilePath())
                          .toCaseFolded();
            if (warmFile.isEmpty() || !QFileInfo::exists(warmFile) ||
                (foldedWarm != foldedRoot &&
                 !foldedWarm.startsWith(foldedRoot + QLatin1Char('/'))))
            {
                warmFile.clear();
                const auto groups = PostResultAutoPrepare::resultGroups(projectPath);
                for (const auto& group : groups)
                {
                    const QString foldedSource =
                        QDir::fromNativeSeparators(QDir(group.sourcePath).absolutePath())
                            .toCaseFolded();
                    if (foldedSource != foldedRoot &&
                        !foldedSource.startsWith(foldedRoot + QLatin1Char('/')))
                        continue;

                    const auto files = PostResultAutoPrepare::resultFiles(
                        projectPath, group.sourcePath, true);
                    if (!files.isEmpty())
                    {
                        warmFile = files.constFirst().filePath;
                        break;
                    }
                }
            }

            if (!warmFile.isEmpty())
            {
                // 内部只保留一份标准 vtkUnstructuredGrid。求解完成后低优先级预读
                // 这一个结果即可，云图/切面/裁剪都复用同一份内存缓存。
                QString gridWarmError;
                PostTecplotUtils::readGrid(warmFile, &gridWarmError);
                if (!gridWarmError.isEmpty())
                    qWarning().noquote()
                        << QStringLiteral("后处理结果预读失败：%1").arg(gridWarmError);
            }
        });

    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this,
            [this, projectPath, rootPath, key, prepared, summary]()
            {
                m_preparingResultRoots.remove(key);
                const bool prepareAgain = m_pendingResultRoots.remove(key);
                if (*prepared)
                {
                    const QString outputDir = QDir(rootPath).filePath(
                        QString::fromUtf8(PublicConstants::kPostResultDirectoryName));
                    QStringList logLines{QStringLiteral("__RAW__计算完成，后处理结果已生成。")};
                    if (summary->groupCount > 0)
                        logLines.push_back(QStringLiteral("生成内容：%1 组后处理结果")
                                               .arg(summary->groupCount));
                    if (!summary->resultTypes.isEmpty())
                        logLines.push_back(QStringLiteral("结果数据：%1")
                                               .arg(summary->resultTypes.join(QStringLiteral("、"))));
                    if (!summary->resultConditions.isEmpty())
                    {
                        QStringList conditions = summary->resultConditions;
                        if (summary->hasRegularResults)
                            conditions.prepend(QStringLiteral("常规"));
                        logLines.push_back(
                            QStringLiteral("结果工况：%1")
                                .arg(conditions.join(QStringLiteral("、"))));
                    }
                    logLines.push_back(
                        QStringLiteral("保存位置：%1")
                            .arg(QDir::toNativeSeparators(outputDir)));
                    qInfo().noquote() << logLines.join(QLatin1Char('\n'));
                }
                else
                {
                    qWarning().noquote() << QStringLiteral(
                        "__RAW__计算完成，但没有生成可用的后处理结果。");
                }
                if (prepareAgain)
                    prepareResultRootAsync(projectPath, rootPath);
            });
    worker->start();
}

void PostProcessing::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        QStringLiteral("project_tree.node"), this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value(QStringLiteral("uuid")).toString();
            const QString kind = ctx.value(QStringLiteral("kind")).toString();

            ContextMenuHub::addSeparator(menu);

            if (kind == QStringLiteral("PostProcessingRoot"))
            {
                for (const auto& spec : postNodeSpecs())
                {
                    const QString title = QString::fromUtf8(spec.title);
                    const QString type = QString::fromLatin1(spec.type);
                    ContextMenuHub::addAction(menu, this, QStringLiteral("新增%1").arg(title),
                                              [this, type, title]
                                              {
                                                  createPostNode(type, title);
                                              });
                }
                return;
            }

            ContextMenuHub::addAction(menu, this, QStringLiteral("重命名"),
                                      [this, uuid]
                                      {
                                          renameNode(uuid);
                                      });
            ContextMenuHub::addAction(menu, this, QStringLiteral("删除"),
                                      [this, uuid]
                                      {
                                          deleteNode(uuid);
                                      });
        },
        80,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value(QStringLiteral("module")).toString() ==
                   QStringLiteral("PostProcessing");
        });
}

void PostProcessing::renameNode(const QString& uuid)
{
    ProjectNode* node = findChildByUuid(uuid);
    if (!node)
        return;

    const QString currentName = node->text(0);
    bool ok = false;
    const QString newName = QInputDialog::getText(nullptr, tr("重命名后处理结果"),
                                                  tr("请输入新名称："), QLineEdit::Normal,
                                                  currentName, &ok);
    if (!ok)
        return;

    const QString trimmedName = newName.trimmed();
    if (trimmedName.isEmpty() || trimmedName == currentName)
        return;

    node->setText(0, trimmedName);
    node->setName(trimmedName);
    if (BasePage* page = node->getPage())
        page->setDisplayName(trimmedName);
}

void PostProcessing::deleteNode(const QString& uuid)
{
    ProjectNode* node = findChildByUuid(uuid);
    if (!node || !m_root)
        return;

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(m_root);

    if (BasePage* page = node->getPage())
    {
        if (!page->hasDeferredLoadJson())
            page->beforeUnload();
        page->deleteLater();
        node->setPage(nullptr);
    }

    m_root->removeChild(node);
    delete node;
}

void PostProcessing::removeRootFromTree()
{
    if (!m_root)
        return;

    if (auto* parent = dynamic_cast<ProjectNode*>(m_root->parent()))
    {
        parent->removeChild(m_root);
    }
    else if (QTreeWidget* tree = m_root->treeWidget())
    {
        const int index = tree->indexOfTopLevelItem(m_root);
        if (index >= 0)
            tree->takeTopLevelItem(index);
    }
}

bool PostProcessing::saveProject()
{
    if (!m_root)
        return true;
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPostProcessingProjectFileName);

    QJsonObject root;
    root["module"] = "PostProcessing";
    root["root_title"] = "后处理";
    root["result_context"] = PostResultSelectionUtils::saveSharedResultSelection();
    root["display_settings"] = savePostDisplaySettings();

    QJsonArray items;
    const int n = m_root->childCount();
    for (int i = 0; i < n; ++i)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;
        BasePage* page = node->getPage();
        if (!page)
            continue;

        QJsonObject item;
        item["uuid"] = node->getUuid();
        item["title"] = node->text(0);
        item["type"] = page->getPageType();
        item["state"] = page->saveJsonForPersistence();
        items.append(item);
    }
    root["items"] = items;
    return writeJsonFile(filePath, root);
}

bool PostProcessing::loadProject()
{
    if (m_root)
        closeProject();

    PostResultSelectionUtils::clearSharedResultSelection();
    postScalarBarStyle() = PostScalarBarStyle{};
    postContourStyle() = PostContourStyle{};
    postSceneStyle() = PostSceneStyle{};
    postDisplaySettingsAuthoritative() = false;
    postFieldRangeStyles().clear();
    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
        m_visualService->showScalarBar(false, false);
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    m_root = new ProjectNode("后处理");
    m_root->setData(0, ProjectNode::RoleUuid, m_root->getUuid());
    m_root->setData(0, ProjectNode::RoleModule, "PostProcessing");
    m_root->setData(0, ProjectNode::RoleKind, "PostProcessingRoot");
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    if (pm)
        pm->addTreeItem(m_root);

    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPostProcessingProjectFileName);
    QJsonObject root;
    if (!readJsonFile(filePath, root))
    {
        postDisplaySettingsAuthoritative() = true;
        if (m_displayControlWidget)
            m_displayControlWidget->syncFromState();
        return true;
    }

    if (root.value(QStringLiteral("result_context")).isObject())
        PostResultSelectionUtils::loadSharedResultSelection(root.value(QStringLiteral("result_context")).toObject());
    if (root.value(QStringLiteral("display_settings")).isObject())
    {
        loadPostDisplaySettings(root.value(QStringLiteral("display_settings")).toObject());
        postDisplaySettingsAuthoritative() = true;
    }
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();
    if (m_visualService)
        m_visualService->setGroupVisible(QStringLiteral("post"), postSceneStyle().visible, false);

    const QJsonArray items = root.value("items").toArray();
    for (const auto& v : items)
    {
        const auto obj = v.toObject();
        const QString id = obj.value("uuid").toString(QUuid::createUuid().toString());
        const QString title = obj.value("title").toString("未命名");
        const QString type = obj.value("type").toString();
        const QJsonObject state = obj.value("state").toObject();

        ProjectNode* node = createPostNode(type, title, id, false);
        if (!node)
            continue;

        if (BasePage* page = node->getPage())
        {
            // 后处理结果页可能在 loadJson() 中扫描结果目录、准备结果文件甚至读取
            // Tecplot/VTU。工程打开阶段只保存状态，首次真正进入页面时再恢复。
            page->deferLoadJson(state);
        }
    }
    postDisplaySettingsAuthoritative() = true;
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    return true;
}

void PostProcessing::closeProject()
{
    if (!m_root)
        return;

    clearPostDisplayContext();
    postMeshFilterReport() = PostMeshFilterReport{};
    setPostViewActive(false);
    if (m_visualService)
    {
        m_visualService->showScalarBar(false, false);
        m_visualService->setGroupVisible(QStringLiteral("post"), false, false);
        m_visualService->render();
    }
    if (m_displayControlWidget)
        m_displayControlWidget->syncFromState();

    while (m_root->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(0));
        if (node)
        {
            if (auto* page = node->getPage())
            {
                if (!page->hasDeferredLoadJson())
                    page->beforeUnload();
                page->deleteLater();

                node->setPage(nullptr);
            }
            m_root->removeChild(node);
            delete node;
        }
    }

    removeRootFromTree();
    if (auto* page = m_root->getPage())
    {
        page->beforeUnload();
        page->deleteLater();
        m_root->setPage(nullptr);
    }
    delete m_root;
    m_root = nullptr;
}

bool PostProcessing::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    const QJsonDocument document(obj);
    if (file.write(document.toJson(QJsonDocument::Indented)) < 0)
        return false;

    return file.commit();
}

bool PostProcessing::readJsonFile(const QString& filePath, QJsonObject& out)
{
    QFile f(filePath);
    if (!f.exists())
        return false;
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    out = doc.object();
    return true;
}
