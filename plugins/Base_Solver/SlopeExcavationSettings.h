#pragma once
#include <QJsonObject>
#include <QVector>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QTabWidget;
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
    QJsonObject save();
    void load(const QJsonObject& state);
    void enter();
    void leave();
    CaveReinforcementFieldPage* reinforcement() const
    {
        return m_reinforcement;
    }
  signals:
    void initialStressRequested();

  private:
    void addStage(const QJsonObject& state = {});
    void updateState();
    void activateStage(int index);
    QCheckBox* m_enabled;
    QComboBox* m_support;
    QTabWidget* m_tabs;
    QWidget* m_content;
    CaveReinforcementFieldPage* m_reinforcement;
    CaveExcavationBodyPage* m_active = nullptr;
    bool m_entered = false;
};
