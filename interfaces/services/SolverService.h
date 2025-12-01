#ifndef SOLVERSERVICE_H
#define SOLVERSERVICE_H

#include <QString>

struct SolverService
{
    virtual void newProject(QString workPath) = 0;
    virtual QStringList getValueStrings() = 0;
    virtual void setValueStrings(QStringList values) = 0;
};


#endif // SOLVERSERVICE_H
