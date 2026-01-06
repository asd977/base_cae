#ifndef PREPROCESSINGACTIVATOR_H
#define PREPROCESSINGACTIVATOR_H

#include "PreProcessing.h"

#include <PluginInterface.h>
#include <QObject>
#include <QDebug>


class PreProcessingActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

public:
    PreProcessingActivator();

    QString pluginName() const override { return "PreProcessing"; }
    void start() override { qDebug() << "Start PreProcessing"; PreProcessing *preProcessing = new PreProcessing;}
    void stop() override { qDebug() << "Stopping..."; }
};

#endif // VISUALIZATIONACTIVATOR_H
