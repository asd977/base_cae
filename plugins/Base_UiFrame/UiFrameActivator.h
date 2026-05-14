#ifndef UIFRAMEACTIVATOR_H
#define UIFRAMEACTIVATOR_H

#include <PluginInterface.h>

#include <QDebug>

#include "UiFrame.h"

class UiFrameActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    UiFrameActivator();

    QString pluginName() const override
    {
        return "UiFrame";
    }
    void start() override
    {
        qInfo() << "Start UiFrame";
        UiFrame* uiFrame = new UiFrame;
    }
    void stop() override
    {
        qInfo() << "Stopping...";
    }
};

#endif // UIFRAMEACTIVATOR_H
