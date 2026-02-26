#ifndef PHYSICALFIELDSERVICE_H
#define PHYSICALFIELDSERVICE_H
class BasePage;

struct PhysicalFieldService
{
    virtual ~PhysicalFieldService() {};
    virtual BasePage *getPhysicalFiledSettingWidget() = 0;
    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void generateIIIDFile() = 0;
    virtual void generateIIWLFile() = 0;

    virtual QString pageTitleByUuid(const QString& uuid) const = 0;
};
#endif // PHYSICALFIELDSERVICE_H
