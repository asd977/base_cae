#include "PhySicalField.h"
#include "ConstraintSettingWidget.h"
#include "LoadSettingWidget.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

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

    ConstraintSettingWidget *constraintSettingWidget = new ConstraintSettingWidget;
    ProjectNode *constraintNode = new ProjectNode("约束",QUuid::createUuid().toString(),(BasePage*)constraintSettingWidget);
    m_node->addChild(constraintNode);

    LoadSettingWidget *loadSettingWidget = new LoadSettingWidget;
    ProjectNode *loadNode = new ProjectNode("荷载",QUuid::createUuid().toString(),(BasePage*)loadSettingWidget);
    m_node->addChild(loadNode);
}
