#ifndef PREPROCESSINGACTIVATOR_H
#define PREPROCESSINGACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "MeshModuleController.h"

class MeshActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    MeshActivator();

    QString pluginName() const override
    {
        return "MeshModuleController";
    }
    void start() override
    {
        qInfo() << "启动网格模块";
        MeshModuleController* meshProcessor = new MeshModuleController;
    }
    void stop() override
    {
        qInfo() << "停止网格模块";
    }
};

#endif // VISUALIZATIONACTIVATOR_H
