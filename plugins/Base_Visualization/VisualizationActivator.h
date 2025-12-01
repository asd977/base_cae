#ifndef VISUALIZATIONACTIVATOR_H
#define VISUALIZATIONACTIVATOR_H
#include "Visualization.h"

#include <PluginInterface.h>
#include <QObject>
#include <QDebug>

VTK_MODULE_INIT(vtkRenderingFreeType);
VTK_MODULE_INIT(vtkRenderingOpenGL2);
VTK_MODULE_INIT(vtkInteractionStyle);


class VisualizationActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

public:
    VisualizationActivator();

    QString pluginName() const override { return "Visualization"; }
    void start() override { qDebug() << "Start Visualization"; }
    void stop() override { qDebug() << "Stopping..."; }
};

#endif // VISUALIZATIONACTIVATOR_H
