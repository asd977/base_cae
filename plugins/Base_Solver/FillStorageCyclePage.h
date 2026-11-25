#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>
#include <QMap>
#include <QPair>
#include <QPointer>
#include <QSet>
#include <QVector>

class QObject;
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
    bool usesImportedSeepageHead() const;
    bool usesImportedSeepageBody() const;
    bool needsSeepageAssociation() const;
    QString seepageSourceUuid() const;
    QString seepageSourceName() const;
    QString seepageResultType() const;
    QString seepageTransientStep() const;
    QString seepageTransientResultLabel() const;
    void setAvailableSeepageSources(const QVector<QPair<QString, QString>>& sources);
    void setAvailableTransientResults(const QVector<int>& steps, bool hasFinalResult,
                                      const QMap<int, QString>& labels = {});
    void setPreparedSeepageFiles(const QString& headFile, const QString& bodyFile);
    void clearPreparedSeepageFiles();
    // 父级“计算类型”只决定当前工况显示静力设置、动力设置或两者。
    // 1=仅静力，2=仅动力（复用已有静力结果），3=先静力后动力。
    void setAnalysisMode(int calculationType);
    // 4=均质坝（求解器固定忽略 IFIIWL_SEEP）；1=混凝土面板堆石坝（支持 0/1/2）。
    void setDamType(int damType);
    void setDynamicOptionsAvailable(bool available);
    // 动力分析现在由同一土石坝工况下的“动力分析”子节点维护。
    // 计算前将该节点状态同步到本页隐藏的求解器字段，以保持 IFILL.DAT 格式不变。
    void applyDynamicAnalysisState(const QJsonObject& state, bool dynamicAvailable);
    int modalExtractionOrder() const;
    void setModalExtractionOrder(int order);
    bool hasCreepCalculation() const;
    bool usesModalAnalysis() const;
    bool usesWaveAnalysis() const;
    bool usesPermanentSettlement() const;
    bool usesWaterDrop() const;
    bool usesWetting() const;
    // 当前工况实际参与动力计算的“静力材料 -> 动力材料”关系。
    QVector<QPair<int, int>> activeDynamicMaterialMappings() const;
    int cycleCount() const;
    QString finalStaticStateDirectoryName(int caseIndex, int maximumDamLayer) const;
    bool hasEffectiveWaterFill(int oneBasedCircle) const;
    bool lastCycleHasReservoirCreep() const;
    void setPreparedDynamicStateDirectory(const QString& directory);
    void clearPreparedDynamicStateDirectory();
    bool validateInputs(QString* errorMessage) const;
    QVector<QPair<int, int>> fillLayerRanges(QString* errorMessage = nullptr) const;
    void setFillLayerRange(int firstLayer, int lastLayer);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void requirementsChanged();
    // P030 是土石坝分析级统一参数，但用户入口与每个工况的模态开关放在一起。
    void modalExtractionOrderChanged(int order);

  private:
    void initValueEditors();
    QString valueOf(const QString& key) const;
    void setValueOf(const QString& key, const QString& value);
    QString tableItemText(QTableWidget* table, int row, int col, const QString& fallback) const;
    QJsonArray tableToJson(QTableWidget* table) const;
    void jsonToTable(const QJsonArray& arr, QTableWidget* table, int minRows);
    void updatePreview();
    void updateConditionalVisibility();
    void openConstructionStageEditor();
    void closeConstructionStageEditor();
    int constructionStagePreviewMode() const;
    void updateConstructionStageLabels();
    void updateConstructionStageSummary();
    void updateConstructionStagePreviewFromEditor();
    void syncConstructionStageEditorToSource();
    void insertConstructionStageRow(int insertAt, int sourceRow, bool duplicate);
    void removeConstructionStageRow(int row);
    void onConstructionStageCurrentCellChanged(int currentRow, int currentColumn,
                                               int previousRow, int previousColumn);
    void onConstructionStageCellChanged(int row, int column);
    void onConstructionStageModeChanged(int index);
    void addConstructionStage();
    void removeCurrentConstructionStage();
    void onConstructionStageEditorDestroyed(QObject* object);
    void updateConstructionStagePreview(QTableWidget* table, int row, int previewMode = 0);
    void clearConstructionStagePreview(bool renderNow = true);
    QVector<QPair<int, int>> materialMappings(QTableWidget* table) const;
    void setMaterialMappings(QTableWidget* table, const QVector<QPair<int, int>>& mappings);
    void addMaterialMappingRow(QTableWidget* table, int sourceMaterial = 0,
                               int targetMaterial = 0, int insertAt = -1);
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
    QPointer<QWidget> m_constructionStageEditor;
    QPointer<QTableWidget> m_constructionStageTable;
    QPointer<QComboBox> m_constructionStageModeCombo;
    QPointer<QLabel> m_constructionStageTitleLabel;
    QPointer<QLabel> m_constructionStageFillLabel;
    QPointer<QLabel> m_constructionStageWaterLabel;
    bool m_pageActive = false;
    QGroupBox* m_dynamicMaterialGroup = nullptr;
    QWidget* m_dynamicMaterialControls = nullptr;
    QPushButton* m_addDynamicMaterialButton = nullptr;
    QPushButton* m_removeWetMaterialButton = nullptr;
    QPushButton* m_removeDropButton = nullptr;
    QPushButton* m_removeDynamicMaterialButton = nullptr;
    QLabel* m_dynamicMaterialHint = nullptr;
    QLabel* m_dynamicMaterialSummary = nullptr;
    QTableWidget* m_activeMaterialTable = nullptr;
    bool m_staticOptionsAvailable = true;
    bool m_dynamicOptionsAvailable = true;
    int m_damType = 4;
    bool m_materialHighlightActive = false;
    QString m_savedSeepageSourceUuid;
    QString m_savedSeepageSourceName;
    QSet<QString> m_availableSeepageSourceUuids;
    QSet<int> m_availableTransientSteps;
    bool m_hasTransientFinalResult = false;
    QString m_savedSeepageTransientStep;
    QString m_preparedHeadFile;
    QString m_preparedBodyFile;
    QString m_preparedDynamicStateDirectory;
};
