#ifndef MATERIALMANAGERACTIVATOR_H
#define MATERIALMANAGERACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "MaterialManager.h"
class MaterialManagerActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    MaterialManagerActivator();

    QString pluginName() const override
    {
        return "MaterialManager";
    }
    void start() override
    {
        qInfo() << "启动材料管理模块";
        MaterialManager* m = new MaterialManager;
    }
    void stop() override
    {
        qInfo() << "停止材料管理模块";
    }
};
#endif // MATERIALMANAGERACTIVATOR_H
