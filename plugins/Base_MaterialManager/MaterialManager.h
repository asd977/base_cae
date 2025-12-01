#ifndef MATERIALMANAGER_H
#define MATERIALMANAGER_H

#include <services/MaterialManagerService.h>
#include <BasePage.h>
#include <QObject>
class ProjectNode;
class MaterialManager:public QObject,public MaterialManagerService
{
    Q_OBJECT
public:
    MaterialManager();
    BasePage *getMaterialManagerWidget();
    void newProject();
    void newMaterial();
private:
    ProjectNode *m_node;
};

#endif // MATERIALMANAGER_H
