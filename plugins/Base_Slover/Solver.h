#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>
#include "GeoStressRegressionWidget.h"
#include <QProcess>
#include <QPointer>

class CaveSupportComputationPage;
class CaveExcavationBodyPage;
class CaveBlockPlanePage;
class QWidget;

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
    void onCaveBlockStabilityAction();
    void onComputeSeepage(ProjectNode* analysisNode);
    void onComputeStressStrain(ProjectNode* analysisNode);
    void onComputeCaveExcavation(ProjectNode* analysisNode);
    void onComputeSlopeStability(ProjectNode* analysisNode);
    void onComputeGeoStressRegression(ProjectNode* analysisNode);
    void onComputeCaveBlockStability(ProjectNode* analysisNode);
    void stopSolverExecutable();

  private:
    void initAction();
    void initTreeMenu();
    void setupNodeRole(ProjectNode* node, const QString& kind) const;
    int countFillStorageCycles(ProjectNode* analysisNode) const;
    int countGeoStressChildren(ProjectNode* geostressNode, const QString& pageType) const;
    void syncDamAnalysisMCase(ProjectNode* analysisNode) const;
    void addFillStorageCycleNode(ProjectNode* analysisNode, bool setCurrent) const;
    void addGeoStressRegressionNode(ProjectNode* analysisNode, bool setCurrent);
    void addCaveSupportComputationNode(ProjectNode* analysisNode, bool setCurrent);
    void addCaveExcavationBodyNode(ProjectNode* supportNode, bool setCurrent) const;
    void addCaveBlockStabilityNode(ProjectNode* analysisNode, bool setCurrent);
    void addCaveBlockPlaneNode(ProjectNode* blockNode, bool setCurrent) const;
    void addGeoStressSideFaceNode(ProjectNode* geostressNode, bool setCurrent) const;
    void addGeoStressMeasurePointNode(ProjectNode* geostressNode, bool setCurrent) const;
    void addGeoStressOuterForceNode(ProjectNode* geostressNode, bool setCurrent) const;
    GeoStressRegressionWidget::ExportPayload buildGeoStressPayload(ProjectNode* geostressNode) const;
    bool beginCommonCompute(ProjectNode* analysisNode, const QString& analysisType);
    bool launchSolverExecutable(const QString& analysisType);
    bool launchCaveBlockSolverExecutable();
    void finishSolverProcess(bool terminateProcess);
    void setComputeWidgetRunning(QWidget* widget, bool computing) const;
    void connectStopRequest(QObject* page) const;
    int solverModeFromAnalysisType(const QString& analysisType) const;
    bool updateIiiDirDat(const QString& projectDir, int solverMode) const;
    bool updateCaveBlockIiiDirDat(const QString& sliderDir) const;
    bool updatePrepDatFiles(const QString& projectDir, int solverMode, int verticalAxis) const;
    bool forceSlopeIiinLayersToOne(const QString& projectDir) const;
    int nextSeepageIndex() const;
    int nextStressStrainIndex() const;
    int nextCaveExcavationIndex() const;
    int nextCaveBlockStabilityIndex() const;
    int nextSlopeIndex() const;
    void removeNodeByUuid(const QString& uuid);
    void deleteNodeRecursively(ProjectNode* node) const;

  private:
    ProjectNode* m_root = nullptr;
    QProcess* m_solverProcess = nullptr;
    QPointer<QWidget> m_pendingComputeWidget;
    QPointer<QWidget> m_activeComputeWidget;
    bool exportInouseDatFile();
    bool exportMaMaskDatFile();
    bool exportKetraceDatFile();
    bool exportJdtraceDatFile();
};

#endif // SOLVER_H
