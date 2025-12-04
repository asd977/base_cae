#ifndef SOLVERSERVICE_H
#define SOLVERSERVICE_H

struct SolverService
{
    virtual void newProject() = 0;
    virtual bool saveProject() = 0;
    virtual bool loadProject() = 0;
    virtual void closeProject() = 0;
};


#endif // SOLVERSERVICE_H
