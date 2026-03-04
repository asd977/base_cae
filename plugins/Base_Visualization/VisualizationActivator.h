#ifndef VISUALIZATIONACTIVATOR_H
#define VISUALIZATIONACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>
#include <QObject>

#include "VisualizationWidget.h"

class VisualizationActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    VisualizationActivator();

    QString pluginName() const override
    {
        return "Visualization";
    }
    void start() override
    {
        qDebug() << "Start Visualization";
        VisualizationWidget* visualizationWidget = new VisualizationWidget;
    }
    void stop() override
    {
        qDebug() << "Stopping...";
    }
};

#endif // VISUALIZATIONACTIVATOR_H
