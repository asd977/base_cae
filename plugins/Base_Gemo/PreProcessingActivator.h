#ifndef PREPROCESSINGACTIVATOR_H
#define PREPROCESSINGACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "PreProcessing.h"

class PreProcessingActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    PreProcessingActivator();

    QString pluginName() const override
    {
        return "PreProcessing";
    }
    void start() override
    {
        qInfo() << "启动前处理模块";
        PreProcessing* preProcessing = new PreProcessing;
    }
    void stop() override
    {
        qInfo() << "停止前处理模块";
    }
};

#endif // VISUALIZATIONACTIVATOR_H
