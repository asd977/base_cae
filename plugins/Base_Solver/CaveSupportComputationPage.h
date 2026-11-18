#pragma once

#include <BasePage.h>
#include <QPointer>

#include <QString>
#include <QVariantMap>
#include <QVector>

class QCheckBox;
class QuietBoundarySelector;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QWidget;
class CaveReinforcementFieldPage;
namespace Ui
{
class CaveSupportComputationPage;
}

class CaveSupportComputationPage : public BasePage
{
    Q_OBJECT

  public:
    explicit CaveSupportComputationPage(QWidget* parent = nullptr);
    ~CaveSupportComputationPage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    Q_INVOKABLE QString sectionText();
    bool exportICaveDatFile(const QString& supportCaseSection) const;

    // 由实际“开挖体”节点数量统一控制开挖级数，避免 ICAVE 与开挖文件数量不一致。
    void setExcavationLevelCount(int count);
    int excavationLevelCount() const;

    // Solver 计算前校验和内部输入转换使用。界面只显示业务概念。
    int calculationTypeCode() const;
    int excavationLoadMethodCode() const;
    bool modalAnalysisEnabled() const;
    int modalCount() const;
    bool timeHistoryEnabled() const;
    bool reinforcedTaskEnabled() const;
    int groundwaterModeCode() const;
    bool liningEnabled() const;
    bool craneBeamEnabled() const;
    int craneBeamInstallStage() const;
    double craneBeamReleaseFactor() const;
    CaveReinforcementFieldPage* reinforcementFieldPage() const;

    // 自动读取当前工程最近完成且网格一致的渗流结果，并转换成洞室求解输入。
    // 求解器内部文件名不会暴露到界面。
    bool prepareAuxiliaryInputFiles(const QString& inputDirectory,
                                    QString* errorMessage = nullptr) const;

    void loadLegacyReinforcementFieldJson(const QJsonObject& obj);
    bool validateInputs(QString* errorMessage) const;
    void loadLegacyCaveJson(const QJsonObject& obj);
    QJsonObject legacyCraneBeamAreaState() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();
    void liningEnabledChanged(bool enabled);
    void craneBeamEnabledChanged(bool enabled);

  private slots:
    void onExcavationLevelCountChanged(int count);
    void onComputeClicked();
    void setComputing(bool computing);
    void updateAnalysisModeUi();
    void updateDynamicDetailsUi();
    void updateStaticDependenciesUi();
    void onReinforcedTaskSelectionChanged();

  private:
    struct CaseData
    {
        int icase = 1;
        QString iexcaSta = "-1";
        QString iexcaEnd = "-3";
        QString ifEall = "1";
        QString ifJall = "0";
        QString ifE1toa = "0";
        QString ifJ1toa = "0";
        QString jaMethod = "1";
        QString ifChen = "0";
        QString idayCase = "0";
    };

    void setupBusinessUi();
    void hideInternalControls();
    void saveCurrentCase();
    void loadCaseToUi();
    QString controlFieldValue(int fieldNo) const;
    void setControlFieldValue(int fieldNo, const QString& value);

    QVector<QVariantMap> availableSeepageResults() const;
    void updateCraneBeamInstallStages();
    void refreshCraneBeamUi();
    QString selectedSeepageResult(QString* analysisName = nullptr) const;
    bool validateSeepageResult(const QString& path, QString* errorMessage = nullptr) const;
    void refreshLiningStatus();
    void refreshGroundwaterStatus();

  private:
    Ui::CaveSupportComputationPage* ui = nullptr;

    QSpinBox* m_iexcaMaxSpin = nullptr;
    QTableWidget* m_excavationDaysTable = nullptr;
    QLineEdit* m_iexcaStaEdit = nullptr;
    QLineEdit* m_iexcaEndEdit = nullptr;
    QComboBox* m_ifSeepCombo = nullptr;
    QComboBox* m_dampingModeCombo = nullptr;
    QComboBox* m_calculationTypeCombo = nullptr;
    QComboBox* m_excavationLoadMethodCombo = nullptr;
    QComboBox* m_iterationMethodCombo = nullptr;
    QSpinBox* m_iterationCountSpin = nullptr;
    QComboBox* m_ifEallCombo = nullptr;
    QComboBox* m_ifJallCombo = nullptr;
    QComboBox* m_ifE1toaCombo = nullptr;
    QComboBox* m_ifJ1toaCombo = nullptr;
    QComboBox* m_jaMethodCombo = nullptr;
    QComboBox* m_ifChenCombo = nullptr;
    QLineEdit* m_idayCaseEdit = nullptr;
    QSpinBox* m_modalCountSpin = nullptr;
    QComboBox* m_ifWaveCombo = nullptr;
    QDoubleSpinBox* m_dtSpin = nullptr;
    QDoubleSpinBox* m_tstaSpin = nullptr;
    QDoubleSpinBox* m_tendSpin = nullptr;
    QLineEdit* m_accXEdit = nullptr;
    QLineEdit* m_accYEdit = nullptr;
    QLineEdit* m_accZEdit = nullptr;

    QGroupBox* m_liningGroundwaterGroup = nullptr;
    QPointer<QCheckBox> m_quietBoundaryCheck = nullptr;
    QPointer<QuietBoundarySelector> m_quietBoundarySelection = nullptr;
    QPointer<QLabel> m_quietBoundaryStatus = nullptr;
    QGroupBox* m_craneBeamGroup = nullptr;
    QCheckBox* m_liningEnabledCheck = nullptr;
    QLabel* m_liningStatusLabel = nullptr;
    QCheckBox* m_groundwaterCheck = nullptr;
    QLabel* m_groundwaterStatusLabel = nullptr;

    QCheckBox* m_craneBeamEnabledCheck = nullptr;
    QComboBox* m_craneBeamInstallStageCombo = nullptr;
    QDoubleSpinBox* m_craneBeamReleaseSpin = nullptr;
    QLabel* m_craneBeamStatusLabel = nullptr;

    CaveReinforcementFieldPage* m_reinforcementFieldPage = nullptr;
    CaseData m_caseData;
    bool m_isUpdatingUi = false;
    QJsonObject m_legacyCraneBeamAreaState;
};
