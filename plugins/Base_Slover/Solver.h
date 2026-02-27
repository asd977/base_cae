#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>

class Solver : public QObject,  public SolverService
{
    Q_OBJECT

public:
    Solver();

    void newProject() override;
    bool saveProject() override;
    bool loadProject() override;
    void closeProject() override;

private slots:
    void onSeepageAction();
    void onStressStrainAction();
    void onComputeSeepage(ProjectNode* analysisNode);
    void onComputeStressStrain(ProjectNode* analysisNode);

private:
    void initAction();
    int nextSeepageIndex() const;
    int nextStressStrainIndex() const;

private:
    ProjectNode* m_root = nullptr;
};

#endif // SOLVER_H
