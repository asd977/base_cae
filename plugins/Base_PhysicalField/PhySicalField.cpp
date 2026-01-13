#include "PhySicalField.h"
#include "PhysicalFieldSettingWidget.h"
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <DisplacementConstraintWidget.h>
#include <LoadSpecificationWidget.h>
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



    DisplacementConstraintWidget *displacementConstraintWidget = new DisplacementConstraintWidget;
    ProjectNode *node2 = new ProjectNode("位移约束",QUuid::createUuid().toString(),(BasePage*)displacementConstraintWidget);
    m_node->addChild(node2);

    LoadSpecificationWidget *loadSpecificationWidget = new LoadSpecificationWidget;
    ProjectNode *node3 = new ProjectNode("荷载设置",QUuid::createUuid().toString(),(BasePage*)loadSpecificationWidget);
    m_node->addChild(node3);

}
