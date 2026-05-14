#ifndef GLOBALPARAMETERACTIVATOR_H
#define GLOBALPARAMETERACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "GlobalParameter.h"

class GlobalParameterActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    GlobalParameterActivator();

    QString pluginName() const override
    {
        return "GlobalParameter";
    }
    void start() override
    {
        qInfo() << "start GlobalParameter";
        GlobalParameter* postProcessing = new GlobalParameter;
    }
    void stop() override
    {
        qInfo() << "Stopping...";
    }
};

#endif // GLOBALPARAMETERACTIVATOR_H
