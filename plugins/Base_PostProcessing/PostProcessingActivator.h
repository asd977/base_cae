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


    QString pluginName() const override { return "PostProcessing"; }
    void start() override { qDebug() << "start PostProcessing"; PostProcessing *postProcessing = new PostProcessing;}
    void stop() override { qDebug() << "Stopping..."; }
};

#endif
