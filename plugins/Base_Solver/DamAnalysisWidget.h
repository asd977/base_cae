#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QMap>
#include <QString>

class QLineEdit;
class QWidget;
class QComboBox;
class QGroupBox;

namespace Ui
{
class DamAnalysisWidget;
}

class DamAnalysisWidget : public BasePage
{
    Q_OBJECT
  public:
    explicit DamAnalysisWidget(QWidget* parent = nullptr);
    ~DamAnalysisWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    bool exportIfillDatFile(int mcase, const QString& cycleSectionText) const;
    bool validateInputs(QString* errorMessage) const;
    void setMCaseCount(int mcase);
    bool freezeThawEnabled() const;
    bool dynamicAnalysisEnabled() const;
    void setUpliftReductionRequired(bool required);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();
    void requirementsChanged();


  private slots:
    void onComputeClicked();
    void setComputing(bool computing);


  private:
    void initValueEditors();
    QString quoteValue(const QString& value) const;
    QString defaultValueByLine(int lineNo) const;
    QString descriptionByLine(int lineNo) const;
    QString normalizeEnumValue(const QString& rawValue) const;
    QString lineValue(int lineNo) const;
    void setLineValue(int lineNo, const QString& value);
    bool isFixedFileLine(int lineNo) const;
    void updateAnalysisModeVisibility();
    void updateDynamicOptionVisibility();
    void updateFreezeThawOptionState();
    void updateUpliftOptionState();

    Ui::DamAnalysisWidget* ui;
    QMap<int, QWidget*> m_lineEditors;
    QComboBox* m_comboMethodIter = nullptr;
    QLineEdit* m_editIter = nullptr;
    QLineEdit* m_editX1 = nullptr;
    QLineEdit* m_editX2 = nullptr;
    QLineEdit* m_editX3 = nullptr;
    QLineEdit* m_editAlfa1 = nullptr;
    QLineEdit* m_editAlfa2 = nullptr;
    QLineEdit* m_editAlfa3 = nullptr;
    QComboBox* m_comboAxisUp = nullptr;
    QComboBox* m_comboAxisDown = nullptr;
    QComboBox* m_comboIfRock = nullptr;
    QComboBox* m_comboIfIso = nullptr;
    QGroupBox* m_groupFreezeThaw = nullptr;
    QLineEdit* m_editFreezeCycles = nullptr;
    QLineEdit* m_editFreezeCoefficient = nullptr;
    QLineEdit* m_editFreezeElevation = nullptr;
    QLineEdit* m_editFreezeCompression = nullptr;
    QLineEdit* m_editFreezeTension = nullptr;
    QLineEdit* m_editIceExpansion = nullptr;
    bool m_upliftReductionRequired = false;
};
