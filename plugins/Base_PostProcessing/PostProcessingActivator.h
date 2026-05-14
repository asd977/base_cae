#ifndef POSTPROCESSINGACTIVATOR_H
#define POSTPROCESSINGACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "PostProcessing.h"

class PostProcessingActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    PostProcessingActivator();

    QString pluginName() const override
    {
        return "PostProcessing";
    }
    void start() override
    {
        qInfo() << "[Base_PostProcessing] Start PostProcessing";
        PostProcessing* postProcessing = new PostProcessing;
    }
    void stop() override
    {
        qInfo() << "[Base_PostProcessing] Stopping...";
    }
};

#endif
