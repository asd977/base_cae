#pragma once

#include <BasePage.h>

#include <QJsonObject>
#include <QString>

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
    QString baseVariablesText() const;
    QString upliftAndAxisText() const;
    QString rockStressText() const;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

  private:
    QString defaultBaseVariablesText() const;
    QString defaultUpliftAndAxisText() const;
    QString defaultRockStressText() const;
    QString defaultSectionText() const;

  private:
    Ui::FillStorageCyclePage* ui = nullptr;
};
