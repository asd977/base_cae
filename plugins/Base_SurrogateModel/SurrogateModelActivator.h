#ifndef SURROGATEMODELACTIVATOR_H
#define SURROGATEMODELACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>

#include "SurrogateModel.h"

class SurrogateModelActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    SurrogateModelActivator();

    QString pluginName() const override
    {
        return "SurrogateModel";
    }
    void start() override
    {
        qInfo() << "start SurrogateModel";
        SurrogateModel* surrogateModel = new SurrogateModel;
    }
    void stop() override
    {
        qInfo() << "Stopping...";
    }
};

#endif // SURROGATEMODELACTIVATOR_H
