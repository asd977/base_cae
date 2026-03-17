#ifndef DISPLAYACTIVATOR_H
#define DISPLAYACTIVATOR_H

#include <PluginInterface.h>

#include <QObject>

class DisplayActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    QString pluginName() const override
    {
        return "Display";
    }

    void start() override;
    void stop() override;
};

#endif // DISPLAYACTIVATOR_H
