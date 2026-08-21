#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QMap>
#include <QPair>
#include <QVector>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTableWidget;

namespace Ui
{
class DamDynamicAnalysisPage;
}

class DamDynamicAnalysisPage : public BasePage
{
    Q_OBJECT

  public:
    explicit DamDynamicAnalysisPage(QWidget* parent = nullptr);
    ~DamDynamicAnalysisPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool usesModalAnalysis() const;
    bool usesWaveAnalysis() const;
    bool usesPermanentSettlement() const;
    int modalExtractionOrder() const;
    void setModalExtractionOrder(int order);
    QVector<QPair<int, int>> activeDynamicMaterialMappings() const;
    bool validateInputs(QString* errorMessage) const;

    void setAnalysisEnabled(bool enabled);
    QJsonObject solverState() const;
    void loadLegacyFillStorageState(const QJsonObject& legacyState);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void requirementsChanged();
    void modalExtractionOrderChanged(int order);

  private:
    QString comboValue(QComboBox* combo) const;
    void setComboValue(QComboBox* combo, const QString& value);
    QVector<QPair<int, int>> materialMappings() const;
    void setMaterialMappings(const QVector<QPair<int, int>>& mappings);
    void addMaterialMappingRow(int sourceMaterial = 0, int targetMaterial = 0, int insertAt = -1);
    void removeSelectedMaterialMapping();
    void refreshMaterialMappingOptions();
    QMap<int, QString> sourceMaterialOptions() const;
    QMap<int, QString> targetMaterialOptions() const;
    void populateMaterialCombo(QComboBox* combo, const QMap<int, QString>& options,
                               int currentMaterial, const QString& emptyText,
                               bool retainUnavailable) const;
    void refreshMaterialMappingHighlight(int row);
    void clearMaterialMappingHighlight();
    void updateConditionalState();
    void updateTableHeight();
    QJsonObject normalizedState(const QJsonObject& obj) const;
    QJsonObject currentState() const;

    Ui::DamDynamicAnalysisPage* ui = nullptr;
    QTableWidget* m_materialTable = nullptr;
    QPushButton* m_addButton = nullptr;
    QPushButton* m_removeButton = nullptr;
    bool m_analysisEnabled = true;
    bool m_materialHighlightActive = false;
};
