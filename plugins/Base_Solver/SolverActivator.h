#ifndef SOLVERACTIVATOR_H
#define SOLVERACTIVATOR_H

#include <PluginInterface.h>

#include <QObject>
#include <QtDebug>

#include "Solver.h"

class SolverActivator : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PluginInterface_iid)
    Q_INTERFACES(PluginInterface)

  public:
    SolverActivator();

    QString pluginName() const override
    {
        return "Solver";
    }
    void start() override
    {
        qInfo() << "启动求解器模块";
        Solver* solver = new Solver;
    }
    void stop() override
    {
        qInfo() << "停止求解器模块";
    }
};
#endif // SOLVERACTIVATOR_H
