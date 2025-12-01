#include "ElectroPhoresisSettingWidget.h"
#include "ElectroPlatingSettingWidget.h"
#include "ElectrodeSettingWidget.h"
#include "ElectrolysisSettingWidget.h"
#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <PrimaryCurrentDistributionWidget.h>
#include <SecondaryCurrentDistributionWidget.h>
#include <TertiaryCurrentDistributionWidget.h>
#include <ServiceRegistry.h>

PhysicalField::PhysicalField()
{
    ServiceRegistry::instance().registerService<PhysicalFieldService>(this);
}

BasePage *PhysicalField::getPhysicalFiledSettingWidget()
{
    PhysicalFieldSettingWidget *tmp = new PhysicalFieldSettingWidget;
    return (BasePage*)tmp;
}

void PhysicalField::newProject()
{
    PhysicalFieldSettingWidget *physicalFieldSettingWidget = new PhysicalFieldSettingWidget;
    m_node = new ProjectNode("物理场",QUuid::createUuid().toString(),(BasePage*)physicalFieldSettingWidget);
    ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);

    auto addDistributionNode = [this](const QString &title, BasePage *page)
    {
        ProjectNode *node = new ProjectNode(title, QUuid::createUuid().toString(), page);
        m_node->addChild(node);
    };

    auto *primaryWidget = new PrimaryCurrentDistributionWidget;
    addDistributionNode("一次电流分布", primaryWidget);

    auto *secondaryWidget = new SecondaryCurrentDistributionWidget;
    addDistributionNode("二次电流分布", secondaryWidget);

    auto *tertiaryWidget = new TertiaryCurrentDistributionWidget;
    addDistributionNode("三次电流分布", tertiaryWidget);
}
