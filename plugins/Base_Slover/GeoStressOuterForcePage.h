#pragma once

#include <BasePage.h>

class QComboBox;
class QLineEdit;

class GeoStressOuterForcePage : public BasePage
{
    Q_OBJECT
  public:
    explicit GeoStressOuterForcePage(QWidget* parent = nullptr);

    void beforeLoad() override {}
    void beforeUnload() override {}

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString forceFile() const;
    int forceEnabled() const;

  private:
    QLineEdit* m_forceFileEdit = nullptr;
    QComboBox* m_forceEnabledCombo = nullptr;
};
