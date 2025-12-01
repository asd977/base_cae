#include "ElectroPhoresisSettingWidget.h"
#include "ElectroPlatingSettingWidget.h"
#include "ElectrodeSettingWidget.h"
#include "ElectrolysisSettingWidget.h"
#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <PrimaryCurrentDistributionWidget.h>
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

    PrimaryCurrentDistributionWidget *primaryCurrentDistributionWidget = new PrimaryCurrentDistributionWidget;
    ProjectNode *node2 = new ProjectNode("一次电流分布",QUuid::createUuid().toString(),(BasePage*)primaryCurrentDistributionWidget);
    primaryCurrentDistributionWidget->setNode(node2);
    m_node->addChild(node2);



}
