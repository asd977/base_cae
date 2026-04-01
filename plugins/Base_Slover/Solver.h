#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>

class Solver : public QObject, public SolverService
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
    void onCaveExcavationAction();
    void onSlopeStabilityAction();
    void onComputeSeepage(ProjectNode* analysisNode);
    void onComputeStressStrain(ProjectNode* analysisNode);
    void onComputeCaveExcavation(ProjectNode* analysisNode);
    void onComputeSlopeStability(ProjectNode* analysisNode);
    void onComputeGeoStressRegression(ProjectNode* analysisNode);

  private:
    void initAction();
    void initTreeMenu();
    void setupNodeRole(ProjectNode* node, const QString& kind) const;
    int countFillStorageCycles(ProjectNode* analysisNode) const;
    void syncDamAnalysisMCase(ProjectNode* analysisNode) const;
    void addFillStorageCycleNode(ProjectNode* analysisNode, bool setCurrent) const;
    void addGeoStressRegressionNode(ProjectNode* analysisNode, bool setCurrent);
    int nextSeepageIndex() const;
    int nextStressStrainIndex() const;
    int nextCaveExcavationIndex() const;
    int nextSlopeIndex() const;

  private:
    ProjectNode* m_root = nullptr;
};

#endif // SOLVER_H
