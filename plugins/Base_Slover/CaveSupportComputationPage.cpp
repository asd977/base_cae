#include "CaveSupportComputationPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QHash>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>

#include "ui_CaveSupportComputationPage.h"

CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation"), ui(new Ui::CaveSupportComputationPage)
{
    ui->setupUi(this);
    Q_UNUSED(parent);

    m_iexcaMaxSpin = ui->spinIexcaMax;
    m_excavationDaysTable = ui->tableExcavationDays;
    m_iexcaStaEdit = ui->editIexcaSta;
    m_iexcaEndEdit = ui->editIexcaEnd;
    m_ifEallCombo = ui->comboIfEall;
    m_ifJallCombo = ui->comboIfJall;
    m_ifE1toaCombo = ui->comboIfE1toa;
    m_ifJ1toaCombo = ui->comboIfJ1toa;
    m_jaMethodCombo = ui->comboJaMethod;
    m_ifChenCombo = ui->comboIfChen;
    m_idayCaseEdit = ui->editIdayCase;
    m_ifModeCombo = ui->comboIfMode;
    m_ifWaveCombo = ui->comboIfWave;
    m_dtSpin = ui->spinDt;
    m_tstaSpin = ui->spinTsta;
    m_tendSpin = ui->spinTend;
    m_accXEdit = ui->editAccX;
    m_accYEdit = ui->editAccY;
    m_accZEdit = ui->editAccZ;
    m_computeButton = ui->btnCompute;

    m_excavationDaysTable->setColumnCount(2);
    m_excavationDaysTable->setHorizontalHeaderLabels({"开挖级", "用时（天）"});
    m_excavationDaysTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto setupBoolCombo = [](QComboBox* combo, const QString& enableText, const QString& disableText)
    {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(QString("1 - %1").arg(enableText), "1");
        combo->addItem(QString("0 - %1").arg(disableText), "0");
    };

    setupBoolCombo(m_ifEallCombo, "计算不加固条件下一次开挖成洞",
                   "不进行不加固条件下一次开挖成洞计算");
    setupBoolCombo(m_ifJallCombo, "计算加固条件下一次开挖成洞",
                   "不进行加固条件下一次开挖成洞计算");
    setupBoolCombo(m_ifE1toaCombo, "计算不加固条件下逐级开挖成洞",
                   "不进行不加固条件下逐级开挖成洞计算");
    setupBoolCombo(m_ifJ1toaCombo, "计算加固条件下逐级开挖成洞",
                   "不进行加固条件下逐级开挖成洞计算");
    setupBoolCombo(m_ifChenCombo, "计算衬砌内力", "不需计算衬砌内力");

    m_jaMethodCombo->clear();
    m_jaMethodCombo->addItem("1 - 锚杆/锚索加固，不考虑衬砌", "1");
    m_jaMethodCombo->addItem("2 - 同时考虑锚杆/锚索和衬砌加固", "2");
    m_idayCaseEdit->setText("0");

    m_ifModeCombo->clear();
    m_ifModeCombo->addItem("1 - 进行模态分析", "1");
    m_ifModeCombo->addItem("0 - 不进行模态分析", "0");
    m_ifWaveCombo->clear();
    m_ifWaveCombo->addItem("1 - 进行时域分析", "1");
    m_ifWaveCombo->addItem("0 - 不进行时域分析", "0");

    connect(m_iexcaMaxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &CaveSupportComputationPage::onExcavationLevelCountChanged);
    connect(m_computeButton, &QPushButton::clicked, this,
            &CaveSupportComputationPage::onComputeClicked);

    m_caseData = CaseData();
    onExcavationLevelCountChanged(m_iexcaMaxSpin->value());
    loadCaseToUi();
}

CaveSupportComputationPage::~CaveSupportComputationPage()
{
    delete ui;
}

void CaveSupportComputationPage::beforeLoad() {}
void CaveSupportComputationPage::beforeUnload() {}

QString CaveSupportComputationPage::sectionText()
{
    saveCurrentCase();
    QString text;
    const int iexcaMax = m_iexcaMaxSpin ? m_iexcaMaxSpin->value() : 1;
    text += QString("%1     IEXCA_MAX\n").arg(iexcaMax, 4);
    for (int i = 0; i < iexcaMax; ++i)
    {
        const QString dayValue =
            m_excavationDaysTable && m_excavationDaysTable->item(i, 1)
                ? m_excavationDaysTable->item(i, 1)->text().trimmed()
                : "0";
        text += QString("%1    %2    EXCA DATE  II \n").arg(dayValue.isEmpty() ? "0" : dayValue, 8).arg(i + 1, 8);
    }
    text += "0     NCHANGE\n";
    text += "1    MCASE\n";
    text += "1        1   JISUAN CASE  II\n";
    text += "1    NCASE\n";
    text += "--------------------------------------------------------------------\n";

    const CaseData& c = m_caseData;
    text += QString("%1    ICASE\n").arg(c.icase);
    text += "0    NCHANGE\n";
    text += QString("%1    %2    IEXCA_STA    IEXCA_END\n").arg(c.iexcaSta, c.iexcaEnd);
    text += QString("%1    %2    IF_EALL    IF_JALL\n").arg(c.ifEall, c.ifJall);
    text += QString("%1    %2    %3    IF_E1TOA    IF_J1TOA    JA_METHOD\n")
                .arg(c.ifE1toa, c.ifJ1toa, c.jaMethod);
    text += QString("%1    %2    IF_CHEN    IDAY_CASE\n").arg(c.ifChen, c.idayCase);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QString caseDir = QDir(QDir(projectDir).absolutePath()).filePath("CASE01");
    caseDir = QDir::toNativeSeparators(caseDir) + QDir::separator();
    text += QString("'%1'     DIRDYNA\n").arg(caseDir);
    text += QString("%1     %2   IFMODE   IFWAVE\n")
                .arg(m_ifModeCombo ? m_ifModeCombo->currentData().toString() : "1",
                     m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : "1");
    text += QString("%1    %2   %3    DT TSTA TEND\n")
                .arg(m_dtSpin ? m_dtSpin->value() : 0.005, 0, 'f', 6)
                .arg(m_tstaSpin ? m_tstaSpin->value() : 0.0, 0, 'f', 6)
                .arg(m_tendSpin ? m_tendSpin->value() : 30.0, 0, 'f', 6);
    text += QString("'%1'  '%2'  '%3'   AX  AY  AZ\n")
                .arg(m_accXEdit ? m_accXEdit->text().trimmed() : "ACCEX.DAT",
                     m_accYEdit ? m_accYEdit->text().trimmed() : "ACCEY.DAT",
                     m_accZEdit ? m_accZEdit->text().trimmed() : "ACCEZ.DAT");
    return text;
}

bool CaveSupportComputationPage::exportICaveDatFile(const QString& supportCaseSection) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("ICAVE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QHash<int, QString> fixedValues = {
        {1, QDir::toNativeSeparators(wd.filePath("PREP")) + QDir::separator()},
        {2, QStringLiteral("MAFACTOR.DAT")},
        {4, QStringLiteral("SHUIMX.DAT")},
        {6, QStringLiteral("IIJX_CHEN.DAT")},
        {8, QStringLiteral("IIJX_T01-01.DAT")},
        {9, QStringLiteral("MASET_DCL.DAT")},
        {12, QStringLiteral("1")},
        {13, QStringLiteral("IISEEP.DAT")},
        {14, QDir::toNativeSeparators(wd.filePath("GEOSTR")) + QDir::separator()},
        {15, QStringLiteral("JDTRACE.DAT")},
        {16, QStringLiteral("KETRACE.DAT")},
        {17, QStringLiteral("TDTRACE.DAT")},
    };

    const QSet<int> quotedFields = {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14,
                                    15, 16, 17, 18, 19, 20, 21, 22, 23,
                                    24, 25};
    for (int fieldNo = 1; fieldNo <= 25; ++fieldNo)
    {
        QString value;
        if (fixedValues.contains(fieldNo))
            value = fixedValues.value(fieldNo);
        else if (auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(fieldNo)))
            value = edit->text().trimmed();

        if (quotedFields.contains(fieldNo))
            os << "'" << value << "'      " << fieldNo << "\n";
        else
            os << " " << value << "      " << fieldNo << "\n";
    }

    if (!supportCaseSection.trimmed().isEmpty())
        os << supportCaseSection.trimmed() << "\n";
    return true;
}

QJsonObject CaveSupportComputationPage::saveJson()
{
    saveCurrentCase();
    QJsonObject obj;
    obj["ncase"] = 1;
    obj["current_case"] = 0;
    obj["iexca_max"] = m_iexcaMaxSpin ? m_iexcaMaxSpin->value() : 1;
    QJsonArray excavationDays;
    const int iexcaMax = m_iexcaMaxSpin ? m_iexcaMaxSpin->value() : 1;
    for (int i = 0; i < iexcaMax; ++i)
    {
        const QString dayValue =
            m_excavationDaysTable && m_excavationDaysTable->item(i, 1)
                ? m_excavationDaysTable->item(i, 1)->text().trimmed()
                : "0";
        excavationDays.append(dayValue.isEmpty() ? "0" : dayValue);
    }
    obj["excavation_days"] = excavationDays;
    obj["ifmode"] = m_ifModeCombo ? m_ifModeCombo->currentData().toString() : "1";
    obj["ifwave"] = m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : "1";
    obj["dt"] = m_dtSpin ? m_dtSpin->value() : 0.005;
    obj["tsta"] = m_tstaSpin ? m_tstaSpin->value() : 0.0;
    obj["tend"] = m_tendSpin ? m_tendSpin->value() : 30.0;
    obj["acc_x"] = m_accXEdit ? m_accXEdit->text().trimmed() : "ACCEX.DAT";
    obj["acc_y"] = m_accYEdit ? m_accYEdit->text().trimmed() : "ACCEY.DAT";
    obj["acc_z"] = m_accZEdit ? m_accZEdit->text().trimmed() : "ACCEZ.DAT";

    QJsonArray cases;
    QJsonObject one;
    one["icase"] = m_caseData.icase;
    one["iexca_sta"] = m_caseData.iexcaSta;
    one["iexca_end"] = m_caseData.iexcaEnd;
    one["if_eall"] = m_caseData.ifEall;
    one["if_jall"] = m_caseData.ifJall;
    one["if_e1toa"] = m_caseData.ifE1toa;
    one["if_j1toa"] = m_caseData.ifJ1toa;
    one["ja_method"] = m_caseData.jaMethod;
    one["if_chen"] = m_caseData.ifChen;
    one["iday_case"] = m_caseData.idayCase;
    cases.append(one);
    obj["cases"] = cases;
    const QStringList fixedIcaveFields = {"1", "2", "4", "6", "8", "9",
                                          "12", "13", "14", "15", "16", "17"};
    for (int i = 1; i <= 25; ++i)
    {
        if (fixedIcaveFields.contains(QString::number(i)))
            continue;
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        obj[QString("line_%1").arg(i)] = edit ? edit->text().trimmed() : "";
    }
    return obj;
}

void CaveSupportComputationPage::loadJson(const QJsonObject& obj)
{
    m_isUpdatingUi = true;

    m_caseData = CaseData();
    const int iexcaMax = qMax(1, obj.value("iexca_max").toInt(1));
    if (m_iexcaMaxSpin)
        m_iexcaMaxSpin->setValue(iexcaMax);
    onExcavationLevelCountChanged(iexcaMax);
    const QJsonArray excavationDays = obj.value("excavation_days").toArray();
    for (int i = 0; i < excavationDays.size() && i < iexcaMax; ++i)
    {
        if (!m_excavationDaysTable->item(i, 1))
            m_excavationDaysTable->setItem(i, 1, new QTableWidgetItem("0"));
        m_excavationDaysTable->item(i, 1)->setText(excavationDays.at(i).toString("0"));
    }
    if (m_ifModeCombo)
    {
        const int idx = m_ifModeCombo->findData(obj.value("ifmode").toString("1"));
        m_ifModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_ifWaveCombo)
    {
        const int idx = m_ifWaveCombo->findData(obj.value("ifwave").toString("1"));
        m_ifWaveCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_dtSpin)
        m_dtSpin->setValue(obj.value("dt").toDouble(0.005));
    if (m_tstaSpin)
        m_tstaSpin->setValue(obj.value("tsta").toDouble(0.0));
    if (m_tendSpin)
        m_tendSpin->setValue(obj.value("tend").toDouble(30.0));
    if (m_accXEdit)
        m_accXEdit->setText(obj.value("acc_x").toString("ACCEX.DAT"));
    if (m_accYEdit)
        m_accYEdit->setText(obj.value("acc_y").toString("ACCEY.DAT"));
    if (m_accZEdit)
        m_accZEdit->setText(obj.value("acc_z").toString("ACCEZ.DAT"));

    const QStringList fixedIcaveFields = {"1", "2", "4", "6", "8", "9",
                                          "12", "13", "14", "15", "16", "17"};
    for (int i = 1; i <= 25; ++i)
    {
        if (fixedIcaveFields.contains(QString::number(i)))
            continue;
        auto* edit = findChild<QLineEdit*>(QString("line%1Edit").arg(i));
        if (edit)
            edit->setText(obj.value(QString("line_%1").arg(i)).toString(edit->text()));
    }

    const QJsonArray cases = obj.value("cases").toArray();
    if (!cases.isEmpty())
    {
        const QJsonObject one = cases.at(0).toObject();
        m_caseData.icase = 1;
        m_caseData.iexcaSta = one.value("iexca_sta").toString(m_caseData.iexcaSta);
        m_caseData.iexcaEnd = one.value("iexca_end").toString(m_caseData.iexcaEnd);
        m_caseData.ifEall = one.value("if_eall").toString(m_caseData.ifEall);
        m_caseData.ifJall = one.value("if_jall").toString(m_caseData.ifJall);
        m_caseData.ifE1toa = one.value("if_e1toa").toString(m_caseData.ifE1toa);
        m_caseData.ifJ1toa = one.value("if_j1toa").toString(m_caseData.ifJ1toa);
        m_caseData.jaMethod = one.value("ja_method").toString(m_caseData.jaMethod);
        m_caseData.ifChen = one.value("if_chen").toString(m_caseData.ifChen);
        m_caseData.idayCase = one.value("iday_case").toString(m_caseData.idayCase);
    }

    m_isUpdatingUi = false;
    loadCaseToUi();
}

void CaveSupportComputationPage::onExcavationLevelCountChanged(int count)
{
    if (!m_excavationDaysTable)
        return;

    const int target = qMax(1, count);
    const int oldRows = m_excavationDaysTable->rowCount();
    m_excavationDaysTable->setRowCount(target);
    for (int r = 0; r < target; ++r)
    {
        if (!m_excavationDaysTable->item(r, 0))
            m_excavationDaysTable->setItem(r, 0, new QTableWidgetItem(QString::number(r + 1)));
        else
            m_excavationDaysTable->item(r, 0)->setText(QString::number(r + 1));
        m_excavationDaysTable->item(r, 0)->setFlags(m_excavationDaysTable->item(r, 0)->flags() & ~Qt::ItemIsEditable);

        if (!m_excavationDaysTable->item(r, 1))
            m_excavationDaysTable->setItem(r, 1, new QTableWidgetItem(r < oldRows ? "0" : "0"));
    }
}

void CaveSupportComputationPage::onComputeClicked()
{
    emit computeRequested();
}

void CaveSupportComputationPage::saveCurrentCase()
{
    CaseData& c = m_caseData;
    c.icase = 1;
    c.iexcaSta = m_iexcaStaEdit->text().trimmed();
    c.iexcaEnd = m_iexcaEndEdit->text().trimmed();
    c.ifEall = m_ifEallCombo->currentData().toString().trimmed();
    c.ifJall = m_ifJallCombo->currentData().toString().trimmed();
    c.ifE1toa = m_ifE1toaCombo->currentData().toString().trimmed();
    c.ifJ1toa = m_ifJ1toaCombo->currentData().toString().trimmed();
    c.jaMethod = m_jaMethodCombo->currentData().toString().trimmed();
    c.ifChen = m_ifChenCombo->currentData().toString().trimmed();
    c.idayCase = m_idayCaseEdit->text().trimmed();
    if (c.idayCase.isEmpty())
        c.idayCase = "0";

}

void CaveSupportComputationPage::loadCaseToUi()
{
    m_isUpdatingUi = true;

    const CaseData& c = m_caseData;
    m_iexcaStaEdit->setText(c.iexcaSta);
    m_iexcaEndEdit->setText(c.iexcaEnd);

    const int eallIdx = m_ifEallCombo->findData(c.ifEall);
    m_ifEallCombo->setCurrentIndex(eallIdx >= 0 ? eallIdx : 0);
    const int jallIdx = m_ifJallCombo->findData(c.ifJall);
    m_ifJallCombo->setCurrentIndex(jallIdx >= 0 ? jallIdx : 0);
    const int e1toaIdx = m_ifE1toaCombo->findData(c.ifE1toa);
    m_ifE1toaCombo->setCurrentIndex(e1toaIdx >= 0 ? e1toaIdx : 0);
    const int j1toaIdx = m_ifJ1toaCombo->findData(c.ifJ1toa);
    m_ifJ1toaCombo->setCurrentIndex(j1toaIdx >= 0 ? j1toaIdx : 0);
    const int chenIdx = m_ifChenCombo->findData(c.ifChen);
    m_ifChenCombo->setCurrentIndex(chenIdx >= 0 ? chenIdx : 0);
    const int jaIdx = m_jaMethodCombo->findData(c.jaMethod);
    m_jaMethodCombo->setCurrentIndex(jaIdx >= 0 ? jaIdx : 0);
    m_idayCaseEdit->setText(c.idayCase);

    m_isUpdatingUi = false;
}
