#include "DamAnalysisWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QIntValidator>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMap>
#include <QStringConverter>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QWidget>

#include "ui_DamAnalysisWidget.h"

#include "SolverUiLayoutHelper.h"
DamAnalysisWidget::DamAnalysisWidget(QWidget* parent)
    : BasePage("DamAnalysis"), ui(new Ui::DamAnalysisWidget)
{
    ui->setupUi(this);
    initValueEditors();
    SolverUiLayoutHelper::polishPage(this);
    connect(ui->btnCompute, &QPushButton::clicked, this, &DamAnalysisWidget::onComputeClicked);
}

DamAnalysisWidget::~DamAnalysisWidget()
{
    delete ui;
}

void DamAnalysisWidget::beforeLoad()
{
}

void DamAnalysisWidget::beforeUnload()
{
}

void DamAnalysisWidget::initValueEditors()
{
    m_comboMethodIter = ui->comboMethodIter;
    m_editIter = ui->editIter;
    m_editIter->setValidator(new QIntValidator(1, 1000000, m_editIter));
    m_editX1 = ui->editX1;
    m_editX2 = ui->editX2;
    m_editX3 = ui->editX3;
    m_editAlfa1 = ui->editAlfa1;
    m_editAlfa2 = ui->editAlfa2;
    m_editAlfa3 = ui->editAlfa3;
    m_comboAxisUp = ui->comboAxisUp;
    m_comboAxisDown = ui->comboAxisDown;
    m_comboIfRock = ui->comboIfRock;
    m_comboIfIso = ui->comboIfIso;

    auto* damTypeLabel = new QLabel(QStringLiteral("均质坝"), this);
    damTypeLabel->setObjectName(QStringLiteral("labelDamTypeValue"));
    ui->formLayoutMode->addRow(QStringLiteral("坝体类型"), damTypeLabel);

    m_groupFreezeThaw = new QGroupBox(QStringLiteral("冻融与结冰参数"), this);
    m_groupFreezeThaw->setObjectName(QStringLiteral("groupFreezeThaw"));
    m_groupFreezeThaw->setCheckable(true);
    m_groupFreezeThaw->setChecked(false);
    auto* freezeForm = new QFormLayout(m_groupFreezeThaw);
    auto addFreezeEdit = [this, freezeForm](const QString& label, const QString& value,
                                            const QString& objectName)
    {
        auto* edit = new QLineEdit(value, m_groupFreezeThaw);
        edit->setObjectName(objectName);
        freezeForm->addRow(label, edit);
        return edit;
    };
    m_editFreezeCycles = addFreezeEdit(QStringLiteral("冻融循环次数"), QStringLiteral("0"),
                                      QStringLiteral("editLine31"));
    m_editFreezeCycles->setValidator(new QIntValidator(0, 1000000, m_editFreezeCycles));
    m_editFreezeCoefficient =
        addFreezeEdit(QStringLiteral("冻融影响系数"), QStringLiteral("-0.206"),
                      QStringLiteral("editLine32"));
    m_editFreezeElevation =
        addFreezeEdit(QStringLiteral("影响高程范围（下限 上限）"),
                      QStringLiteral("2100.0 2105.0"), QStringLiteral("editLine33"));
    m_editFreezeCompression =
        addFreezeEdit(QStringLiteral("抗压强度系数（b c）"), QStringLiteral("2.7110 2.9480"),
                      QStringLiteral("editLine34"));
    m_editFreezeTension =
        addFreezeEdit(QStringLiteral("抗拉强度系数（b c）"), QStringLiteral("24.381 3.1310"),
                      QStringLiteral("editLine35"));
    m_editIceExpansion =
        addFreezeEdit(QStringLiteral("冰线膨胀率"), QStringLiteral("0.0357"),
                      QStringLiteral("editLine37"));
    ui->contentLayout->insertWidget(ui->contentLayout->indexOf(ui->btnCompute),
                                    m_groupFreezeThaw);

    m_lineEditors[1] = nullptr;
    m_lineEditors[2] = nullptr;
    m_lineEditors[3] = nullptr;
    m_lineEditors[4] = nullptr;
    m_lineEditors[5] = nullptr;
    m_lineEditors[6] = nullptr;
    m_lineEditors[7] = nullptr;
    m_lineEditors[8] = nullptr;
    m_lineEditors[9] = nullptr;
    m_lineEditors[10] = nullptr;
    m_lineEditors[11] = nullptr;
    m_lineEditors[12] = nullptr;
    m_lineEditors[13] = nullptr;
    m_lineEditors[14] = nullptr;
    m_lineEditors[15] = ui->comboLine15;
    m_lineEditors[16] = ui->comboLine16;
    m_lineEditors[17] = ui->editLine17;
    m_lineEditors[18] = ui->editLine18;
    m_lineEditors[19] = ui->comboLine19;
    m_lineEditors[20] = nullptr;
    m_lineEditors[21] = ui->editLine21;
    m_lineEditors[22] = ui->editLine22;
    m_lineEditors[23] = ui->editLine23;
    m_lineEditors[24] = ui->editLine24;
    m_lineEditors[25] = ui->editLine25;
    m_lineEditors[26] = ui->editLine26;
    m_lineEditors[27] = ui->editLine27;
    m_lineEditors[28] = ui->editLine28;
    m_lineEditors[29] = ui->comboLine29;
    m_lineEditors[30] = ui->editLine30;
    m_lineEditors[31] = m_editFreezeCycles;
    m_lineEditors[32] = m_editFreezeCoefficient;
    m_lineEditors[33] = m_editFreezeElevation;
    m_lineEditors[34] = m_editFreezeCompression;
    m_lineEditors[35] = m_editFreezeTension;
    m_lineEditors[36] = nullptr;
    m_lineEditors[37] = m_editIceExpansion;
    m_lineEditors[38] = nullptr;
    m_lineEditors[39] = nullptr;
    m_lineEditors[40] = nullptr;

    ui->comboLine15->setItemData(0, "0");
    ui->comboLine15->setItemData(1, "1");

    ui->comboLine16->setItemData(0, "0");
    ui->comboLine16->setItemData(1, "1");

    ui->comboLine19->setItemData(0, "1");
    ui->comboLine19->setItemData(1, "2");
    ui->comboLine19->setItemData(2, "3");

    ui->comboLine29->setItemData(0, "0");
    ui->comboLine29->setItemData(1, "1");

    m_comboMethodIter->setItemData(0, "1");
    m_comboMethodIter->setItemData(1, "2");
    m_comboMethodIter->setItemData(2, "3");
    m_comboMethodIter->setItemData(3, "4");
    m_comboMethodIter->setCurrentIndex(3);

    m_comboAxisUp->setItemData(0, "1");
    m_comboAxisUp->setItemData(1, "2");
    m_comboAxisUp->setItemData(2, "3");
    m_comboAxisDown->setItemData(0, "1");
    m_comboAxisDown->setItemData(1, "2");
    m_comboAxisDown->setItemData(2, "3");
    m_comboAxisUp->setCurrentIndex(0);
    m_comboAxisDown->setCurrentIndex(2);

    m_comboIfRock->setItemData(0, "0");
    m_comboIfRock->setItemData(1, "1");
    m_comboIfIso->setItemData(0, "0");
    m_comboIfIso->setItemData(1, "1");
    m_comboIfIso->setItemText(0, QStringLiteral("按建模材料计算"));
    m_comboIfIso->setItemText(1, QStringLiteral("按替代材料计算"));

    connect(ui->comboLine19, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]()
            {
                updateAnalysisModeVisibility();
                emit requirementsChanged();
            });
    connect(ui->comboLine16, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateDynamicOptionVisibility(); });
    connect(ui->comboLine29, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateDynamicOptionVisibility(); });
    connect(m_editFreezeCycles, &QLineEdit::textChanged, this,
            [this]() { updateFreezeThawOptionState(); });
    connect(m_groupFreezeThaw, &QGroupBox::toggled, this,
            [this]()
            {
                updateFreezeThawOptionState();
                emit requirementsChanged();
            });
    updateAnalysisModeVisibility();
    updateFreezeThawOptionState();
    updateUpliftOptionState();
}

void DamAnalysisWidget::updateFreezeThawOptionState()
{
    const bool enabled = freezeThawEnabled();
    for (QLineEdit* edit : {m_editFreezeCycles, m_editFreezeCoefficient, m_editFreezeElevation,
                            m_editFreezeCompression, m_editFreezeTension, m_editIceExpansion})
    {
        if (edit)
            edit->setEnabled(enabled);
    }
}

bool DamAnalysisWidget::freezeThawEnabled() const
{
    return m_groupFreezeThaw && m_groupFreezeThaw->isChecked();
}

bool DamAnalysisWidget::dynamicAnalysisEnabled() const
{
    const int type = ui->comboLine19->currentData().toString().toInt();
    return type == 2 || type == 3;
}

void DamAnalysisWidget::setUpliftReductionRequired(bool required)
{
    if (m_upliftReductionRequired == required)
        return;
    m_upliftReductionRequired = required;
    updateUpliftOptionState();
}

void DamAnalysisWidget::updateUpliftOptionState()
{
    if (!ui || !ui->formLayoutStatic)
        return;
    for (QLineEdit* edit : {m_editX1, m_editX2, m_editX3,
                            m_editAlfa1, m_editAlfa2, m_editAlfa3})
    {
        if (edit)
            ui->formLayoutStatic->setRowVisible(edit, m_upliftReductionRequired);
    }
}

void DamAnalysisWidget::updateAnalysisModeVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int type = ui->comboLine19->currentData().toString().toInt();
    const bool showStatic = (type == 1 || type == 3);
    const bool showDynamic = (type == 2 || type == 3);

    ui->groupStatic->setVisible(showStatic);
    ui->groupDynamic->setVisible(showDynamic);

    updateDynamicOptionVisibility();
}

void DamAnalysisWidget::updateDynamicOptionVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int type = ui->comboLine19->currentData().toString().toInt();
    const bool showDynamic = (type == 2 || type == 3);
    const bool rayleigh = showDynamic &&
                          ui->comboLine16->currentData().toString() == QStringLiteral("1");

    // 次级开关采用“禁用”而不是反复插入/删除表单行，避免页面高度跳动。
    ui->labelLine17->setEnabled(rayleigh);
    ui->editLine17->setEnabled(rayleigh);
    ui->labelLine18->setEnabled(rayleigh);
    ui->editLine18->setEnabled(rayleigh);

    const bool eigen = showDynamic &&
                       ui->comboLine29->currentData().toString() == QStringLiteral("1");
    ui->labelLine30->setEnabled(eigen);
    ui->editLine30->setEnabled(eigen);
}

QString DamAnalysisWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString DamAnalysisWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {
        "", "FA_DAMUP.DAT", "FA_DAMDW.DAT", "FA_DAMBASE.DAT",
        "NONE", "MA_ROCK.DAT", "MA_DAMUP.DAT", "MA_DAMDW.DAT", "MA_CORE.DAT", "4",
        "MA_PLATE.DAT", "JDTRACE.DAT", "KETRACE.DAT", "TDTRACE.DAT", "0", "0", "0.05", "0.0",
        "2", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10", "0", "-0.206", "2100.0 2105.0", "2.7110 2.9480", "24.381 3.1310",
        "JMFW.DAT", "0.0357", "---", "---", "---"};
    return defaults.value(lineNo - 1);
}

QString DamAnalysisWidget::descriptionByLine(int lineNo) const
{
    static const QStringList descriptions = {
        "计算默认文件目录", "上游水头边界集文件", "下游水头边界集文件", "心墙建基面文件",
        "坝体中心面文件（已停用，占位参数）", "基岩材料集（层号为1）", "上游材料集（面板坝不需要）",
        "下游材料集", "心墙材料集（面板坝不需要）",
        "坝体类型：均质坝",
        "面板材料集文件", "特征节点集文件", "特征单元集文件", "特征时刻文件",
        "阻尼：0系统阻尼 / 1分材料阻尼", "瑞雷阻尼：0否 / 1是", "质量阻尼系数",
        "刚度阻尼系数", "计算类型：1静力 / 2动力 / 3静动力", "迭代方法与迭代次数",
        "蠕变计算时间步长 DT_CREEP", "水平最大地震加速度 AHMAX", "竖向最大地震加速度 AVMAX",
        "反应谱特征周期 TG1", "反应谱特征周期 TG2", "反应谱 BETAMAX", "反应谱指数 EXPN",
        "地震震级 QUAKE_LEVEL", "是否计算特征值特征向量：0否 / 1是", "特征值数",
        "冻融循环次数", "冻融影响系数", "冻融影响高程范围", "冻融抗压强度系数",
        "冻融抗拉强度系数", "结冰节点集合（内部参数）", "冰线膨胀率", "保留参数", "保留参数",
        "保留参数"};
    return descriptions.value(lineNo - 1);
}

QString DamAnalysisWidget::normalizeEnumValue(const QString& rawValue) const
{
    const QString trimmed = rawValue.trimmed();
    const int quoteStart = trimmed.indexOf('\'');
    if (quoteStart < 0)
        return trimmed;
    const int quoteEnd = trimmed.lastIndexOf('\'');
    if (quoteEnd <= quoteStart)
        return trimmed;
    return trimmed.mid(quoteStart + 1, quoteEnd - quoteStart - 1).trimmed();
}

QString DamAnalysisWidget::lineValue(int lineNo) const
{
    if (lineNo == 20)
        return QString("%1 %2").arg(m_comboMethodIter->currentData().toString(),
                                    m_editIter->text().trimmed());
    if (lineNo == 31 && !freezeThawEnabled())
        return QStringLiteral("0");

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
        return combo->currentData().toString();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    if (auto* label = qobject_cast<QLabel*>(w))
        return label->text().trimmed();
    return defaultValueByLine(lineNo);
}

void DamAnalysisWidget::setLineValue(int lineNo, const QString& value)
{
    if (lineNo == 20)
    {
        const QStringList parts =
            value.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString method = parts.value(0, "4");
        const QString iter = parts.value(1, "10");
        const int idx = m_comboMethodIter->findData(method);
        if (idx >= 0)
            m_comboMethodIter->setCurrentIndex(idx);
        m_editIter->setText(iter);
        return;
    }

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
    {
        const int idx = combo->findData(value.trimmed());
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        return;
    }
    if (auto* edit = qobject_cast<QLineEdit*>(w))
    {
        edit->setText(value);
        return;
    }
}

bool DamAnalysisWidget::isFixedFileLine(int lineNo) const
{
    static const QSet<int> fixedLines = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
                                         36, 38, 39, 40};
    return fixedLines.contains(lineNo);
}

bool DamAnalysisWidget::validateInputs(QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    auto setError = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
    };

    static const QSet<int> oneTokenLines = {10, 15, 16, 17, 18, 19, 21, 22, 23, 24,
                                           25, 26, 27, 28, 29, 30, 31, 32, 37};
    static const QSet<int> twoTokenLines = {33, 34, 35};
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        if (!freezeThawEnabled() && lineNo >= 31 && lineNo <= 37)
            continue;
        const QString text = normalizeEnumValue(lineValue(lineNo));
        if (text.trimmed().isEmpty())
        {
            setError(QString("%1不能为空。").arg(descriptionByLine(lineNo)));
            return false;
        }
        if (oneTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 1)
            {
                setError(QString("%1只能填写一个值。").arg(descriptionByLine(lineNo)));
                return false;
            }
        }
        if (twoTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 2)
            {
                setError(QString("%1需要填写两个数值，以空格分隔。").arg(descriptionByLine(lineNo)));
                return false;
            }
        }

        if ((oneTokenLines.contains(lineNo) || twoTokenLines.contains(lineNo)) && lineNo >= 10)
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            for (const QString& part : parts)
            {
                bool numberOk = false;
                part.toDouble(&numberOk);
                if (!numberOk)
                {
                    setError(QString("%1必须填写数值。").arg(descriptionByLine(lineNo)));
                    return false;
                }
            }
        }
    }

    bool cyclesOk = false;
    const int freezeCycles = m_editFreezeCycles->text().toInt(&cyclesOk);
    if (freezeThawEnabled() && (!cyclesOk || freezeCycles <= 0))
    {
        setError("启用冻融结冰后，冻融循环次数必须为正整数。");
        return false;
    }

    bool iceExpansionOk = false;
    const double iceExpansion = m_editIceExpansion->text().toDouble(&iceExpansionOk);
    if (freezeThawEnabled() && (!iceExpansionOk || iceExpansion < 0.0))
    {
        setError("冰线膨胀率必须为不小于零的数值。");
        return false;
    }

    bool iterOk = false;
    const int iterations = m_editIter->text().toInt(&iterOk);
    if (!iterOk || iterations <= 0)
    {
        setError("最大迭代次数必须为正整数。");
        return false;
    }

    auto isNumber = [](const QLineEdit* edit)
    {
        bool ok = false;
        edit->text().trimmed().toDouble(&ok);
        return ok;
    };
    if (m_upliftReductionRequired &&
        (!isNumber(m_editX1) || !isNumber(m_editX2) || !isNumber(m_editX3)))
    {
        setError("扬压力折减位置必须填写数值。");
        return false;
    }
    if (m_upliftReductionRequired &&
        (!isNumber(m_editAlfa1) || !isNumber(m_editAlfa2) || !isNumber(m_editAlfa3)))
    {
        setError("扬压力折减系数必须填写数值。");
        return false;
    }

    const QStringList elevationRange = m_editFreezeElevation->text().trimmed().split(
        QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (freezeThawEnabled() && elevationRange.at(0).toDouble() > elevationRange.at(1).toDouble())
    {
        setError("冻融影响高程下限不能大于上限。");
        return false;
    }
    return true;
}

bool DamAnalysisWidget::exportIfillDatFile(int mcase, const QString& cycleSectionText) const
{
    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(nullptr, "参数格式错误", error);
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    // 均质坝的坝体和基岩材料集合由求解器按当前网格层派生，避免复用旧模型结果。
    QFile::remove(inputDir.filePath(QStringLiteral("MA_ROCK.DAT")));
    QFile::remove(inputDir.filePath(QStringLiteral("MA_DAMUP.DAT")));
    QFile::remove(inputDir.filePath(QStringLiteral("MA_DAMDW.DAT")));

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    const QString inputDirAbs = ensureTrailingSep(inputDir.absolutePath());
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01FILL01"));

    QFile file(wd.filePath("IFILL.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);

    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = inputDirAbs;
        else if (isFixedFileLine(lineNo))
            value = defaultValueByLine(lineNo);
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    os << QString("%1 %2 %3")
              .arg(m_editX1->text().trimmed())
              .arg(m_editX2->text().trimmed())
              .arg(m_editX3->text().trimmed())
              << "    X1  X2  X3\n";
    os << QString("%1 %2 %3")
              .arg(m_editAlfa1->text().trimmed())
              .arg(m_editAlfa2->text().trimmed())
              .arg(m_editAlfa3->text().trimmed())
              << "    ALFA1  ALFA2  ALFA3\n";
    os << QString("%1 %2 0.000000 0.000000 0.000000 0.000000")
              .arg(m_comboAxisUp->currentData().toString(),
                   m_comboAxisDown->currentData().toString())
              << "    IXUPDW IYUPDW XAMID YAMID XBMID YBMID\n";
    os << QString("%1 %2").arg(m_comboIfRock->currentData().toString(),
                               m_comboIfIso->currentData().toString())
              << "    IFROCK IFISO\n";

    const int safeCaseCount = qMax(1, mcase);
    os << safeCaseCount << "   MCASE\n";
    for (int i = 1; i <= safeCaseCount; ++i)
        os << i << "     " << i << "\n";

    QString dynamicSection = cycleSectionText.trimmed();
    if (dynamicSection.isEmpty())
    {
        dynamicSection =
            "1    NCASE\n"
            "----------------------------------------------\n"
            "1    ICASE--------------------------------------\n"
            "0     0     1     IFIIWL_SEEP   IFIIWL_BODY  IFIIWL_FACE\n"
            "0     0     NUM_WET        IFWET\n"
            "0     0     NUM_DROP       IFDROP\n"
            "0     NCHANGE\n"
            "1  NCIRCLE\n"
            "0 0 0.0000 0.0000 0.0000 0.0000 0 0 0 0 'NONE' 'NONE' 1\n"
            "'CASE01FILL01'     DIRDYNA\n"
            "0   NUM_DYNA\n"
            "0     0     0   IFMODE   IFWAVE     IFSETT\n"
            "0.020000    0.000000    0.000000    0.000000    DT TSTA TEND WOMI_1\n"
            "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ";
    }
    dynamicSection.replace("'CASE01FILL01'", "'" + caseDirAbs + "'");
    os << dynamicSection;
    if (!dynamicSection.endsWith('\n'))
        os << "\n";

    return true;
}

void DamAnalysisWidget::setMCaseCount(int mcase)
{
    Q_UNUSED(mcase);
}

QJsonObject DamAnalysisWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["x_values"] = QJsonArray {m_editX1->text(), m_editX2->text(), m_editX3->text()};
    obj["alfa_values"] = QJsonArray {m_editAlfa1->text(), m_editAlfa2->text(), m_editAlfa3->text()};
    obj["axis_values"] = QJsonArray {m_comboAxisUp->currentData().toString(),
                                     m_comboAxisDown->currentData().toString()};
    obj["rock_values"] = QJsonArray {m_comboIfRock->currentData().toString(),
                                      m_comboIfIso->currentData().toString()};
    obj["freeze_thaw_enabled"] = freezeThawEnabled();
    return obj;
}

void DamAnalysisWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 40; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        const QString value = (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }
    const QJsonValue xVal = obj.value("x_values");
    const QJsonArray xArr = xVal.toArray();
    if (!xArr.isEmpty())
    {
        if (xArr.size() > 0)
            m_editX1->setText(xArr.at(0).toString(m_editX1->text()));
        if (xArr.size() > 1)
            m_editX2->setText(xArr.at(1).toString(m_editX2->text()));
        if (xArr.size() > 2)
            m_editX3->setText(xArr.at(2).toString(m_editX3->text()));
    }
    else if (xVal.isString())
    {
        const QStringList parts =
            xVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        m_editX1->setText(parts.value(0, m_editX1->text()));
        m_editX2->setText(parts.value(1, m_editX2->text()));
        m_editX3->setText(parts.value(2, m_editX3->text()));
    }

    const QJsonValue alfaVal = obj.value("alfa_values");
    const QJsonArray alfaArr = alfaVal.toArray();
    if (!alfaArr.isEmpty())
    {
        if (alfaArr.size() > 0)
            m_editAlfa1->setText(alfaArr.at(0).toString(m_editAlfa1->text()));
        if (alfaArr.size() > 1)
            m_editAlfa2->setText(alfaArr.at(1).toString(m_editAlfa2->text()));
        if (alfaArr.size() > 2)
            m_editAlfa3->setText(alfaArr.at(2).toString(m_editAlfa3->text()));
    }
    else if (alfaVal.isString())
    {
        const QStringList parts =
            alfaVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        m_editAlfa1->setText(parts.value(0, m_editAlfa1->text()));
        m_editAlfa2->setText(parts.value(1, m_editAlfa2->text()));
        m_editAlfa3->setText(parts.value(2, m_editAlfa3->text()));
    }

    const QJsonValue axisVal = obj.value("axis_values");
    const QJsonArray axisArr = axisVal.toArray();
    if (!axisArr.isEmpty())
    {
        const int upIdx = m_comboAxisUp->findData(axisArr.size() > 0 ? axisArr.at(0).toString() : "");
        const int downIdx =
            m_comboAxisDown->findData(axisArr.size() > 1 ? axisArr.at(1).toString() : "");
        if (upIdx >= 0)
            m_comboAxisUp->setCurrentIndex(upIdx);
        if (downIdx >= 0)
            m_comboAxisDown->setCurrentIndex(downIdx);
    }
    else if (axisVal.isString())
    {
        const QStringList parts =
            axisVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const int upIdx = m_comboAxisUp->findData(parts.value(0));
        const int downIdx = m_comboAxisDown->findData(parts.value(1));
        if (upIdx >= 0)
            m_comboAxisUp->setCurrentIndex(upIdx);
        if (downIdx >= 0)
            m_comboAxisDown->setCurrentIndex(downIdx);
    }

    const QJsonValue rockVal = obj.value("rock_values");
    const QJsonArray rockArr = rockVal.toArray();
    if (!rockArr.isEmpty())
    {
        const int rockIdx = m_comboIfRock->findData(rockArr.size() > 0 ? rockArr.at(0).toString() : "");
        const int isoIdx = m_comboIfIso->findData(rockArr.size() > 1 ? rockArr.at(1).toString() : "");
        if (rockIdx >= 0)
            m_comboIfRock->setCurrentIndex(rockIdx);
        if (isoIdx >= 0)
            m_comboIfIso->setCurrentIndex(isoIdx);
    }
    else if (rockVal.isString())
    {
        const QStringList parts =
            rockVal.toString().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const int rockIdx = m_comboIfRock->findData(parts.value(0));
        const int isoIdx = m_comboIfIso->findData(parts.value(1));
        if (rockIdx >= 0)
            m_comboIfRock->setCurrentIndex(rockIdx);
        if (isoIdx >= 0)
            m_comboIfIso->setCurrentIndex(isoIdx);
    }

    const bool freezeEnabled = obj.contains("freeze_thaw_enabled")
                                   ? obj.value("freeze_thaw_enabled").toBool(false)
                                   : m_editFreezeCycles->text().toInt() > 0;
    m_groupFreezeThaw->setChecked(freezeEnabled);
    updateAnalysisModeVisibility();
    updateFreezeThawOptionState();
    updateUpliftOptionState();
}

void DamAnalysisWidget::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    emit computeRequested();
}

void DamAnalysisWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止") : QStringLiteral("计算"));
}
