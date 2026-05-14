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
        qInfo() << "[Base_Slover] Start Solver";
        Solver* solver = new Solver;
    }
    void stop() override
    {
        qInfo() << "[Base_Slover] Stopping...";
    }
};
#endif // SOLVERACTIVATOR_H
