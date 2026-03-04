
#include "PostProcessing.h"

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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QToolButton>
#include <QTreeWidgetItem>
#include <QUuid>

#include "AnimationAviWidget.h"
#include "DerivedValueSettingWidget.h"
#include "IsosurfaceSegmentationWidget.h"
#include "IsosurfaceSettingsWidget.h"
#include "ProbeSettingsWidget.h"
VTK_MODULE_INIT(vtkRenderingOpenGL2)
VTK_MODULE_INIT(vtkRenderingContextOpenGL2)
VTK_MODULE_INIT(vtkInteractionStyle);

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
}

BasePage* PostProcessing::createPageByType(const QString& type)
{
    auto it = m_pageFactory.find(type);
    return it == m_pageFactory.end() ? nullptr : it.value()(visualService());
}

PostProcessing::PostProcessing()
{
    m_visualService = nullptr;

    ServiceRegistry::instance().registerService<PostProcessingService>(this);
    UiFrameService* uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();

    ProbeSettingsWidget* tmp = new ProbeSettingsWidget(visualService());
    if (uiFrameService)
        uiFrameService->addWidget(tmp, "物理量");

    initAction();
    initPageFactory();
}

void PostProcessing::newNephogramSettingWidget()
{
    NephogramSettingWidget* tmp = new NephogramSettingWidget(visualService());
    ProjectNode* node = new ProjectNode("云图", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newVectorSettingWidget()
{
    VectorSettingWidget* tmp = new VectorSettingWidget(visualService());
    ProjectNode* node = new ProjectNode("矢量", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newSliceSettingWidget()
{
    SliceSettingsWidget* tmp = new SliceSettingsWidget(visualService());
    ProjectNode* node = new ProjectNode("切面", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newBlockSettingsWidget()
{
    BlockSettingsWidget* tmp = new BlockSettingsWidget(visualService());
    ProjectNode* node = new ProjectNode("切块", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newIsosurfaceSettingsWidget()
{
    IsosurfaceSettingsWidget* tmp = new IsosurfaceSettingsWidget(visualService());
    ProjectNode* node = new ProjectNode("等值面", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newIsosurfaceSegmentationWidget()
{
    IsosurfaceSegmentationWidget* tmp = new IsosurfaceSegmentationWidget(visualService());
    ProjectNode* node =
        new ProjectNode("等值面分割", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newAnimationAviWidget()
{
    AnimationAviWidget* tmp = new AnimationAviWidget(visualService());
    ProjectNode* node = new ProjectNode("动画", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

// void PostProcessing::newProbeSettingsWidget()
//{

//}

void PostProcessing::newDerivedValueSettingWidget()
{
    DerivedValueSettingWidget* tmp = new DerivedValueSettingWidget;
    ProjectNode* node = new ProjectNode("派生值", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void PostProcessing::newProject()
{
    m_root = new ProjectNode("后处理");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

void PostProcessing::initAction()
{
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessNephogram, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newNephogramSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessVector, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newVectorSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessSection, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newSliceSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDice, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newBlockSettingsWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessIsosurface, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newIsosurfaceSettingsWidget();
                                             });
    EventManager::instance().registerHandler(
        PublicConstants::kActionPostProcessIsosurfaceSegmentation, this,
        [this](const QVariantMap& data)
        {
            this->newIsosurfaceSegmentationWidget();
        });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessXyPlot, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 //        this->new();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessAnimation, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newAnimationAviWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessPickPhysical, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 //        this->ne();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDerivative, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newDerivedValueSettingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionPostProcessDataExport, this,
                                             [this](const QVariantMap& data)
                                             {
                                                 //        this->newNephogramSettingWidget();
                                             });
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

    QJsonArray items;
    const int n = m_root->childCount();
    for (int i = 0; i < n; ++i)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(i));
        if (!node)
            continue;
        BasePage* page = node->getPage();

        QJsonObject item;
        item["uuid"] = node->getUuid();
        item["title"] = node->text(0);
        item["type"] = page->getPageType();
        item["state"] = page->saveJson();
        items.append(item);
    }
    root["items"] = items;
    return writeJsonFile(filePath, root);
}

bool PostProcessing::loadProject()
{
    m_root = new ProjectNode("后处理");
    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    pm->addTreeItem(m_root);

    const QString filePath =
        QDir(projectDir).filePath(PublicConstants::kPostProcessingProjectFileName);
    QJsonObject root;
    if (!readJsonFile(filePath, root))
    {
        return true;
    }

    const QJsonArray items = root.value("items").toArray();
    for (const auto& v : items)
    {
        const auto obj = v.toObject();
        const QString id = obj.value("uuid").toString(QUuid::createUuid().toString());
        const QString title = obj.value("title").toString("未命名");
        const QString type = obj.value("type").toString();
        const QJsonObject state = obj.value("state").toObject();

        BasePage* page = createPageByType(type);
        ProjectNode* node = new ProjectNode(title, id, page);
        m_root->addChild(node);

        if (page)
            page->loadJson(state);
    }
    return true;
}

void PostProcessing::closeProject()
{
    if (!m_root)
        return;

    while (m_root->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(0));
        if (node)
        {
            if (auto* page = node->getPage())
            {
                page->deleteLater();

                node->setPage(nullptr);
            }
            delete m_root->takeChild(0);
        }
    }

    // 从工程树移除根节点
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
    {
    }
    delete m_root;
    m_root = nullptr;
}

bool PostProcessing::writeJsonFile(const QString& filePath, const QJsonObject& obj)
{
    QFile f(filePath);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QJsonDocument doc(obj);
    f.write(doc.toJson(QJsonDocument::Indented));
    return true;
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
