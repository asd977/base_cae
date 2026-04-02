#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QComboBox;
class QLineEdit;
class QPushButton;

class GeoStressOuterForcePage : public BasePage
{
    Q_OBJECT
  public:
    explicit GeoStressOuterForcePage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString forceFile() const;
    int forceEnabled() const;
    bool exportDatFiles() const;

  private slots:
    void onClearClicked();

  private:
    PickMode currentPickMode() const;

  private:
    QLineEdit* m_roofFileEdit = nullptr;
    QLineEdit* m_forceFileEdit = nullptr;
    QComboBox* m_pickModeCombo = nullptr;
    QComboBox* m_forceEnabledCombo = nullptr;
    QLineEdit* m_fxEdit = nullptr;
    QLineEdit* m_fyEdit = nullptr;
    QLineEdit* m_fzEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
