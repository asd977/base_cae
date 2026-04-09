#include "CaveSupportComputationPage.h"

#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>

#include <QComboBox>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation")
{
    Q_UNUSED(parent);

    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel("开挖和加固计算设置（NCASE + 每工况参数）", this);
    title->setWordWrap(true);
    root->addWidget(title);

    auto* baseGroup = new QGroupBox("工况管理", this);
    auto* baseLayout = new QHBoxLayout(baseGroup);
    baseLayout->addWidget(new QLabel("NCASE（计算工况数）", this));
    m_ncaseSpin = new QSpinBox(this);
    m_ncaseSpin->setRange(1, 999);
    m_ncaseSpin->setValue(1);
    baseLayout->addWidget(m_ncaseSpin);
    baseLayout->addSpacing(20);
    baseLayout->addWidget(new QLabel("当前工况", this));
    m_caseSelector = new QComboBox(this);
    baseLayout->addWidget(m_caseSelector);
    baseLayout->addSpacing(20);
    baseLayout->addWidget(new QLabel("ICASE", this));
    m_icaseEdit = new QLineEdit(this);
    m_icaseEdit->setReadOnly(true);
    baseLayout->addWidget(m_icaseEdit);
    baseLayout->addStretch();
    root->addWidget(baseGroup);

    auto* matGroup = new QGroupBox("NCHANGE 材料属性变换", this);
    auto* matLayout = new QVBoxLayout(matGroup);
    matLayout->addWidget(new QLabel("需变换材料属性号的材料总数由下表行数自动确定（Mold -> Mnew）", this));
    m_materialTable = new QTableWidget(this);
    m_materialTable->setColumnCount(2);
    m_materialTable->setHorizontalHeaderLabels({"Mold", "Mnew"});
    m_materialTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    matLayout->addWidget(m_materialTable);

    auto* rowMatBtn = new QHBoxLayout();
    auto* btnAdd = new QPushButton("新增材料变换", this);
    auto* btnDel = new QPushButton("删除选中行", this);
    auto* btnEditLibrary = new QPushButton("编辑材料变换库", this);
    rowMatBtn->addWidget(btnAdd);
    rowMatBtn->addWidget(btnDel);
    rowMatBtn->addWidget(btnEditLibrary);
    rowMatBtn->addStretch();
    matLayout->addLayout(rowMatBtn);
    root->addWidget(matGroup);

    auto* paramGroup = new QGroupBox("工况参数", this);
    auto* form = new QFormLayout(paramGroup);

    auto* rowExc = new QWidget(this);
    auto* rowExcLayout = new QHBoxLayout(rowExc);
    rowExcLayout->setContentsMargins(0, 0, 0, 0);
    m_iexcaStaEdit = new QLineEdit(this);
    m_iexcaEndEdit = new QLineEdit(this);
    rowExcLayout->addWidget(new QLabel("IEXCA_STA（负值，且 >= IEXCA_END）", this));
    rowExcLayout->addWidget(m_iexcaStaEdit);
    rowExcLayout->addWidget(new QLabel("IEXCA_END（负值）", this));
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

    form->addRow("IF_EALL", m_ifEallCombo);
    form->addRow("IF_JALL", m_ifJallCombo);
    form->addRow("IF_E1TOA", m_ifE1toaCombo);
    form->addRow("IF_J1TOA", m_ifJ1toaCombo);

    m_jaMethodCombo = new QComboBox(this);
    m_jaMethodCombo->addItem("1 - 锚杆/锚索加固，不考虑衬砌", "1");
    m_jaMethodCombo->addItem("2 - 同时考虑锚杆/锚索和衬砌加固", "2");
    form->addRow("JA_METHOD", m_jaMethodCombo);
    form->addRow("IF_CHEN", m_ifChenCombo);

    root->addWidget(paramGroup);
    root->addStretch();

    connect(m_caseSelector, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::onCaseChanged);
    connect(m_ncaseSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &CaveSupportComputationPage::onCaseCountChanged);
    connect(btnAdd, &QPushButton::clicked, this, &CaveSupportComputationPage::onAddMaterialRow);
    connect(btnDel, &QPushButton::clicked, this, &CaveSupportComputationPage::onDeleteMaterialRow);
    connect(btnEditLibrary, &QPushButton::clicked, this,
            []()
            {
                auto* materialService = ServiceRegistry::instance().getService<MaterialManagerService>();
                if (!materialService)
                    return;
                materialService->showMaterialTransitionDialog();
            });

    ensureCaseCount(1);
    refreshCaseSelector();
    loadCaseToUi(0);
}

void CaveSupportComputationPage::beforeLoad() {}
void CaveSupportComputationPage::beforeUnload() {}

QString CaveSupportComputationPage::sectionText()
{
    saveCurrentCase();

    QString text;
    text += QString("%1    NCASE\n\n").arg(m_cases.size());

    for (int i = 0; i < m_cases.size(); ++i)
    {
        const CaseData& c = m_cases.at(i);
        text += QString("%1    ICASE\n").arg(c.icase);
        text += QString("%1    NCHANGE\n").arg(c.materialChanges.size());
        for (const auto& row : c.materialChanges)
            text += QString("%1    %2\n").arg(row.first, row.second);
        text += QString("%1    %2    IEXCA_STA    IEXCA_END\n").arg(c.iexcaSta, c.iexcaEnd);
        text += QString("%1    %2    IF_EALL    IF_JALL\n").arg(c.ifEall, c.ifJall);
        text += QString("%1    %2    %3    IF_E1TOA    IF_J1TOA    JA_METHOD\n")
                    .arg(c.ifE1toa, c.ifJ1toa, c.jaMethod);
        text += QString("%1    IF_CHEN\n").arg(c.ifChen);
        if (i != m_cases.size() - 1)
            text += "\n";
    }
    return text;
}

QJsonObject CaveSupportComputationPage::saveJson()
{
    saveCurrentCase();

    QJsonObject obj;
    obj["ncase"] = m_cases.size();
    obj["current_case"] = m_caseSelector ? m_caseSelector->currentIndex() : 0;

    QJsonArray cases;
    for (const CaseData& c : m_cases)
    {
        QJsonObject one;
        one["icase"] = c.icase;
        one["iexca_sta"] = c.iexcaSta;
        one["iexca_end"] = c.iexcaEnd;
        one["if_eall"] = c.ifEall;
        one["if_jall"] = c.ifJall;
        one["if_e1toa"] = c.ifE1toa;
        one["if_j1toa"] = c.ifJ1toa;
        one["ja_method"] = c.jaMethod;
        one["if_chen"] = c.ifChen;

        QJsonArray mats;
        for (const auto& row : c.materialChanges)
        {
            QJsonObject m;
            m["old"] = row.first;
            m["new"] = row.second;
            mats.append(m);
        }
        one["materials"] = mats;
        cases.append(one);
    }
    obj["cases"] = cases;
    return obj;
}

void CaveSupportComputationPage::loadJson(const QJsonObject& obj)
{
    m_isUpdatingUi = true;

    const int ncase = qMax(1, obj.value("ncase").toInt(1));
    ensureCaseCount(ncase);

    const QJsonArray cases = obj.value("cases").toArray();
    for (int i = 0; i < cases.size() && i < m_cases.size(); ++i)
    {
        const QJsonObject one = cases.at(i).toObject();
        CaseData c = defaultCase(i);
        c.icase = one.value("icase").toInt(i + 1);
        c.iexcaSta = one.value("iexca_sta").toString(c.iexcaSta);
        c.iexcaEnd = one.value("iexca_end").toString(c.iexcaEnd);
        c.ifEall = one.value("if_eall").toString(c.ifEall);
        c.ifJall = one.value("if_jall").toString(c.ifJall);
        c.ifE1toa = one.value("if_e1toa").toString(c.ifE1toa);
        c.ifJ1toa = one.value("if_j1toa").toString(c.ifJ1toa);
        c.jaMethod = one.value("ja_method").toString(c.jaMethod);
        c.ifChen = one.value("if_chen").toString(c.ifChen);

        c.materialChanges.clear();
        const QJsonArray mats = one.value("materials").toArray();
        for (const QJsonValue& mv : mats)
        {
            const QJsonObject mo = mv.toObject();
            c.materialChanges.push_back({mo.value("old").toString("1"), mo.value("new").toString("1")});
        }
        if (c.materialChanges.isEmpty())
            c.materialChanges.push_back({"1", "1"});

        m_cases[i] = c;
    }

    refreshCaseSelector();
    const int current = qBound(0, obj.value("current_case").toInt(0), m_cases.size() - 1);
    m_caseSelector->setCurrentIndex(current);
    m_currentCaseIndex = current;
    m_ncaseSpin->setValue(m_cases.size());

    m_isUpdatingUi = false;
    loadCaseToUi(m_currentCaseIndex);
}

void CaveSupportComputationPage::onCaseChanged(int index)
{
    if (m_isUpdatingUi || index < 0 || index >= m_cases.size())
        return;

    saveCurrentCase();
    m_currentCaseIndex = index;
    loadCaseToUi(index);
}

void CaveSupportComputationPage::onCaseCountChanged(int count)
{
    if (m_isUpdatingUi)
        return;

    saveCurrentCase();
    ensureCaseCount(count);

    m_isUpdatingUi = true;
    refreshCaseSelector();
    m_caseSelector->setCurrentIndex(qMin(m_currentCaseIndex, m_cases.size() - 1));
    m_isUpdatingUi = false;

    m_currentCaseIndex = m_caseSelector->currentIndex();
    loadCaseToUi(m_currentCaseIndex);
}

void CaveSupportComputationPage::onAddMaterialRow()
{
    const int row = m_materialTable->rowCount();
    m_materialTable->insertRow(row);
    m_materialTable->setItem(row, 0, new QTableWidgetItem("1"));
    m_materialTable->setItem(row, 1, new QTableWidgetItem("1"));
}

void CaveSupportComputationPage::onDeleteMaterialRow()
{
    const auto rows = m_materialTable->selectionModel()->selectedRows();
    for (int i = rows.size() - 1; i >= 0; --i)
        m_materialTable->removeRow(rows.at(i).row());
    if (m_materialTable->rowCount() == 0)
        onAddMaterialRow();
}

void CaveSupportComputationPage::saveCurrentCase()
{
    if (m_currentCaseIndex < 0 || m_currentCaseIndex >= m_cases.size())
        return;

    CaseData& c = m_cases[m_currentCaseIndex];
    c.icase = m_currentCaseIndex + 1;
    c.iexcaSta = m_iexcaStaEdit->text().trimmed();
    c.iexcaEnd = m_iexcaEndEdit->text().trimmed();
    c.ifEall = m_ifEallCombo->currentData().toString().trimmed();
    c.ifJall = m_ifJallCombo->currentData().toString().trimmed();
    c.ifE1toa = m_ifE1toaCombo->currentData().toString().trimmed();
    c.ifJ1toa = m_ifJ1toaCombo->currentData().toString().trimmed();
    c.jaMethod = m_jaMethodCombo->currentData().toString().trimmed();
    c.ifChen = m_ifChenCombo->currentData().toString().trimmed();

    c.materialChanges.clear();
    for (int r = 0; r < m_materialTable->rowCount(); ++r)
    {
        const QString oldVal = m_materialTable->item(r, 0) ? m_materialTable->item(r, 0)->text().trimmed() : "1";
        const QString newVal = m_materialTable->item(r, 1) ? m_materialTable->item(r, 1)->text().trimmed() : "1";
        c.materialChanges.push_back({oldVal.isEmpty() ? "1" : oldVal, newVal.isEmpty() ? "1" : newVal});
    }
    if (c.materialChanges.isEmpty())
        c.materialChanges.push_back({"1", "1"});
}

void CaveSupportComputationPage::loadCaseToUi(int index)
{
    if (index < 0 || index >= m_cases.size())
        return;

    m_isUpdatingUi = true;

    const CaseData& c = m_cases.at(index);
    m_icaseEdit->setText(QString::number(c.icase));
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

    m_materialTable->setRowCount(c.materialChanges.size());
    for (int r = 0; r < c.materialChanges.size(); ++r)
    {
        m_materialTable->setItem(r, 0, new QTableWidgetItem(c.materialChanges[r].first));
        m_materialTable->setItem(r, 1, new QTableWidgetItem(c.materialChanges[r].second));
    }

    m_isUpdatingUi = false;
}

void CaveSupportComputationPage::refreshCaseSelector()
{
    m_caseSelector->clear();
    for (int i = 0; i < m_cases.size(); ++i)
        m_caseSelector->addItem(QString("工况 %1").arg(i + 1));
}

void CaveSupportComputationPage::ensureCaseCount(int count)
{
    const int target = qMax(1, count);
    const int oldSize = m_cases.size();
    if (oldSize < target)
    {
        for (int i = oldSize; i < target; ++i)
            m_cases.push_back(defaultCase(i));
    }
    else if (oldSize > target)
    {
        m_cases.resize(target);
    }

    for (int i = 0; i < m_cases.size(); ++i)
        m_cases[i].icase = i + 1;

    if (m_currentCaseIndex >= m_cases.size())
        m_currentCaseIndex = m_cases.size() - 1;
}

CaveSupportComputationPage::CaseData CaveSupportComputationPage::defaultCase(int index) const
{
    CaseData c;
    c.icase = index + 1;
    c.materialChanges.push_back({"1", "1"});
    return c;
}
