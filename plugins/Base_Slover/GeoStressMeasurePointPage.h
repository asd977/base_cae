#pragma once

#include <BasePage.h>
#include <PickManager.h>

class QLineEdit;
class QPushButton;

class GeoStressMeasurePointPage : public BasePage
{
    Q_OBJECT
  public:
    struct MeasurePoint
    {
        QString x;
        QString y;
        QString sgx;
        QString sgy;
        QString txy;
    };

    explicit GeoStressMeasurePointPage(QWidget* parent = nullptr);

    void beforeLoad() override;
    void beforeUnload() override;

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    MeasurePoint point() const;

  private slots:
    void onClearClicked();

  private:
    QLineEdit* m_pointIdEdit = nullptr;
    QLineEdit* m_sgxEdit = nullptr;
    QLineEdit* m_sgyEdit = nullptr;
    QLineEdit* m_txyEdit = nullptr;
    QPushButton* m_clearBtn = nullptr;
};
