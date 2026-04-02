#include "GeoStressMeasurePointPage.h"

#include <QFormLayout>
#include <QLineEdit>

GeoStressMeasurePointPage::GeoStressMeasurePointPage(QWidget* parent)
    : BasePage("GeoStressRegressionMeasurePoint")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);

    m_xEdit = new QLineEdit("0", this);
    m_yEdit = new QLineEdit("0", this);
    m_sgxEdit = new QLineEdit("0", this);
    m_sgyEdit = new QLineEdit("0", this);
    m_txyEdit = new QLineEdit("0", this);
    m_iiEdit = new QLineEdit("1", this);

    form->addRow("X", m_xEdit);
    form->addRow("Y", m_yEdit);
    form->addRow("SGX", m_sgxEdit);
    form->addRow("SGY", m_sgyEdit);
    form->addRow("TXY", m_txyEdit);
    form->addRow("序号", m_iiEdit);
}

QJsonObject GeoStressMeasurePointPage::saveJson()
{
    QJsonObject obj;
    obj["x"] = m_xEdit->text().trimmed();
    obj["y"] = m_yEdit->text().trimmed();
    obj["sgx"] = m_sgxEdit->text().trimmed();
    obj["sgy"] = m_sgyEdit->text().trimmed();
    obj["txy"] = m_txyEdit->text().trimmed();
    obj["ii"] = m_iiEdit->text().trimmed();
    return obj;
}

void GeoStressMeasurePointPage::loadJson(const QJsonObject& obj)
{
    m_xEdit->setText(obj.value("x").toString(m_xEdit->text()));
    m_yEdit->setText(obj.value("y").toString(m_yEdit->text()));
    m_sgxEdit->setText(obj.value("sgx").toString(m_sgxEdit->text()));
    m_sgyEdit->setText(obj.value("sgy").toString(m_sgyEdit->text()));
    m_txyEdit->setText(obj.value("txy").toString(m_txyEdit->text()));
    m_iiEdit->setText(obj.value("ii").toString(m_iiEdit->text()));
}

GeoStressMeasurePointPage::MeasurePoint GeoStressMeasurePointPage::point() const
{
    MeasurePoint p;
    p.x = m_xEdit->text().trimmed();
    p.y = m_yEdit->text().trimmed();
    p.sgx = m_sgxEdit->text().trimmed();
    p.sgy = m_sgyEdit->text().trimmed();
    p.txy = m_txyEdit->text().trimmed();
    p.ii = m_iiEdit->text().trimmed();
    return p;
}
