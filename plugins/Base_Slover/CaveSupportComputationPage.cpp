#include "CaveSupportComputationPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel("开挖和加固计算设置（固定 1 个工况）", this);
    title->setWordWrap(true);
    root->addWidget(title);

    auto* excavationTimeGroup = new QGroupBox("4.5.2 洞室开挖时间定义", this);
    auto* excavationTimeLayout = new QVBoxLayout(excavationTimeGroup);
    auto* rowIexcaMax = new QHBoxLayout();
    rowIexcaMax->addWidget(new QLabel("最大开挖级（IEXCA_MAX）", this));
    m_iexcaMaxSpin = new QSpinBox(this);
    m_iexcaMaxSpin->setRange(1, 999);
    m_iexcaMaxSpin->setValue(1);
    rowIexcaMax->addWidget(m_iexcaMaxSpin);
    rowIexcaMax->addStretch();
    excavationTimeLayout->addLayout(rowIexcaMax);

    m_excavationDaysTable = new QTableWidget(this);
    m_excavationDaysTable->setColumnCount(2);
    m_excavationDaysTable->setHorizontalHeaderLabels({"开挖级", "用时（天）"});
    m_excavationDaysTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    excavationTimeLayout->addWidget(m_excavationDaysTable);
    root->addWidget(excavationTimeGroup);

    auto* paramGroup = new QGroupBox("开挖与加固参数（工况1）", this);
    auto* form = new QFormLayout(paramGroup);

    auto* rowExc = new QWidget(this);
    auto* rowExcLayout = new QHBoxLayout(rowExc);
    rowExcLayout->setContentsMargins(0, 0, 0, 0);
    m_iexcaStaEdit = new QLineEdit(this);
    m_iexcaEndEdit = new QLineEdit(this);
    rowExcLayout->addWidget(new QLabel("起始开挖级（IEXCA_STA，负值）", this));
    rowExcLayout->addWidget(m_iexcaStaEdit);
    rowExcLayout->addWidget(new QLabel("终了开挖级（IEXCA_END，负值）", this));
    rowExcLayout->addWidget(m_iexcaEndEdit);
    form->addRow("开挖级范围", rowExc);

    auto mkBoolCombo = [this](const QString& enableText, const QString& disableText)
    {
        auto* combo = new QComboBox(this);
        combo->addItem(QString("1 - %1").arg(enableText), "1");
        combo->addItem(QString("0 - %1").arg(disableText), "0");
        return combo;
    };

    m_ifEallCombo = mkBoolCombo("计算不加固条件下一次开挖成洞",
                                "不进行不加固条件下一次开挖成洞计算");
    m_ifJallCombo = mkBoolCombo("计算加固条件下一次开挖成洞",
                                "不进行加固条件下一次开挖成洞计算");
    m_ifE1toaCombo = mkBoolCombo("计算不加固条件下逐级开挖成洞",
                                 "不进行不加固条件下逐级开挖成洞计算");
    m_ifJ1toaCombo = mkBoolCombo("计算加固条件下逐级开挖成洞",
                                 "不进行加固条件下逐级开挖成洞计算");
    m_ifChenCombo = mkBoolCombo("计算衬砌内力", "不需计算衬砌内力");

    form->addRow("一次成洞（不加固）", m_ifEallCombo);
    form->addRow("一次成洞（加固）", m_ifJallCombo);
    form->addRow("逐级开挖（不加固）", m_ifE1toaCombo);
    form->addRow("逐级开挖（加固）", m_ifJ1toaCombo);

    m_jaMethodCombo = new QComboBox(this);
    m_jaMethodCombo->addItem("1 - 锚杆/锚索加固，不考虑衬砌", "1");
    m_jaMethodCombo->addItem("2 - 同时考虑锚杆/锚索和衬砌加固", "2");
    form->addRow("加固方式（JA_METHOD）", m_jaMethodCombo);
    form->addRow("衬砌内力计算（IF_CHEN）", m_ifChenCombo);
    m_idayCaseEdit = new QLineEdit(this);
    m_idayCaseEdit->setText("0");
    form->addRow("长期稳定日期（IDAY_CASE）", m_idayCaseEdit);

    root->addWidget(paramGroup);

    auto* dynaGroup = new QGroupBox("4.5.6 洞室地震动力计算相关定义", this);
    auto* dynaForm = new QFormLayout(dynaGroup);
    m_ifModeCombo = new QComboBox(this);
    m_ifModeCombo->addItem("1 - 进行模态分析", "1");
    m_ifModeCombo->addItem("0 - 不进行模态分析", "0");
    m_ifWaveCombo = new QComboBox(this);
    m_ifWaveCombo->addItem("1 - 进行时域分析", "1");
    m_ifWaveCombo->addItem("0 - 不进行时域分析", "0");
    dynaForm->addRow("模态分析开关（IFMODE）", m_ifModeCombo);
    dynaForm->addRow("时程分析开关（IFWAVE）", m_ifWaveCombo);

    m_dtSpin = new QDoubleSpinBox(this);
    m_dtSpin->setDecimals(6);
    m_dtSpin->setRange(0.0, 999999.0);
    m_dtSpin->setValue(0.005);
    m_tstaSpin = new QDoubleSpinBox(this);
    m_tstaSpin->setDecimals(6);
    m_tstaSpin->setRange(0.0, 999999.0);
    m_tstaSpin->setValue(0.0);
    m_tendSpin = new QDoubleSpinBox(this);
    m_tendSpin->setDecimals(6);
    m_tendSpin->setRange(0.0, 999999.0);
    m_tendSpin->setValue(30.0);

    auto* dtRow = new QWidget(this);
    auto* dtLayout = new QHBoxLayout(dtRow);
    dtLayout->setContentsMargins(0, 0, 0, 0);
    dtLayout->addWidget(new QLabel("DT", this));
    dtLayout->addWidget(m_dtSpin);
    dtLayout->addWidget(new QLabel("TSTA", this));
    dtLayout->addWidget(m_tstaSpin);
    dtLayout->addWidget(new QLabel("TEND", this));
    dtLayout->addWidget(m_tendSpin);
    dynaForm->addRow("时间参数", dtRow);

    m_accXEdit = new QLineEdit("ACCEX.DAT", this);
    m_accYEdit = new QLineEdit("ACCEY.DAT", this);
    m_accZEdit = new QLineEdit("ACCEZ.DAT", this);
    auto* accRow = new QWidget(this);
    auto* accLayout = new QHBoxLayout(accRow);
    accLayout->setContentsMargins(0, 0, 0, 0);
    accLayout->addWidget(new QLabel("AX", this));
    accLayout->addWidget(m_accXEdit);
    accLayout->addWidget(new QLabel("AY", this));
    accLayout->addWidget(m_accYEdit);
    accLayout->addWidget(new QLabel("AZ", this));
    accLayout->addWidget(m_accZEdit);
    dynaForm->addRow("地震波文件", accRow);
    root->addWidget(dynaGroup);

    root->addStretch();

    connect(m_iexcaMaxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &CaveSupportComputationPage::onExcavationLevelCountChanged);

    m_caseData = CaseData();
    onExcavationLevelCountChanged(m_iexcaMaxSpin->value());
    loadCaseToUi();
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

    text += "1    NCASE\n\n";

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
