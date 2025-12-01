#ifndef UIFRAMEACTIVATOR_H
#define UIFRAMEACTIVATOR_H

#include "UiFrame.h"

#include <PluginInterface.h>
#include <QDebug>

class UiFrameActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

public:
    UiFrameActivator();

    QString pluginName() const override { return "UiFrame"; }
    void start() override
    {
        qDebug() << "Start UiFrame";
        UiFrame *uiFrame = new UiFrame;

    }
    void stop() override { qDebug() << "Stopping..."; }

};

#endif // UIFRAMEACTIVATOR_H
