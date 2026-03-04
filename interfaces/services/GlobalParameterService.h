#ifndef GLOBALPARAMETERSERVICE_H
#define GLOBALPARAMETERSERVICE_H

class BasePage;
struct GlobalParameterService
{
    virtual ~GlobalParameterService() {};
    virtual BasePage* newGlobalParameterSettingWidget() = 0;
    virtual void newProject() = 0;
};

#endif // GLOBALPARAMETERSERVICE_H
