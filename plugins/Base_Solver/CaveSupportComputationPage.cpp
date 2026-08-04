#include "CaveSupportComputationPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QSet>
#include <QStringConverter>
#include <QTextStream>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QVBoxLayout>

#include "ui_CaveSupportComputationPage.h"

#include "CaveReinforcementFieldPage.h"
#include "SolverUiLayoutHelper.h"
CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation"), ui(new Ui::CaveSupportComputationPage)
{
    ui->setupUi(this);
    m_reinforcementFieldPage = new CaveReinforcementFieldPage(this);
    // 放在“开挖任务”之后、“动力参数”之前；它属于加固任务设置，不再作为树节点。
    ui->contentLayout->insertWidget(4, m_reinforcementFieldPage);
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);
    Q_UNUSED(parent);

    m_ifSeepCombo = ui->comboIfSeep;
    m_dampingModeCombo = ui->comboDampingMode;
    m_calculationTypeCombo = ui->comboCalculationType;
    m_excavationLoadMethodCombo = ui->comboExcavationLoadMethod;
    m_iterationMethodCombo = ui->comboIterationMethod;
    m_iterationCountSpin = ui->spinIterationCount;
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
    m_accXEdit->setText(QStringLiteral("WAVE-AX.DAT"));
    m_accYEdit->setText(QStringLiteral("WAVE-AY.DAT"));
    m_accZEdit->setText(QStringLiteral("WAVE-AZ.DAT"));

    m_excavationDaysTable->setColumnCount(2);
    m_excavationDaysTable->setHorizontalHeaderLabels({"开挖级", "用时（天）"});
    m_excavationDaysTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto setupBoolCombo = [](QComboBox* combo, const QString& enableText, const QString& disableText)
    {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(disableText, "0");
        combo->addItem(enableText, "1");
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
    ui->groupParams->setToolTip(
        QStringLiteral("四种开挖任务可以同时启用，求解器会分别计算并输出对应结果。"));

    m_ifSeepCombo->clear();
    m_ifSeepCombo->addItem("不考虑地下水影响", "0");
    m_ifSeepCombo->addItem("考虑地下水影响", "1");

    m_dampingModeCombo->clear();
    m_dampingModeCombo->addItem("使用系统统一阻尼", "0");
    m_dampingModeCombo->addItem("使用分材料阻尼", "1");

    m_calculationTypeCombo->clear();
    m_calculationTypeCombo->addItem("仅进行静力开挖计算", "1");
    m_calculationTypeCombo->addItem("仅进行动力计算", "2");
    m_calculationTypeCombo->addItem("静力开挖后继续动力计算", "3");

    m_excavationLoadMethodCombo->clear();
    m_excavationLoadMethodCombo->addItem("按开挖面计算", "1");
    m_excavationLoadMethodCombo->addItem("按开挖单元计算", "2");
    m_excavationLoadMethodCombo->setCurrentIndex(1);

    m_iterationMethodCombo->clear();
    m_iterationMethodCombo->addItem("线弹性分析", "0");
    m_iterationMethodCombo->addItem("初应变法", "1");
    m_iterationMethodCombo->addItem("初应力法", "2");
    m_iterationMethodCombo->addItem("变刚度法（切线刚度法）", "3");
    m_iterationMethodCombo->setCurrentIndex(1);
    m_iterationCountSpin->setValue(100);

    m_jaMethodCombo->clear();
    m_jaMethodCombo->addItem("锚杆或锚索加固，不考虑衬砌", "1");
    m_jaMethodCombo->addItem("同时考虑锚杆、锚索和衬砌加固", "2");
    m_idayCaseEdit->setText("0");

    m_ifModeCombo->clear();
    m_ifModeCombo->addItem("不进行模态分析", "0");
    m_ifModeCombo->addItem("进行模态分析", "1");
    m_ifWaveCombo->clear();
    m_ifWaveCombo->addItem("不进行时程分析", "0");
    m_ifWaveCombo->addItem("进行时程分析", "1");

    connect(m_iexcaMaxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &CaveSupportComputationPage::onExcavationLevelCountChanged);
    connect(m_calculationTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::updateAnalysisModeUi);
    connect(m_ifWaveCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::updateDynamicDetailsUi);
    connect(m_ifSeepCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::updateStaticDependenciesUi);
    for (QComboBox* combo : {m_ifEallCombo, m_ifJallCombo, m_ifE1toaCombo,
                             m_ifJ1toaCombo, m_ifChenCombo})
    {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &CaveSupportComputationPage::updateStaticDependenciesUi);
    }
    connect(m_ifJallCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::onReinforcedTaskSelectionChanged);
    connect(m_ifJ1toaCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::onReinforcedTaskSelectionChanged);
    connect(ui->btnCompute, &QPushButton::clicked, this, &CaveSupportComputationPage::onComputeClicked);

    m_caseData = CaseData();
    onExcavationLevelCountChanged(m_iexcaMaxSpin->value());
    loadCaseToUi();
    updateAnalysisModeUi();
}

CaveSupportComputationPage::~CaveSupportComputationPage()
{
    delete ui;
}

void CaveSupportComputationPage::beforeLoad()
{
    updateAnalysisModeUi();
}

void CaveSupportComputationPage::beforeUnload() {}

void CaveSupportComputationPage::setExcavationLevelCount(int count)
{
    const int target = qMax(1, count);

    if (m_iexcaMaxSpin)
    {
        const QSignalBlocker blocker(m_iexcaMaxSpin);
        m_iexcaMaxSpin->setValue(target);
    }

    onExcavationLevelCountChanged(target);

    // Fortran 中开挖层号使用 -1、-2、...、-N。
    m_caseData.iexcaSta = QStringLiteral("-1");
    m_caseData.iexcaEnd = QString::number(-target);

    if (m_iexcaStaEdit)
        m_iexcaStaEdit->setText(m_caseData.iexcaSta);
    if (m_iexcaEndEdit)
        m_iexcaEndEdit->setText(m_caseData.iexcaEnd);
}

int CaveSupportComputationPage::excavationLevelCount() const
{
    return m_iexcaMaxSpin ? qMax(1, m_iexcaMaxSpin->value()) : 1;
}

int CaveSupportComputationPage::calculationTypeCode() const
{
    if (!m_calculationTypeCombo)
        return 1;

    bool ok = false;
    const int code = m_calculationTypeCombo->currentData().toString().toInt(&ok);
    return ok ? code : 1;
}

int CaveSupportComputationPage::excavationLoadMethodCode() const
{
    if (!m_excavationLoadMethodCombo)
        return 2;

    bool ok = false;
    const int code = m_excavationLoadMethodCombo->currentData().toString().toInt(&ok);
    return ok ? code : 2;
}

bool CaveSupportComputationPage::modalAnalysisEnabled() const
{
    return m_ifModeCombo &&
           m_ifModeCombo->currentData().toString() == QStringLiteral("1");
}

bool CaveSupportComputationPage::timeHistoryEnabled() const
{
    return m_ifWaveCombo &&
           m_ifWaveCombo->currentData().toString() == QStringLiteral("1");
}

bool CaveSupportComputationPage::reinforcedTaskEnabled() const
{
    return (m_ifJallCombo && m_ifJallCombo->currentData().toString() == QStringLiteral("1"))
           || (m_ifJ1toaCombo
               && m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1"));
}

CaveReinforcementFieldPage* CaveSupportComputationPage::reinforcementFieldPage() const
{
    return m_reinforcementFieldPage;
}

void CaveSupportComputationPage::loadLegacyReinforcementFieldJson(const QJsonObject& obj)
{
    if (!m_reinforcementFieldPage || obj.isEmpty())
        return;

    m_reinforcementFieldPage->loadJson(obj);
    updateStaticDependenciesUi();
}

void CaveSupportComputationPage::updateAnalysisModeUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int typeCode = calculationTypeCode();
    const bool showStatic = (typeCode == 1 || typeCode == 3);
    const bool showDynamic = (typeCode == 2 || typeCode == 3);

    // 静力开挖相关设置始终保留在页面中；不参与当前计算时仅置灰，
    // 让用户仍能看到四种开挖任务及其依赖关系。
    for (QWidget* group : {ui->groupStaticControl, ui->groupExcavationTime,
                           ui->groupParams})
    {
        if (group)
        {
            group->setVisible(true);
            group->setEnabled(showStatic);
        }
    }

    if (ui->groupDynamicControl)
        ui->groupDynamicControl->setVisible(showDynamic);
    if (ui->groupDyna)
        ui->groupDyna->setVisible(showDynamic);

    updateDynamicDetailsUi();
    updateStaticDependenciesUi();
}

void CaveSupportComputationPage::updateDynamicDetailsUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int typeCode = calculationTypeCode();
    const bool dynamicVisible = (typeCode == 2 || typeCode == 3);
    const bool showTimeHistory = dynamicVisible && timeHistoryEnabled();

    ui->labelDt->setVisible(showTimeHistory);
    ui->widgetTimeParams->setVisible(showTimeHistory);
    ui->labelTsta->setVisible(showTimeHistory);
    ui->spinTsta->setVisible(showTimeHistory);
    ui->labelTend->setVisible(showTimeHistory);
    ui->spinTend->setVisible(showTimeHistory);
    ui->labelWaveFiles->hide();
    ui->widgetWaveFiles->hide();
}

void CaveSupportComputationPage::updateStaticDependenciesUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);
    const bool showStatic = calculationTypeCode() == 1 || calculationTypeCode() == 3;
    const bool seepage = showStatic &&
                         m_ifSeepCombo->currentData().toString() == QStringLiteral("1");
    ui->label5->setVisible(seepage);
    ui->line5Edit->setVisible(seepage);
    ui->label7->setVisible(seepage);
    ui->line7Edit->setVisible(seepage);

    const bool support = showStatic &&
        (m_ifJallCombo->currentData().toString() == QStringLiteral("1") ||
         m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1"));
    QWidget* const supportWidgets[] = {
        ui->label25, ui->line25Edit, ui->labelJaMethod, m_jaMethodCombo,
        ui->labelIfChen, m_ifChenCombo, ui->labelIdayCase, m_idayCaseEdit};
    for (QWidget* widget : supportWidgets)
    {
        if (widget)
        {
            widget->setVisible(true);
            widget->setEnabled(support);
        }
    }

    if (m_reinforcementFieldPage)
    {
        m_reinforcementFieldPage->setVisible(true);
        m_reinforcementFieldPage->setEnabled(support);
    }
}

void CaveSupportComputationPage::onReinforcedTaskSelectionChanged()
{
    if (m_isUpdatingUi)
        return;

    // 不改写用户已经选择的修正方式；无加固任务时仅将相关设置置灰。
    updateStaticDependenciesUi();
}

bool CaveSupportComputationPage::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    auto parse = [](const QLineEdit* edit, double* out = nullptr)
    {
        bool ok = false;
        const double value = edit->text().trimmed().toDouble(&ok);
        if (ok && out)
            *out = value;
        return ok;
    };

    const int type = calculationTypeCode();
    if (type == 1 || type == 3)
    {
        if (m_ifSeepCombo->currentData().toString() == QStringLiteral("1") &&
            (!parse(ui->line5Edit) || !parse(ui->line7Edit)))
            return fail(QStringLiteral("考虑地下水影响时，洞内水位和衬砌外水位必须为数值。"));

        double release = 0.0;
        if (!parse(ui->line11Edit, &release) || release < 0.0 || release > 1.0)
            return fail(QStringLiteral("围岩已释放荷载系数必须在 0 到 1 之间。"));

        const bool hasTask = m_ifEallCombo->currentData().toString() == QStringLiteral("1") ||
                             m_ifJallCombo->currentData().toString() == QStringLiteral("1") ||
                             m_ifE1toaCombo->currentData().toString() == QStringLiteral("1") ||
                             m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1");
        if (!hasTask)
            return fail(QStringLiteral("至少需要选择一种洞室开挖任务。"));

        for (int row = 0; row < m_excavationDaysTable->rowCount(); ++row)
        {
            const auto* item = m_excavationDaysTable->item(row, 1);
            bool ok = false;
            const double days = item ? item->text().trimmed().toDouble(&ok) : 0.0;
            if (!ok || days < 0.0)
                return fail(QStringLiteral("第 %1 级开挖用时必须为非负数。").arg(row + 1));
        }
    }

    if ((type == 2 || type == 3) && timeHistoryEnabled())
    {
        if (m_dtSpin->value() <= 0.0)
            return fail(QStringLiteral("时程分析的时间步长必须为正数。"));
        if (m_tendSpin->value() <= m_tstaSpin->value())
            return fail(QStringLiteral("时程分析的终止时刻必须大于起始时刻。"));
    }
    return true;
}

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
    QString caseDir = QDir(SolverFilePaths::outputRoot(projectDir)).filePath("CASE01");
    caseDir = QDir::toNativeSeparators(caseDir) + QDir::separator();
    text += QString("'%1'     DIRDYNA\n").arg(caseDir);
    text += QString("%1     %2   IFMODE   IFWAVE\n")
                .arg(m_ifModeCombo ? m_ifModeCombo->currentData().toString() : "0",
                     m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : "0");
    text += QString("%1    %2   %3    DT TSTA TEND\n")
                .arg(m_dtSpin ? m_dtSpin->value() : 0.005, 0, 'f', 6)
                .arg(m_tstaSpin ? m_tstaSpin->value() : 0.0, 0, 'f', 6)
                .arg(m_tendSpin ? m_tendSpin->value() : 30.0, 0, 'f', 6);
    text += "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n";
    return text;
}

bool CaveSupportComputationPage::exportICaveDatFile(const QString& supportCaseSection) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(wd.filePath("ICAVE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);
    const QHash<int, QString> fixedValues = {
        {1, QDir::toNativeSeparators(inputDir.absolutePath()) + QDir::separator()},
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

    const QSet<int> quotedFields = {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
    for (int fieldNo = 1; fieldNo <= 25; ++fieldNo)
    {
        QString value;
        if (fixedValues.contains(fieldNo))
            value = fixedValues.value(fieldNo);
        else
            value = controlFieldValue(fieldNo);

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
    obj["ifmode"] = m_ifModeCombo ? m_ifModeCombo->currentData().toString() : "0";
    obj["ifwave"] = m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : "0";
    obj["dt"] = m_dtSpin ? m_dtSpin->value() : 0.005;
    obj["tsta"] = m_tstaSpin ? m_tstaSpin->value() : 0.0;
    obj["tend"] = m_tendSpin ? m_tendSpin->value() : 30.0;
    obj["acc_x"] = "WAVE-AX.DAT";
    obj["acc_y"] = "WAVE-AY.DAT";
    obj["acc_z"] = "WAVE-AZ.DAT";

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
    const QStringList fixed = {"1", "2", "4", "6", "8", "9", "12", "13", "14", "15", "16", "17"};
    for (int i = 1; i <= 25; ++i)
    {
        if (fixed.contains(QString::number(i)))
            continue;
        obj[QString("line_%1").arg(i)] = controlFieldValue(i);
    }
    if (m_reinforcementFieldPage)
        obj[QStringLiteral("reinforcement_field")] = m_reinforcementFieldPage->saveJson();
    return obj;
}

void CaveSupportComputationPage::loadJson(const QJsonObject& obj)
{
    m_isUpdatingUi = true;

    m_caseData = CaseData();
    if (m_excavationDaysTable)
        m_excavationDaysTable->setUpdatesEnabled(false);

    const int iexcaMax = qMax(1, obj.value("iexca_max").toInt(1));
    if (m_iexcaMaxSpin)
    {
        const QSignalBlocker blocker(m_iexcaMaxSpin);
        m_iexcaMaxSpin->setValue(iexcaMax);
    }
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
        const int idx = m_ifModeCombo->findData(obj.value("ifmode").toString("0"));
        m_ifModeCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_ifWaveCombo)
    {
        const int idx = m_ifWaveCombo->findData(obj.value("ifwave").toString("0"));
        m_ifWaveCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    if (m_dtSpin)
        m_dtSpin->setValue(obj.value("dt").toDouble(0.005));
    if (m_tstaSpin)
        m_tstaSpin->setValue(obj.value("tsta").toDouble(0.0));
    if (m_tendSpin)
        m_tendSpin->setValue(obj.value("tend").toDouble(30.0));
    if (m_accXEdit)
        m_accXEdit->setText(QStringLiteral("WAVE-AX.DAT"));
    if (m_accYEdit)
        m_accYEdit->setText(QStringLiteral("WAVE-AY.DAT"));
    if (m_accZEdit)
        m_accZEdit->setText(QStringLiteral("WAVE-AZ.DAT"));

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

    loadLegacyCaveJson(obj);
    if (m_reinforcementFieldPage)
    {
        const QJsonObject fieldState =
            obj.value(QStringLiteral("reinforcement_field")).toObject();
        if (!fieldState.isEmpty())
            m_reinforcementFieldPage->loadJson(fieldState);
    }

    if (m_excavationDaysTable)
        m_excavationDaysTable->setUpdatesEnabled(true);

    m_isUpdatingUi = false;
    loadCaseToUi();
    updateAnalysisModeUi();
}

void CaveSupportComputationPage::loadLegacyCaveJson(const QJsonObject& obj)
{
    const QStringList fixed = {"1", "2", "4", "6", "8", "9", "12", "13", "14", "15", "16", "17"};
    for (int i = 1; i <= 25; ++i)
    {
        if (fixed.contains(QString::number(i)))
            continue;
        const QString key = QString("line_%1").arg(i);
        if (obj.contains(key))
            setControlFieldValue(i, obj.value(key).toString(controlFieldValue(i)));
    }
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

    SolverUiLayoutHelper::fitTableToRows(m_excavationDaysTable, target, 2, 6);

    if (!m_isUpdatingUi)
    {
        m_caseData.iexcaSta = QStringLiteral("-1");
        m_caseData.iexcaEnd = QString::number(-target);
        if (m_iexcaStaEdit)
            m_iexcaStaEdit->setText(m_caseData.iexcaSta);
        if (m_iexcaEndEdit)
            m_iexcaEndEdit->setText(m_caseData.iexcaEnd);
    }
}

QString CaveSupportComputationPage::controlFieldValue(int fieldNo) const
{
    auto comboValue = [](QComboBox* combo, const QString& fallback)
    {
        if (!combo)
            return fallback;
        const QString value = combo->currentData().toString().trimmed();
        return value.isEmpty() ? fallback : value;
    };

    switch (fieldNo)
    {
    case 3:
        return comboValue(m_ifSeepCombo, QStringLiteral("0"));
    case 18:
        return comboValue(m_dampingModeCombo, QStringLiteral("0"));
    case 22:
        return comboValue(m_calculationTypeCombo, QStringLiteral("1"));
    case 23:
        return comboValue(m_excavationLoadMethodCombo, QStringLiteral("2"));
    case 24:
        return QStringLiteral("%1 %2")
            .arg(comboValue(m_iterationMethodCombo, QStringLiteral("1")))
            .arg(m_iterationCountSpin ? m_iterationCountSpin->value() : 100);
    default:
        break;
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("line%1Edit").arg(fieldNo)))
        return edit->text().trimmed();
    return QString();
}

void CaveSupportComputationPage::setControlFieldValue(int fieldNo, const QString& value)
{
    auto setCombo = [](QComboBox* combo, const QString& code)
    {
        if (!combo)
            return;
        const int index = combo->findData(code.trimmed());
        if (index >= 0)
            combo->setCurrentIndex(index);
    };

    switch (fieldNo)
    {
    case 3:
        setCombo(m_ifSeepCombo, value);
        return;
    case 18:
        setCombo(m_dampingModeCombo, value);
        return;
    case 22:
        setCombo(m_calculationTypeCombo, value);
        return;
    case 23:
        setCombo(m_excavationLoadMethodCombo, value);
        return;
    case 24:
    {
        const QStringList parts = value.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        setCombo(m_iterationMethodCombo, parts.value(0, QStringLiteral("1")));
        bool ok = false;
        const int count = parts.value(1, QStringLiteral("100")).toInt(&ok);
        if (m_iterationCountSpin)
            m_iterationCountSpin->setValue(ok ? qMax(1, count) : 100);
        return;
    }
    default:
        break;
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("line%1Edit").arg(fieldNo)))
        edit->setText(value);
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
    updateStaticDependenciesUi();
}

void CaveSupportComputationPage::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(this, QStringLiteral("设置不完整"), error);
        return;
    }
    emit computeRequested();
}

void CaveSupportComputationPage::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}
