#pragma once

#include <BasePage.h>
#include <SmeshWorkflowData.h>
#include <QEvent>
#include <QThread>
#include <QHash>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "core/MeshSmeshWorkflowState.h"
#include "core/MeshControlSelectionPresentation.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QGroupBox;

class MeshSmeshSweepWidget : public BasePage
{
    Q_OBJECT
public:
    explicit MeshSmeshSweepWidget(QWidget* parent = nullptr);
    ~MeshSmeshSweepWidget() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject&) override;
    SmeshSweepSettings settings() const;
    QVector<SmeshSweepSettings> tasks() const;
    QSet<int> assignedCadVolumeIds() const;
    void setDisplayName(const QString& name);
    void setPartitionPlanIdentity(const QString& uuid);
    void invalidatePartitionTasks();
    void beforeLoad() override;
    void beforeUnload() override;
    void setMeshPlanIdentity(const QString& uuid, const QString& displayName);
    void setBaseVolumeSize(double size);

public slots:
    void generateMesh();
    void quickCheckCurrentRegion();

signals:
    void logMessage(const QString& msg);
    void configurationChanged();
    void computeRequested();
    void partitionRequested();
    void sigLoadingStart(const QString& tipText);
    void sigLoadingStop(bool success, const QString& msg);

protected:
    bool event(QEvent* ev) override;

private:
    enum PickMode { PickNone = 0, PickObject, PickSource, PickTarget };

    QObject* geometryBridge() const;
    void loadState();
    void saveState();
    void normalizeSharedParameters();
    void syncSharedParameters();
    QSet<int> occupiedByOtherSweepNodes(bool partitioned) const;
    QSet<int> occupiedByVolumeSettings() const;
    void refreshMode();
    void refreshTable();
    void autoAssignLegacyDirections();
    void reconcileSweepDirections();
    QString directionKey(const MeshSmeshWorkflowState::SweepTask& task) const;
    QString directionLabel(const QVariantMap& candidate) const;
    QVariantList directionCandidates(const MeshSmeshWorkflowState::SweepTask& task, bool force = false);
    void fillDirectionCombo(QComboBox* combo, int row);
    void applyTaskToEditors(int row);
    MeshSmeshWorkflowState::SweepTask currentTask() const;
    bool validateTask(const MeshSmeshWorkflowState::SweepTask& task, QString* error = nullptr) const;
    QVariantList planes() const;
    bool hasPartition() const;
    void beginObjectPick();
    void beginFacePick(bool source);
    void stopPick();
    void refreshPickDisplay();
    void showCurrentPartitionSelection();
    QString partitionFaceDisplayText(int faceId) const;
    void autoAnalyze();
    void populateDirections(bool notify);
    void syncCurrentTask();
    void batchAddCadVolumes();
    void syncAllPartitionRegions();
    void removeTask();
    void clearTasks();
    void generate(bool localDiagnostic = false, const QVector<MeshSmeshWorkflowState::SweepTask>& forcedTasks = {});

private:
    QComboBox* m_objectMode = nullptr;
    QComboBox* m_direction = nullptr;
    QSpinBox* m_objectId = nullptr;
    QSpinBox* m_sourceFace = nullptr;
    QSpinBox* m_targetFace = nullptr;
    QSpinBox* m_layers = nullptr;
    QDoubleSpinBox* m_sourceSize = nullptr;
    QCheckBox* m_quadSource = nullptr;
    QLabel* m_ownerLabel = nullptr;
    QLabel* m_hint = nullptr;
    QLabel* m_objectDisplay = nullptr;
    QPushButton* m_objectPickButton = nullptr;
    QPushButton* m_sourcePickButton = nullptr;
    QPushButton* m_targetPickButton = nullptr;
    QTableWidget* m_table = nullptr;
    QGroupBox* m_taskGroup = nullptr;
    QHash<QString, QVariantList> m_directionCandidates;
    QSet<QString> m_checkedDirectionKeys;
    QPushButton* m_batchAddCad = nullptr;
    QPushButton* m_syncRegions = nullptr;
    QPushButton* m_partitionButton = nullptr;
    QPushButton* m_quickCheckButton = nullptr;
    double m_baseVolumeSize = 5.0;
    QVector<MeshSmeshWorkflowState::SweepTask> m_tasks;
    int m_groupAxis = -1; // A mesh task does not mix X/Y/Z sweep axes.
    bool m_reconcilingDirections = false;
    bool m_restoringTask = false; // Block auto-save while a table row is being loaded.
    PickMode m_pickMode = PickNone;
    QThread* m_thread = nullptr;
    QString m_partitionPlanUuid;
    QString m_displayName=QStringLiteral("扫掠网格");
    QString m_planUuid;
    QString m_planName;
    MeshControlSelectionPresentation m_selectionPresentation;
};
