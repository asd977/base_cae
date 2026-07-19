#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QPair>
#include <QVector>

class QLineEdit;
class QLabel;
class QPushButton;
class QComboBox;
class QGroupBox;
class QWidget;
class QPlainTextEdit;
class QTableWidget;
class QJsonArray;

namespace Ui
{
class FillStorageCyclePage;
}

class FillStorageCyclePage : public BasePage
{
    Q_OBJECT

  public:
    explicit FillStorageCyclePage(QWidget* parent = nullptr);
    ~FillStorageCyclePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QString sectionText() const;
    QString sectionText(int caseIndex) const;
    bool usesDamBaseUplift() const;
    bool usesLinearDamBaseUplift() const;
    bool usesDamFacePressure() const;
    void setDynamicOptionsAvailable(bool available);
    bool validateInputs(QString* errorMessage) const;
    QVector<QPair<int, int>> fillLayerRanges(QString* errorMessage = nullptr) const;
    void setFillLayerRange(int firstLayer, int lastLayer);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void requirementsChanged();

  private:
    void initValueEditors();
    QString valueOf(const QString& key) const;
    void setValueOf(const QString& key, const QString& value);
    QString tableItemText(QTableWidget* table, int row, int col, const QString& fallback) const;
    QJsonArray tableToJson(QTableWidget* table) const;
    void jsonToTable(const QJsonArray& arr, QTableWidget* table, int minRows);
    void updatePreview();
    void updateConditionalVisibility();
    QVector<QPair<int, int>> materialMappings(QTableWidget* table) const;
    void setMaterialMappings(QTableWidget* table, const QVector<QPair<int, int>>& mappings);
    void addMaterialMappingRow(QTableWidget* table, int sourceMaterial = 0,
                               int targetMaterial = 0);
    void removeSelectedMaterialMapping(QTableWidget* table);
    void refreshMaterialMappingOptions(QTableWidget* table);
    QMap<int, QString> materialSourceOptions() const;
    QMap<int, QString> materialTargetOptions(QTableWidget* table, int sourceMaterial) const;
    void populateMaterialCombo(QComboBox* combo, const QMap<int, QString>& options,
                               int currentMaterial, const QString& emptyText,
                               bool retainUnavailable) const;
    void refreshMaterialMappingHighlight(QTableWidget* table, int row);
    void clearMaterialMappingHighlight();
    void updateMaterialTableHeight(QTableWidget* table) const;
    bool validateMaterialMappings(QTableWidget* table, const QString& relationName,
                                  bool required, QString* errorMessage) const;

  private:
    Ui::FillStorageCyclePage* ui = nullptr;
    QMap<QString, QWidget*> m_editors;
    QTableWidget* m_wetTable = nullptr;
    QTableWidget* m_dynamicMaterialTable = nullptr;
    QTableWidget* m_dropTable = nullptr;
    QTableWidget* m_circleTable = nullptr;
    QGroupBox* m_dynamicMaterialGroup = nullptr;
    QWidget* m_dynamicMaterialControls = nullptr;
    QPushButton* m_addDynamicMaterialButton = nullptr;
    QPushButton* m_removeWetMaterialButton = nullptr;
    QPushButton* m_removeDropButton = nullptr;
    QPushButton* m_removeDynamicMaterialButton = nullptr;
    QLabel* m_dynamicMaterialHint = nullptr;
    QLabel* m_dynamicMaterialSummary = nullptr;
    QTableWidget* m_activeMaterialTable = nullptr;
    bool m_dynamicOptionsAvailable = true;
    bool m_materialHighlightActive = false;
};
