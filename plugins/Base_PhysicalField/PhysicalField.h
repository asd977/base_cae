#ifndef PHYSICALFIELD_H
#define PHYSICALFIELD_H

#include <BasePage.h>
#include <ProjectNode.h>
#include <services/PhySicalFieldService.h>

class PhysicalField :public PhysicalFieldService
{
public:
    PhysicalField();

    BasePage *getPhysicalFiledSettingWidget();
    void newProject();

private:
    ProjectNode *m_node;
};

#endif // PHYSICALFIELD_H
