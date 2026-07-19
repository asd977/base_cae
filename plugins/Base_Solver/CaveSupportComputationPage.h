#pragma once

#include <BasePage.h>

#include <QVector>

class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QWidget;
class QDoubleSpinBox;
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

    QString sectionText();
    bool exportICaveDatFile(const QString& supportCaseSection) const;

    // 由实际“开挖体”节点数量统一控制开挖级数，避免 ICAVE 与 IIKE 文件数量不一致。
    void setExcavationLevelCount(int count);
    int excavationLevelCount() const;

    // Solver 计算前校验使用。界面显示中文选项，返回值仍是 Fortran 数字代码。
    int calculationTypeCode() const;
    int excavationLoadMethodCode() const;
    bool modalAnalysisEnabled() const;
    bool timeHistoryEnabled() const;
    bool validateInputs(QString* errorMessage) const;

    void loadLegacyCaveJson(const QJsonObject& obj);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();


  private slots:
    void onExcavationLevelCountChanged(int count);
    void onComputeClicked();
    void setComputing(bool computing);
    void updateAnalysisModeUi();
    void updateDynamicDetailsUi();
    void updateStaticDependenciesUi();


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

    void saveCurrentCase();
    void loadCaseToUi();
    QString controlFieldValue(int fieldNo) const;
    void setControlFieldValue(int fieldNo, const QString& value);

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
    QComboBox* m_ifModeCombo = nullptr;
    QComboBox* m_ifWaveCombo = nullptr;
    QDoubleSpinBox* m_dtSpin = nullptr;
    QDoubleSpinBox* m_tstaSpin = nullptr;
    QDoubleSpinBox* m_tendSpin = nullptr;
    QLineEdit* m_accXEdit = nullptr;
    QLineEdit* m_accYEdit = nullptr;
    QLineEdit* m_accZEdit = nullptr;
    CaseData m_caseData;
    bool m_isUpdatingUi = false;
};
