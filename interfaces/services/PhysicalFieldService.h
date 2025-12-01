#ifndef PHYSICALFIELDSERVICE_H
#define PHYSICALFIELDSERVICE_H
class BasePage;

struct PhysicalFieldService
{
    virtual ~PhysicalFieldService() {};
    virtual BasePage *getPhysicalFiledSettingWidget() = 0;
    virtual void newProject() = 0;

};
#endif // PHYSICALFIELDSERVICE_H
