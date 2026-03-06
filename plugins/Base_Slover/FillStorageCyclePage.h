#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>
#include <QMap>

class QFormLayout;
class QLineEdit;
class QPlainTextEdit;

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

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private:
    void initValueEditors();
    QString valueOf(const QString& key) const;
    void setValueOf(const QString& key, const QString& value);
    void updatePreview();
  private:
    Ui::FillStorageCyclePage* ui = nullptr;
    QFormLayout* m_formLayout = nullptr;
    QMap<QString, QLineEdit*> m_edits;
};
