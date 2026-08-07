#ifndef SOLVER_H
#define SOLVER_H

#include <ProjectNode.h>
#include <services/SolverService.h>
#include "GeoStressRegressionWidget.h"
#include <QProcess>
#include <QPointer>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

class CaveSupportComputationPage;
class CaveExcavationBodyPage;
class CaveCraneBeamAreaPage;
class CaveReinforcementFieldPage;
class CaveBlockPlanePage;
class CaveExcavationWidget;
class DamAnalysisWidget;
class FillStorageCyclePage;
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
    void onBatchComputeRequested(const QStringList& analysisUuids);
    void onBatchStopRequested();
    void stopSolverExecutable();

  private:
    void initAction();
    void initTreeMenu();
    void setupNodeRole(ProjectNode* node, const QString& kind) const;
    void connectCaveOverview(CaveExcavationWidget* page, ProjectNode* analysisNode) const;
    ProjectNode* findChildByPageType(ProjectNode* parent, const QString& pageType) const;
    bool canDeleteNode(ProjectNode* node) const;
    void moveNodeByUuid(const QString& uuid, int direction);
    void normalizeChildNodeOrder(ProjectNode* parent) const;
    void refreshRootOverview() const;
    QVector<ProjectNode*> batchComputableNodes() const;
    QString batchNodeDisplayName(ProjectNode* node) const;
    bool dispatchBatchCompute(ProjectNode* node);
    void startNextBatchCompute();
    void completeBatchItem(bool succeeded, const QString& detail = {});
    void finishBatchCompute(bool cancelled);
    void connectBoundaryStatus(ProjectNode* node) const;
    void syncSeepageConditionalNodes(ProjectNode* analysisNode) const;
    void syncDamConditionalNodes(ProjectNode* analysisNode) const;
    QVector<QPair<QString, QString>> seepageAnalysisSources() const;
    ProjectNode* findAnalysisNodeByUuid(const QString& uuid) const;
    bool prepareDamSeepageFiles(FillStorageCyclePage* cyclePage, int cycleIndex,
                                const QString& projectDir, QString* errorMessage) const;
    void syncCaveSupportConditionalNodes(ProjectNode* supportNode) const;
    void updateBoundaryNodeStatus(ProjectNode* node, int selectionCount, bool enabled = true,
                                  const QString& disabledReason = {}) const;
    int countFillStorageCycles(ProjectNode* analysisNode) const;
    int countGeoStressChildren(ProjectNode* geostressNode, const QString& pageType) const;
    int minimumGeoStressSideFaceCount() const;
    void syncAnalysisNodeNames() const;
    void syncChildNodeNames(ProjectNode* parent) const;
    void renameNodeByUuid(const QString& uuid);
    void syncDamAnalysisMCase(ProjectNode* analysisNode) const;
    void connectDamLayerUpdates(DamAnalysisWidget* page, ProjectNode* analysisNode) const;
    void addFillStorageCycleNode(ProjectNode* analysisNode, bool setCurrent) const;
    void addGeoStressRegressionNode(ProjectNode* analysisNode, bool setCurrent);
    void addCaveSupportComputationNode(ProjectNode* analysisNode, bool setCurrent);
    void addCaveLiningBoundaryNode(ProjectNode* supportNode, bool innerBoundary,
                                   bool setCurrent) const;
    void addCaveCraneBeamAreaNode(ProjectNode* supportNode, bool setCurrent) const;
    void addCaveExcavationBodyNode(ProjectNode* supportNode, bool setCurrent) const;
    void addCaveBlockPlaneNode(ProjectNode* blockNode, bool setCurrent) const;
    void addGeoStressSideFaceNode(ProjectNode* geostressNode, bool setCurrent) const;
    void addGeoStressMeasurePointNode(ProjectNode* geostressNode, bool setCurrent) const;
    void addGeoStressOuterForceNode(ProjectNode* geostressNode, bool setCurrent) const;
    GeoStressRegressionWidget::ExportPayload buildGeoStressPayload(ProjectNode* geostressNode) const;
    bool beginCommonCompute(ProjectNode* analysisNode, const QString& analysisType);
    ProjectNode* topLevelAnalysisNode(ProjectNode* node) const;
    QString analysisWorkspace(ProjectNode* node) const;
    bool prepareAnalysisWorkspace(ProjectNode* node, bool caveBlock, QString* workspaceDir,
                                  QString* runDir) const;
    bool writeAnalysisManifest(const QString& workspaceDir, const QString& analysisUuid,
                               const QString& analysisName, const QString& analysisKind,
                               const QString& analysisType, const QString& state) const;
    void updateAnalysisManifestName(ProjectNode* node) const;
    bool launchSolverExecutable(ProjectNode* analysisNode, const QString& analysisType);
    bool launchCaveBlockSolverExecutable(ProjectNode* analysisNode);
    void finishSolverProcess(bool terminateProcess);
    void consumeSolverOutput();
    void handleSolverProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void clearSolverRunDirectory() const;
    void notifySolverFinished();
    void setComputeWidgetRunning(QWidget* widget, bool computing) const;
    void connectStopRequest(QObject* page) const;
    int solverModeFromAnalysisType(const QString& analysisType) const;
    bool updateIiiDirDat(const QString& projectDir, int solverMode, int deviceCode,
                         int assemblyLogInterval, int solverLogInterval) const;
    bool updateCaveBlockIiiDirDat(const QString& sliderDir, int assemblyLogInterval,
                                  int solverLogInterval) const;
    bool updatePrepDatFiles(const QString& projectDir, int solverMode, int verticalAxis) const;
    bool normalizeGeoStressSolidLayers(QString* errorMessage) const;
    bool forceIiinLayersToOne(const QString& analysisDir) const;
    void removeNodeByUuid(const QString& uuid);
    void deleteNodeRecursively(ProjectNode* node) const;

  private:
    ProjectNode* m_root = nullptr;
    QProcess* m_solverProcess = nullptr;
    QPointer<QWidget> m_pendingComputeWidget;
    QPointer<QWidget> m_activeComputeWidget;
    bool m_solverFinishedNotified{false};
    QString m_solverFailureDetail;
    qint64 m_solverStartedAtMs{0};
    QString m_activeProjectDir;
    QString m_activeAnalysisWorkspace;
    QString m_activeAnalysisUuid;
    QString m_activeAnalysisName;
    QString m_activeAnalysisKind;
    QString m_activeAnalysisType;
    QStringList m_batchQueue;
    QString m_batchCurrentUuid;
    int m_batchTotal{0};
    int m_batchFinished{0};
    int m_batchFailed{0};
    bool m_batchActive{false};
    bool m_batchDispatching{false};
    bool exportInouseDatFile();
    bool exportMaMaskDatFile();
    bool exportKetraceDatFile();
    bool exportJdtraceDatFile();
};

#endif // SOLVER_H
