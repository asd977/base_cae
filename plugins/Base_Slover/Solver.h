#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>
class Solver :public SolverService
{
public:
    Solver();


    void newProject();
    bool saveProject();
    bool loadProject();
    void closeProject();

private slots:
    void onSeepageAction();
private:
    ProjectNode *m_root;

    void initAction();
};

#endif // SOLVER_H
