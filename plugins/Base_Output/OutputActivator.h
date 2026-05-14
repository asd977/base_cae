#ifndef OUTPUTACTIVATOR_H
#define OUTPUTACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "Output.h"

class OutputActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    OutputActivator();

    QString pluginName() const override
    {
        return "Output";
    }
    void start() override
    {
        qInfo() << "start Output";
        Output* output = new Output;
    }
    void stop() override
    {
        qInfo() << "Stopping...";
    }
};

#endif // OUTPUTACTIVATOR_H
