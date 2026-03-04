#include "SurrogateModel.h"

#include <EventManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "DNNModel.h"
#include "DatasetEvaluation.h"
#include "LatinHypercubeSampling.h"
#include "OrthogonalSampling.h"

SurrogateModel::SurrogateModel()
{
    ServiceRegistry::instance().registerService<SurrogateModelService>(this);

    initAction();
}

void SurrogateModel::newLatinHypercubeSamplingWidget()
{
    if (!m_root)
        return;
    LatinHypercubeSampling* tmp = new LatinHypercubeSampling();
    ProjectNode* node =
        new ProjectNode("拉丁超立方采样", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void SurrogateModel::newOrthogonalSamplingWidget()
{
    if (!m_root)
        return;
    OrthogonalSampling* tmp = new OrthogonalSampling();
    ProjectNode* node = new ProjectNode("正交采样", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void SurrogateModel::newDNNModel()
{
    if (!m_root)
        return;
    DNNModel* tmp = new DNNModel();
    ProjectNode* node = new ProjectNode("DNN模型", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void SurrogateModel::newDatasetEvaluation()
{
    if (!m_root)
        return;
    DatasetEvaluation* tmp = new DatasetEvaluation();
    ProjectNode* node =
        new ProjectNode("数据集评估", QUuid::createUuid().toString(), (BasePage*)tmp);

    m_root->addChild(node);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(node);
}

void SurrogateModel::newProject()
{
    if (m_root)
        return;
    m_root = new ProjectNode("代理模型");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
}

void SurrogateModel::initAction()
{
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelLatin,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newLatinHypercubeSamplingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelOrthogonal,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newOrthogonalSamplingWidget();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelDnn,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newDNNModel();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelPod,
                                             [this](const QVariantMap& data)
                                             {
                                                 //        this->();
                                             });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelDataEvaluation,
                                             [this](const QVariantMap& data)
                                             {
                                                 this->newDatasetEvaluation();
                                             });
}

bool SurrogateModel::saveProject()
{
    if (!m_root)
        return true;

    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString filePath = QDir(projectDir).filePath(PublicConstants::kSurrogateProjectFileName);

    QJsonObject root;
    root["module"] = "SurrogateModel";
    root["root_title"] = "代理模型";

    QJsonArray items;
    for (ProjectNode* node : m_root->getChildren())
    {
        if (!node || !node->getPage())
            continue;

        QJsonObject item;
        item["uuid"] = node->getUuid();
        item["title"] = node->text(0);
        item["type"] = node->getPage()->getPageType();
        item["state"] = node->getPage()->saveJson();
        items.append(item);
    }
    root["items"] = items;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool SurrogateModel::loadProject()
{
    newProject();
    if (!m_root)
        return false;

    QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSurrogateProjectFileName));
    if (!file.open(QIODevice::ReadOnly))
        return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const auto& v : root.value("items").toArray())
    {
        const QJsonObject obj = v.toObject();
        BasePage* page = createPageByType(obj.value("type").toString());
        if (!page)
            continue;

        auto* node =
            new ProjectNode(obj.value("title").toString("未命名"),
                            obj.value("uuid").toString(QUuid::createUuid().toString()), page);
        m_root->addChild(node);
        page->loadJson(obj.value("state").toObject());
    }
    return true;
}

void SurrogateModel::closeProject()
{
    if (!m_root)
        return;
    while (m_root->childCount() > 0)
    {
        ProjectNode* node = static_cast<ProjectNode*>(m_root->child(0));
        if (node && node->getPage())
        {
            node->getPage()->deleteLater();
            node->setPage(nullptr);
        }
        delete m_root->takeChild(0);
    }
    delete m_root;
    m_root = nullptr;
}

BasePage* SurrogateModel::createPageByType(const QString& type)
{
    if (type == "Latin")
        return new LatinHypercubeSampling();
    if (type == "Orthogonal")
        return new OrthogonalSampling();
    if (type == "DNN")
        return new DNNModel();
    if (type == "DatasetEvaluation")
        return new DatasetEvaluation();
    return nullptr;
}
