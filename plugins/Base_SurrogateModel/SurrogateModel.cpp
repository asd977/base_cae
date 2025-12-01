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
    LatinHypercubeSampling *tmp = new LatinHypercubeSampling();
    ProjectNode *node = new ProjectNode("拉丁超立方采样",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newOrthogonalSamplingWidget()
{
    OrthogonalSampling *tmp = new OrthogonalSampling();
    ProjectNode *node = new ProjectNode("正交采样",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);

}

void SurrogateModel::newDNNModel()
{
    DNNModel *tmp = new DNNModel();
    ProjectNode *node = new ProjectNode("DNN模型",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newDatasetEvaluation()
{
    DatasetEvaluation *tmp = new DatasetEvaluation();
    ProjectNode *node = new ProjectNode("数据集评估",QUuid::createUuid().toString(),(BasePage*)tmp);

    m_root->addChild(node);
}

void SurrogateModel::newProject()
{
    // m_root = new ProjectNode("代理模型");
    // ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    // projectManagerService->addTreeItem(m_root);
}

void SurrogateModel::initAction()
{
    EventManager::instance().registerHandler(PublicConstants::ACTION_SURROGATEMODEL_LATIN, [this](const QVariantMap &data) {
        this->newLatinHypercubeSamplingWidget();
    });
    EventManager::instance().registerHandler(PublicConstants::ACTION_SURROGATEMODEL_ORTHOGONAL, [this](const QVariantMap &data) {
        this->newOrthogonalSamplingWidget();
    });
    EventManager::instance().registerHandler(PublicConstants::ACTION_SURROGATEMODEL_DNN, [this](const QVariantMap &data) {
        this->newDNNModel();
    });
    EventManager::instance().registerHandler(PublicConstants::ACTION_SURROGATEMODEL_POD, [this](const QVariantMap &data) {
//        this->();
    });
    EventManager::instance().registerHandler(PublicConstants::ACTION_SURROGATEMODEL_DATAEVALUATION, [this](const QVariantMap &data) {
        this->newDatasetEvaluation();
    });
}

bool SurrogateModel::saveProject()
{
    return true;
}

bool SurrogateModel::loadProject()
{
    return true;
}

void SurrogateModel::closeProject()
{

}
