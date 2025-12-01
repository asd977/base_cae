#ifndef SURROGATEMODELACTIVATOR_H
#define SURROGATEMODELACTIVATOR_H

#include "SurrogateModel.h"

#include <PluginInterface.h>
#include <QDebug>


class SurrogateModelActivator: public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

public:
    SurrogateModelActivator();

    QString pluginName() const override { return "SurrogateModel"; }
    void start() override { qDebug() << "start SurrogateModel"; SurrogateModel *surrogateModel = new SurrogateModel;}
    void stop() override { qDebug() << "Stopping..."; }

};

#endif // SURROGATEMODELACTIVATOR_H
