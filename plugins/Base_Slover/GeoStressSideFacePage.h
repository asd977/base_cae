#pragma once

#include <BasePage.h>
#include <PickManager.h>

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

  private slots:
    void onClearClicked();

  private:
    PickMode currentPickMode() const;

  private:
    QLineEdit* m_faceFileEdit = nullptr;
    QComboBox* m_pickModeCombo = nullptr;
    QLineEdit* m_idirEdit = nullptr;
    QLineEdit* m_heightEdit = nullptr;
    QLineEdit* m_gamaEdit = nullptr;
    QLineEdit* m_pgroutEdit = nullptr;
    QLineEdit* m_iiEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
