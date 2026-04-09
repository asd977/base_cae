#ifndef MATERIALMANAGERSERVICE_H
#define MATERIALMANAGERSERVICE_H

#include <QString>
#include <QVector>
#include <QPair>
class BasePage;

struct MaterialManagerService
{
    virtual ~MaterialManagerService()
    {
    }

    virtual BasePage* getMaterialManagerWidget() = 0;

    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void addMaterialFromMenu(const QString& modelKey) = 0;
    virtual void showBuiltinMaterialListDialog() = 0;
    virtual void showBuiltinSeepageListDialog() = 0;
    virtual void showMaterialTransitionDialog() = 0;
    virtual QVector<QPair<int, int>> materialTransitions() const = 0;
    virtual void setMaterialTransitions(const QVector<QPair<int, int>>& transitions) = 0;
};

#endif // MATERIALMANAGERSERVICE_H
