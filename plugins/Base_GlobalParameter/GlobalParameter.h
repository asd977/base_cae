#ifndef GLOBALPARAMETER_H
#define GLOBALPARAMETER_H

#include <ProjectNode.h>
#include <services/GlobalParameterService.h>

class GlobalParameter : public GlobalParameterService
{
  public:
    GlobalParameter();
    BasePage* newGlobalParameterSettingWidget() override;
    void newProject() override;

  private:
    ProjectNode* m_node;
};

#endif // GLOBALPARAMETER_H
