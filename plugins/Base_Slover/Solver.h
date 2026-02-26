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
    void onComputeSeepage(ProjectNode* analysisNode);

private:
    void initAction();
    int nextSeepageIndex() const;

private:
    ProjectNode* m_root = nullptr;
};

#endif // SOLVER_H
