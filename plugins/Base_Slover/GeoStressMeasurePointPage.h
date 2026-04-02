#pragma once

#include <BasePage.h>

class QLineEdit;

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
        QString ii;
    };

    explicit GeoStressMeasurePointPage(QWidget* parent = nullptr);

    void beforeLoad() override {}
    void beforeUnload() override {}

    QJsonObject saveJson() override;
    void loadJson(const QJsonObject& obj) override;

    MeasurePoint point() const;

  private:
    QLineEdit* m_xEdit = nullptr;
    QLineEdit* m_yEdit = nullptr;
    QLineEdit* m_sgxEdit = nullptr;
    QLineEdit* m_sgyEdit = nullptr;
    QLineEdit* m_txyEdit = nullptr;
    QLineEdit* m_iiEdit = nullptr;
};
