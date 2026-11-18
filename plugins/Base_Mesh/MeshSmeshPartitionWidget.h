#pragma once

#include <BasePage.h>
#include <QEvent>
#include <QVector>

#include "core/MeshSmeshWorkflowState.h"
#include "core/MeshControlSelectionPresentation.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;

class MeshSmeshPartitionWidget : public BasePage
{
    Q_OBJECT
public:
    explicit MeshSmeshPartitionWidget(QWidget* parent = nullptr);
    ~MeshSmeshPartitionWidget() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject&) override;
    void beforeLoad() override;
    void beforeUnload() override;
    void setMeshPlanIdentity(const QString& uuid, const QString& displayName);

signals:
    void logMessage(const QString& msg);
    void partitionChanged();

protected:
    bool event(QEvent* ev) override;

private:
    enum PickMode { PickNone = 0, PickTargetVolume, PickCadPlane, PickEdgePair, PickRegion };

    QObject* geometryBridge() const;
    void loadState();
    void saveState();
    void refreshRuleList();
    void refreshRegionList();
    void rebuildTargetVolumes();
    void beginTargetVolumePick();
    void snapCoordinateToTargetCenter();
    void addAxisRule();
    void updateSelectedAxisRule();
    void loadSelectedRuleToEditors();
    void beginCadPlanePick();
    void beginEdgePairPick();
    bool buildPlaneFromEdgePair(int edge1, int edge2, MeshSmeshWorkflowState::PartitionRule& rule, QString& error) const;
    void beginRegionPick();
    void removeSelectedRule();
    void clearRules();
    void previewPartition();
    void showCuttingPlanePreview();
    void showSelectedRegion();
    void restoreCad();
    void stopPick();
    QVariantList planes() const;

private:
    QComboBox* m_targetVolume = nullptr;
    QComboBox* m_axis = nullptr;
    QDoubleSpinBox* m_coordinate = nullptr;
    QListWidget* m_rules = nullptr;
    QListWidget* m_regions = nullptr;
    QLabel* m_ownerLabel = nullptr;
    QLabel* m_summary = nullptr;
    QPushButton* m_preview = nullptr;
    QVector<MeshSmeshWorkflowState::PartitionRule> m_ruleData;
    PickMode m_pickMode = PickNone;
    bool m_loadingRules = false;
    int m_firstEdgeId = -1;
    QString m_planUuid;
    QString m_planName;
    MeshControlSelectionPresentation m_selectionPresentation;
};
