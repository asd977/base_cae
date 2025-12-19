#include "DNNModel.h"
#include "DatasetEvaluation.h"
#include "LatinHypercubeSampling.h"
#include "OrthogonalSampling.h"
#include "SurrogateModel.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <EventManager.h>
#include <PublicConstants.h>

SurrogateModel::SurrogateModel()
{
    ServiceRegistry::instance().registerService<SurrogateModelService>(this);

    initAction();
}

void SurrogateModel::newLatinHypercubeSamplingWidget()
{
    if (!m_root) return;
    LatinHypercubeSampling *tmp = new LatinHypercubeSampling();
    ProjectNode *node = new ProjectNode("拉丁超立方采样",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newOrthogonalSamplingWidget()
{
    if (!m_root) return;
    OrthogonalSampling *tmp = new OrthogonalSampling();
    ProjectNode *node = new ProjectNode("正交采样",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);

}

void SurrogateModel::newDNNModel()
{
    if (!m_root) return;
    DNNModel *tmp = new DNNModel();
    ProjectNode *node = new ProjectNode("DNN模型",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newDatasetEvaluation()
{
    if (!m_root) return;
    DatasetEvaluation *tmp = new DatasetEvaluation();
    ProjectNode *node = new ProjectNode("数据集评估",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newProject()
{
    ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    m_root = projectManagerService->nodeByKey("surrogate");
}

void SurrogateModel::initAction()
{
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelLatin, [this](const QVariantMap &data) {
        this->newLatinHypercubeSamplingWidget();
    });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelOrthogonal, [this](const QVariantMap &data) {
        this->newOrthogonalSamplingWidget();
    });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelDnn, [this](const QVariantMap &data) {
        this->newDNNModel();
    });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelPod, [this](const QVariantMap &data) {
//        this->();
    });
    EventManager::instance().registerHandler(PublicConstants::kActionSurrogateModelDataEvaluation, [this](const QVariantMap &data) {
        this->newDatasetEvaluation();
    });
}

bool SurrogateModel::saveProject()
{
    return true;
}

bool SurrogateModel::loadProject()
{
    ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    m_root = projectManagerService ? projectManagerService->nodeByKey("surrogate") : nullptr;
    return true;
}

void SurrogateModel::closeProject()
{

}
