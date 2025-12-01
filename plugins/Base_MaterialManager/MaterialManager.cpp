#include "ElectrolyteSettingWidget.h"
#include "MaterialManager.h"
#include "MaterialManagerWidget.h"
#include "MetalSettingWidget.h"
#include "ThinFilmSettingWidget.h"
#include <ProjectNode.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

MaterialManager::MaterialManager()
{
    ServiceRegistry::instance().registerService<MaterialManagerService>(this);
}

BasePage *MaterialManager::getMaterialManagerWidget()
{
//    MaterialManagerWidget *mm = new MaterialManagerWidget;
//    return (BasePage*)mm;
    return nullptr;
}

void MaterialManager::newProject()
{
    MaterialManagerWidget *mm = new MaterialManagerWidget(this);
    m_node = new ProjectNode("Material", QUuid::createUuid().toString() ,(BasePage*)mm);
    ProjectManagerService *projectManagerService= ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);

}

void MaterialManager::newMaterial()
{
    MetalSettingWidget *metalSettingWidget = new MetalSettingWidget;
    ProjectNode *node = new ProjectNode(QStringLiteral("金属材料1"), QUuid::createUuid().toString(),metalSettingWidget);
    m_node->addChild(node);

    ElectrolyteSettingWidget *electrolyteSettingWidget = new ElectrolyteSettingWidget;
    ProjectNode *node2 = new ProjectNode(QStringLiteral("电解液1"), QUuid::createUuid().toString(),electrolyteSettingWidget);
    m_node->addChild(node2);

    ThinFilmSettingWidget *thinFilmSettingWidget = new ThinFilmSettingWidget;
    ProjectNode *node3 = new ProjectNode(QStringLiteral("薄膜1"), QUuid::createUuid().toString(),thinFilmSettingWidget);
    m_node->addChild(node3);
}
