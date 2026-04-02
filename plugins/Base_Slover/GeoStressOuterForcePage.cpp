#include "GeoStressOuterForcePage.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLineEdit>

GeoStressOuterForcePage::GeoStressOuterForcePage(QWidget* parent)
    : BasePage("GeoStressRegressionOuterForce")
{
    Q_UNUSED(parent);
    auto* form = new QFormLayout(this);
    m_forceFileEdit = new QLineEdit(this);
    m_forceEnabledCombo = new QComboBox(this);
    m_forceEnabledCombo->addItem("0 - 不考虑", 0);
    m_forceEnabledCombo->addItem("1 - 考虑", 1);

    form->addRow("外面力文件(固定)", m_forceFileEdit);
    form->addRow("是否考虑外面力", m_forceEnabledCombo);
}

QJsonObject GeoStressOuterForcePage::saveJson()
{
    QJsonObject obj;
    obj["force_file"] = m_forceFileEdit->text().trimmed();
    obj["force_enabled"] = m_forceEnabledCombo->currentData().toInt();
    return obj;
}

void GeoStressOuterForcePage::loadJson(const QJsonObject& obj)
{
    m_forceFileEdit->setText(obj.value("force_file").toString(m_forceFileEdit->text()));
    const int enabled = obj.value("force_enabled").toInt(m_forceEnabledCombo->currentData().toInt());
    const int idx = m_forceEnabledCombo->findData(enabled);
    if (idx >= 0)
        m_forceEnabledCombo->setCurrentIndex(idx);
}

QString GeoStressOuterForcePage::forceFile() const
{
    return m_forceFileEdit->text().trimmed();
}

int GeoStressOuterForcePage::forceEnabled() const
{
    return m_forceEnabledCombo->currentData().toInt();
}
