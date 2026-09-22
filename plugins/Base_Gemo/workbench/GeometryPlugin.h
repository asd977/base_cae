#ifndef GEOMETRYPLUGIN_H
#define GEOMETRYPLUGIN_H

#include <PluginInterface.h>
#include <QDebug>
#include <QObject>
#include "GeometryWorkbench.h"

class GeometryService;

class GeometryPlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    GeometryPlugin();

    QString pluginName() const override
    {
        // The launcher starts this slot under the historical plugin name.
        // Keep the external contract while using the new geometry workbench internally.
        return "PreProcessing";
    }
    void start() override;
    void stop() override;

  private:
    GeometryService* m_geometryService = nullptr;
    GeometryWorkbench* m_workbench = nullptr;
};

#endif // GEOMETRYPLUGIN_H
