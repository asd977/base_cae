#include "GlobalParameter.h"

#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>

#include "GlobalParameterSettingWidget.h"

GlobalParameter::GlobalParameter()
{
    ServiceRegistry::instance().registerService<GlobalParameterService>(this);
}

BasePage* GlobalParameter::newGlobalParameterSettingWidget()
{
    QUuid uuid = QUuid::createUuid();

    QString uuidStr = uuid.toString();

    GlobalParameterSettingWidget* tmp = new GlobalParameterSettingWidget(uuidStr);

    return (BasePage*)tmp;
}

void GlobalParameter::newProject()
{
    m_node = new ProjectNode("自定义参数");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_node);
}
