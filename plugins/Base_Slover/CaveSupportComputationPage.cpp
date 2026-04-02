#include "CaveSupportComputationPage.h"

#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation"), m_ncaseEdit(new QLineEdit(this)),
      m_caseBlocksEdit(new QPlainTextEdit(this))
{
    Q_UNUSED(parent);

    auto* layout = new QVBoxLayout(this);

    auto* tips = new QLabel(
        "此节点用于配置洞室“开挖和加固计算”工况段。NCASE表示工况数，下面为各工况循环内容。", this);
    tips->setWordWrap(true);
    layout->addWidget(tips);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel("NCASE（计算工况数）", this));
    m_ncaseEdit->setText("1");
    row->addWidget(m_ncaseEdit);
    auto* regenBtn = new QPushButton("按 NCASE 生成模板", this);
    row->addWidget(regenBtn);
    row->addStretch();
    layout->addLayout(row);

    layout->addWidget(new QLabel("工况循环段（I=1..NCASE）", this));
    m_caseBlocksEdit->setPlainText(buildTemplate(1));
    layout->addWidget(m_caseBlocksEdit, 1);

    connect(regenBtn, &QPushButton::clicked, this,
            &CaveSupportComputationPage::onRegenerateTemplate);
}

void CaveSupportComputationPage::beforeLoad() {}
void CaveSupportComputationPage::beforeUnload() {}

QString CaveSupportComputationPage::sectionText() const
{
    QString text;
    text += QString("%1    NCASE\n").arg(m_ncaseEdit->text().trimmed());
    text += "\n";
    text += m_caseBlocksEdit->toPlainText().trimmed();
    text += "\n";
    return text;
}

QJsonObject CaveSupportComputationPage::saveJson()
{
    QJsonObject obj;
    obj["ncase"] = m_ncaseEdit->text().trimmed();
    obj["case_blocks"] = m_caseBlocksEdit->toPlainText();
    return obj;
}

void CaveSupportComputationPage::loadJson(const QJsonObject& obj)
{
    m_ncaseEdit->setText(obj.value("ncase").toString(m_ncaseEdit->text()));
    m_caseBlocksEdit->setPlainText(
        obj.value("case_blocks").toString(m_caseBlocksEdit->toPlainText()));
}

void CaveSupportComputationPage::onRegenerateTemplate()
{
    const int ncase = m_ncaseEdit->text().trimmed().toInt();
    m_caseBlocksEdit->setPlainText(buildTemplate(ncase > 0 ? ncase : 1));
}

QString CaveSupportComputationPage::buildTemplate(int ncase) const
{
    QString text;
    for (int i = 1; i <= ncase; ++i)
    {
        if (i > 1)
            text += "\n------------------------------------------------------------\n";
        text += QString("%1    ICASE\n").arg(i);
        text += "0    NCHANGE\n";
        text += "-1   -3    IEXCA_STA    IEXCA_END\n";
        text += "1    1     IF_EALL      IF_JALL\n";
        text += "1    1    1    IF_E1TOA   IF_J1TOA   JA_METHOD\n";
        text += "0    IF_CHEN\n";
    }
    return text;
}
