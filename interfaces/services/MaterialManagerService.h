#ifndef MATERIALMANAGERSERVICE_H
#define MATERIALMANAGERSERVICE_H

#include <QString>
class BasePage;

struct MaterialManagerService
{
    virtual BasePage* getMaterialManagerWidget() = 0;

    virtual void newProject() = 0;
    virtual void addMaterialFromMenu(const QString& modelKey) = 0;
};

#endif // MATERIALMANAGERSERVICE_H
