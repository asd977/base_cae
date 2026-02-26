#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>

class Solver : public SolverService
{
public:
    Solver();

    void newProject() override;
    bool saveProject() override;
    bool loadProject() override;
    void closeProject() override;

private slots:
    void onSeepageAction();

private:
    void initAction();

private:
    ProjectNode* m_root = nullptr;
};

#endif // SOLVER_H
