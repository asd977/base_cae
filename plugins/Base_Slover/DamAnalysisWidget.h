#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QMap>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;
class QWidget;
class QComboBox;

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
    void setMCaseCount(int mcase);

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  signals:
    void computeRequested();
    void stopRequested();


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
    bool validateInputs(QString* errorMessage) const;
    void updateAnalysisModeVisibility();
    void updateDynamicOptionVisibility();

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
};
