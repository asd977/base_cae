#pragma once

#include <BasePage.h>

class QLineEdit;
class QPlainTextEdit;
class QWidget;

class CaveSupportComputationPage : public BasePage
{
    Q_OBJECT

  public:
    explicit CaveSupportComputationPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QString sectionText() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private slots:
    void onRegenerateTemplate();

  private:
    QString buildTemplate(int ncase) const;

  private:
    QLineEdit* m_ncaseEdit = nullptr;
    QPlainTextEdit* m_caseBlocksEdit = nullptr;
};
