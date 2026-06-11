#pragma once

#include <BasePage.h>

#include <QVector>

class QComboBox;
class QLineEdit;
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


  private:
    struct CaseData
    {
        int icase = 1;
        QString iexcaSta = "-1";
        QString iexcaEnd = "-3";
        QString ifEall = "1";
        QString ifJall = "1";
        QString ifE1toa = "1";
        QString ifJ1toa = "1";
        QString jaMethod = "1";
        QString ifChen = "0";
        QString idayCase = "0";
    };

    void saveCurrentCase();
    void loadCaseToUi();

  private:
    Ui::CaveSupportComputationPage* ui = nullptr;

    QSpinBox* m_iexcaMaxSpin = nullptr;
    QTableWidget* m_excavationDaysTable = nullptr;
    QLineEdit* m_iexcaStaEdit = nullptr;
    QLineEdit* m_iexcaEndEdit = nullptr;
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
