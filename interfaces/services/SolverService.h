#ifndef SOLVERSERVICE_H
#define SOLVERSERVICE_H

#include <QString>

struct SolverService
{
    virtual void newProject(QString workPath) = 0;
    virtual QStringList getValueStrings() = 0;
    virtual void setValueStrings(QStringList values) = 0;

    // 根据当前模式执行一次/二次/三次电流分布求解
    virtual void runCurrentDistribution(const QString &mode) = 0;
};


#endif // SOLVERSERVICE_H
