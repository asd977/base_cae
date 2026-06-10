#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

namespace Ui
{
class GeoStressOuterForcePage;
}

class GeoStressOuterForcePage : public BasePage
{
    Q_OBJECT
  public:
    explicit GeoStressOuterForcePage(QWidget* parent = nullptr);
    ~GeoStressOuterForcePage() override;

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString forceFile() const;
    int forceEnabled() const;
    bool exportDatFiles() const;

  private slots:
    void onClearClicked();
    void onBatchAddClicked();
    void onGroupHovered(int row);

  private:
    PickMode currentPickMode() const;
    void refreshGroupCombo();

  private:
    Ui::GeoStressOuterForcePage* ui = nullptr;
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_batchAddBtn = nullptr;
    QLineEdit* m_fxEdit = nullptr;
    QLineEdit* m_fyEdit = nullptr;
    QLineEdit* m_fzEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QCheckBox* m_pickEnableCheckBox = nullptr;
};
