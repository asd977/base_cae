#pragma once

#include <QJsonObject>
#include <QPointer>
#include <QVector>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class CaveExcavationBodyPage;
class CaveReinforcementFieldPage;

class SlopeExcavationSettings : public QWidget
{
    Q_OBJECT
  public:
    explicit SlopeExcavationSettings(QWidget* parent = nullptr);
    ~SlopeExcavationSettings() override;

    bool enabled() const;
    bool reinforced() const;
    QVector<QVector<int>> selectedStages();
    int stageCount() const;
    CaveExcavationBodyPage* stagePage(int index) const;
    CaveReinforcementFieldPage* reinforcement() const;
    QWidget* storageHost() const { return m_storageHost; }

    int addStage(const QJsonObject& state = {}, bool emitSignals = true);
    bool removeStage(int index, bool emitSignals = true);
    bool moveStage(int index, int delta, bool emitSignals = true);

    QJsonObject save();
    void load(const QJsonObject& state);
    void refreshSummary();

    // 兼容旧调用：拾取生命周期已交由工程树中的独立开挖级节点管理。
    void enter();
    void leave();

  signals:
    void initialStressRequested();
    void structureChanged();
    void navigateToStageRequested(int index);
    void navigateToReinforcementRequested();

  private:
    void updateState();
    void renumberStages();

    QCheckBox* m_enabled = nullptr;
    QComboBox* m_support = nullptr;
    QWidget* m_content = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_removeButton = nullptr;
    QPushButton* m_reinforcementButton = nullptr;
    QWidget* m_storageHost = nullptr;
    QVector<QPointer<CaveExcavationBodyPage>> m_stages;
    QPointer<CaveReinforcementFieldPage> m_reinforcement;
    bool m_loading = false;
};
