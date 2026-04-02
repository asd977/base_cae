#include "GeoStressSideFacePage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>

GeoStressSideFacePage::GeoStressSideFacePage(QWidget* parent)
    : BasePage("GeoStressRegressionSideFace")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);

    m_faceFileEdit = new QLineEdit(this);
    m_idirCombo = new QComboBox(this);
    m_idirCombo->addItem("1 - x方向", "1");
    m_idirCombo->addItem("2 - y方向", "2");
    m_idirCombo->addItem("3 - z方向", "3");

    m_heightEdit = new QLineEdit("2000.0000", this);
    m_gamaEdit = new QLineEdit("2650.0000", this);
    m_pgroutEdit = new QLineEdit("0", this);
    m_iiEdit = new QLineEdit("1", this);

    form->addRow("侧立面文件", m_faceFileEdit);
    form->addRow("剖切方向(IDIR)", m_idirCombo);
    form->addRow("侧压高程(m)", m_heightEdit);
    form->addRow("测压容重(kg/m3)", m_gamaEdit);
    form->addRow("附加矩形常压(Pa)", m_pgroutEdit);
    form->addRow("序号", m_iiEdit);
}

QJsonObject GeoStressSideFacePage::saveJson()
{
    QJsonObject obj;
    obj["face_file"] = m_faceFileEdit->text().trimmed();
    obj["idir"] = m_idirCombo->currentData().toString();
    obj["height"] = m_heightEdit->text().trimmed();
    obj["gama"] = m_gamaEdit->text().trimmed();
    obj["pgrout"] = m_pgroutEdit->text().trimmed();
    obj["ii"] = m_iiEdit->text().trimmed();
    return obj;
}

void GeoStressSideFacePage::loadJson(const QJsonObject& obj)
{
    m_faceFileEdit->setText(obj.value("face_file").toString(m_faceFileEdit->text()));
    const QString idir = obj.value("idir").toString(m_idirCombo->currentData().toString());
    const int idx = m_idirCombo->findData(idir);
    if (idx >= 0)
        m_idirCombo->setCurrentIndex(idx);
    m_heightEdit->setText(obj.value("height").toString(m_heightEdit->text()));
    m_gamaEdit->setText(obj.value("gama").toString(m_gamaEdit->text()));
    m_pgroutEdit->setText(obj.value("pgrout").toString(m_pgroutEdit->text()));
    m_iiEdit->setText(obj.value("ii").toString(m_iiEdit->text()));
}

QString GeoStressSideFacePage::faceFile() const
{
    return m_faceFileEdit->text().trimmed();
}

GeoStressSideFacePage::FaceAttr GeoStressSideFacePage::attr() const
{
    FaceAttr a;
    a.idir = m_idirCombo->currentData().toString();
    a.height = m_heightEdit->text().trimmed();
    a.gama = m_gamaEdit->text().trimmed();
    a.pgrout = m_pgroutEdit->text().trimmed();
    a.ii = m_iiEdit->text().trimmed();
    return a;
}
