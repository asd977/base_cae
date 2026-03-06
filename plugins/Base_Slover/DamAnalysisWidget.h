#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>

class QFormLayout;
class QLineEdit;
class QTableWidget;
class QLabel;
class QPushButton;

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

  private slots:
    void onComputeClicked();

  private:
    void initValueEditors();
    QString quoteValue(const QString& value) const;
    QString defaultValueByLine(int lineNo) const;
    QString descriptionByLine(int lineNo) const;
    QString normalizeEnumValue(const QString& rawValue) const;
    bool validateInputs(QString* errorMessage) const;
    void updateConfigSummary();

    Ui::DamAnalysisWidget* ui;
    QFormLayout* m_formLayout = nullptr;
    QTableWidget* m_baseTable = nullptr;
    QLineEdit* m_editX = nullptr;
    QLineEdit* m_editAlfa = nullptr;
    QLineEdit* m_editAxis = nullptr;
    QLineEdit* m_editRock = nullptr;
    QLineEdit* m_editMCase = nullptr;
    QLabel* m_summaryLabel = nullptr;
    QPushButton* m_btnValidation = nullptr;
};
