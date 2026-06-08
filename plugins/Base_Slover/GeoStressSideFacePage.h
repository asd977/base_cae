#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;

class GeoStressSideFacePage : public BasePage
{
    Q_OBJECT
  public:
    struct FaceAttr
    {
        QString idir;
        QString height;
        QString gama;
        QString pgrout;
        QString ii;
    };

    explicit GeoStressSideFacePage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    QString faceFile() const;
    FaceAttr attr() const;
    bool exportDatFile() const;
    void setFaceIndex(int index);

  private slots:
    void onClearClicked();
    void onBatchAddClicked();
    void onGroupHovered(int row);

  private:
    PickMode currentPickMode() const;
    void refreshGroupCombo();

  private:
    int m_faceIndex = 1;
    QComboBox* m_groupCombo = nullptr;
    QPushButton* m_batchAddBtn = nullptr;
    QLineEdit* m_idirEdit = nullptr;
    QLineEdit* m_heightEdit = nullptr;
    QLineEdit* m_gamaEdit = nullptr;
    QLineEdit* m_pgroutEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QCheckBox* m_pickEnableCheckBox = nullptr;
};
