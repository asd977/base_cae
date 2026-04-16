#pragma once

#include <BasePage.h>

#include <QVector>

class QComboBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;
class QWidget;

class CaveSupportComputationPage : public BasePage
{
    Q_OBJECT

  public:
    explicit CaveSupportComputationPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QString sectionText();

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onCaseChanged(int index);
    void onCaseCountChanged(int count);
    void onAddMaterialRow();
    void onDeleteMaterialRow();
    void onExcavationLevelCountChanged(int count);

  private:
    struct CaseData
    {
        int icase = 1;
        QVector<QPair<QString, QString>> materialChanges;
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
    void loadCaseToUi(int index);
    void refreshCaseSelector();
    void ensureCaseCount(int count);
    CaseData defaultCase(int index) const;
    QVector<QPair<QString, QString>> transitionsFromLibrary() const;

  private:
    QSpinBox* m_ncaseSpin = nullptr;
    QSpinBox* m_iexcaMaxSpin = nullptr;
    QTableWidget* m_excavationDaysTable = nullptr;
    QComboBox* m_caseSelector = nullptr;
    QLineEdit* m_icaseEdit = nullptr;
    QTableWidget* m_materialTable = nullptr;
    QLineEdit* m_iexcaStaEdit = nullptr;
    QLineEdit* m_iexcaEndEdit = nullptr;
    QComboBox* m_ifEallCombo = nullptr;
    QComboBox* m_ifJallCombo = nullptr;
    QComboBox* m_ifE1toaCombo = nullptr;
    QComboBox* m_ifJ1toaCombo = nullptr;
    QComboBox* m_jaMethodCombo = nullptr;
    QComboBox* m_ifChenCombo = nullptr;
    QLineEdit* m_idayCaseEdit = nullptr;

    QVector<CaseData> m_cases;
    int m_currentCaseIndex = 0;
    bool m_isUpdatingUi = false;
};
