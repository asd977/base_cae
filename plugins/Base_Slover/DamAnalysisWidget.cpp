#include "DamAnalysisWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>
#include <QWidget>

#include "ui_DamAnalysisWidget.h"

DamAnalysisWidget::DamAnalysisWidget(QWidget* parent)
    : BasePage("DamAnalysis"), ui(new Ui::DamAnalysisWidget)
{
    ui->setupUi(this);
    initValueEditors();
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
    m_editX = ui->editX;
    m_editAlfa = ui->editAlfa;
    m_editAxis = ui->editAxis;
    m_editRock = ui->editRock;
    m_editMCase = ui->editMCase;
    m_summaryLabel = ui->summaryLabel;
    m_btnValidation = ui->btnValidation;

    m_lineEditors[1] = ui->line1Value;
    m_lineEditors[2] = ui->line2Value;
    m_lineEditors[3] = ui->line3Value;
    m_lineEditors[4] = ui->line4Value;
    m_lineEditors[5] = ui->line5Value;
    m_lineEditors[6] = ui->line6Value;
    m_lineEditors[7] = ui->line7Value;
    m_lineEditors[8] = ui->line8Value;
    m_lineEditors[9] = ui->line9Value;
    m_lineEditors[10] = ui->comboLine10;
    m_lineEditors[11] = ui->line11Value;
    m_lineEditors[12] = ui->line12Value;
    m_lineEditors[13] = ui->line13Value;
    m_lineEditors[14] = ui->line14Value;
    m_lineEditors[15] = ui->comboLine15;
    m_lineEditors[16] = ui->comboLine16;
    m_lineEditors[17] = ui->editLine17;
    m_lineEditors[18] = ui->editLine18;
    m_lineEditors[19] = ui->comboLine19;
    m_lineEditors[20] = ui->editLine20;
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

    ui->comboLine10->setItemData(0, "1");
    ui->comboLine10->setItemData(1, "2");
    ui->comboLine10->setItemData(2, "3");
    ui->comboLine10->setItemData(3, "4");

    ui->comboLine15->setItemData(0, "0");
    ui->comboLine15->setItemData(1, "1");

    ui->comboLine16->setItemData(0, "0");
    ui->comboLine16->setItemData(1, "1");

    ui->comboLine19->setItemData(0, "1");
    ui->comboLine19->setItemData(1, "2");
    ui->comboLine19->setItemData(2, "3");

    ui->comboLine29->setItemData(0, "0");
    ui->comboLine29->setItemData(1, "1");

    for (auto it = m_lineEditors.begin(); it != m_lineEditors.end(); ++it)
    {
        if (auto* combo = qobject_cast<QComboBox*>(it.value()))
            connect(combo, &QComboBox::currentTextChanged, this, [this]() { updateConfigSummary(); });
        else if (auto* edit = qobject_cast<QLineEdit*>(it.value()))
            connect(edit, &QLineEdit::textChanged, this, [this]() { updateConfigSummary(); });
    }

    connect(m_btnValidation, &QPushButton::clicked, this, [this]() {
        QString error;
        if (validateInputs(&error))
        {
            QMessageBox::information(this, "校验通过", "IFILL.DAT 参数格式校验通过。");
            return;
        }
        QMessageBox::warning(this, "校验失败", error);
    });

    updateConfigSummary();
}

QString DamAnalysisWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString DamAnalysisWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {
        "F:\\FEMCLASS\\Z03_JUNZ3D\\PREP\\", "FA_DAMUP.DAT", "FA_DAMDW.DAT", "FA_DAMBASE.DAT",
        "FA_DAMMID.DAT", "MA_ROCK.DAT", "MA_DAMUP.DAT", "MA_DAMDW.DAT", "MA_CORE.DAT", "1",
        "MA_PLATE.DAT", "JDTRACE.DAT", "KETRACE.DAT", "TDTRACE.DAT", "0", "0", "0.05", "0.01",
        "2", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10"};
    return defaults.value(lineNo - 1);
}

QString DamAnalysisWidget::descriptionByLine(int lineNo) const
{
    static const QStringList descriptions = {
        "计算默认文件目录", "上游迎水面集文件", "下游迎水面集文件", "心墙建基面文件",
        "心墙中心面面集文件（面板坝不需要）", "基岩材料集（层号为1）", "上游材料集（面板坝不需要）",
        "下游材料集", "心墙材料集（面板坝不需要）",
        "坝型：1面板堆石坝 / 2心墙堆石坝 / 3沥青心墙堆石坝 / 4均质坝",
        "面板材料集文件", "特征节点集文件", "特征单元集文件", "特征时刻文件",
        "阻尼：0系统阻尼 / 1分材料阻尼", "瑞雷阻尼：0否 / 1是", "质量阻尼系数 ALFAP",
        "刚度阻尼系数 BETAP", "计算类型：1静力 / 2动力 / 3静动力", "METHOD_ITER ITER",
        "蠕变计算时间步长 DT_CREEP", "水平最大地震加速度 AHMAX", "竖向最大地震加速度 AVMAX",
        "反应谱特征周期 TG1", "反应谱特征周期 TG2", "反应谱 BETAMAX", "反应谱指数 EXPN",
        "地震震级 QUAKE_LEVEL", "是否计算特征值特征向量：0否 / 1是", "特征值数"};
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
    static const QSet<int> fixedLines = {1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 12, 13, 14};
    return fixedLines.contains(lineNo);
}

bool DamAnalysisWidget::validateInputs(QString* errorMessage) const
{
    auto setError = [errorMessage](const QString& message) {
        if (errorMessage)
            *errorMessage = message;
    };

    static const QSet<int> oneTokenLines = {10, 15, 16, 17, 18, 19, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30};
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        const QString text = normalizeEnumValue(lineValue(lineNo));
        if (text.trimmed().isEmpty())
        {
            setError(QString("第 %1 行为空，请填写：%2").arg(lineNo).arg(descriptionByLine(lineNo)));
            return false;
        }
        if (lineNo == 20)
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 2)
            {
                setError("第 20 行需包含两个数值：METHOD_ITER 和 ITER，例如：4 10。");
                return false;
            }
        }
        if (oneTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (parts.size() != 1)
            {
                setError(QString("第 %1 行应为单一值，当前输入：%2").arg(lineNo).arg(text));
                return false;
            }
        }
    }

    auto tokenCount = [](const QString& value) {
        return value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).size();
    };
    if (tokenCount(m_editX->text()) != 3)
    {
        setError("X1 X2 X3 需要 3 个值。");
        return false;
    }
    if (tokenCount(m_editAlfa->text()) != 3)
    {
        setError("α1 α2 α3 需要 3 个值。");
        return false;
    }
    if (tokenCount(m_editAxis->text()) != 6)
    {
        setError("IXUPDW XAMID YAMID XBMID YBMID 配置需 6 个值。");
        return false;
    }
    if (tokenCount(m_editRock->text()) != 2)
    {
        setError("IFROCK IFISO 需要 2 个值。");
        return false;
    }

    return true;
}

void DamAnalysisWidget::updateConfigSummary()
{
    const QString fillType = normalizeEnumValue(lineValue(10));
    const QString calcType = normalizeEnumValue(lineValue(19));
    const QString dampingType = normalizeEnumValue(lineValue(15));
    const QString rayleigh = normalizeEnumValue(lineValue(16));

    auto mapName = [](const QString& code, const QMap<QString, QString>& map) { return map.value(code, QString("未知(%1)").arg(code)); };

    const QMap<QString, QString> fillMap = {{"1", "面板堆石坝"}, {"2", "心墙堆石坝"}, {"3", "沥青心墙堆石坝"}, {"4", "均质坝"}};
    const QMap<QString, QString> calcMap = {{"1", "静力"}, {"2", "动力"}, {"3", "静动力全算"}};
    const QMap<QString, QString> dampingMap = {{"0", "系统阻尼"}, {"1", "分材料阻尼"}};
    const QMap<QString, QString> rayleighMap = {{"0", "非瑞雷"}, {"1", "瑞雷"}};

    m_summaryLabel->setText(QString("坝型：%1；计算类型：%2；阻尼：%3；瑞雷：%4；MCASE：%5")
                                .arg(mapName(fillType, fillMap))
                                .arg(mapName(calcType, calcMap))
                                .arg(mapName(dampingType, dampingMap))
                                .arg(mapName(rayleigh, rayleighMap))
                                .arg(m_editMCase ? m_editMCase->text() : "1"));
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
    const QString projectDirAbs = QDir(projectDir).absolutePath();

    QDir wd(projectDirAbs);
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    auto ensureTrailingSep = [](const QString& path)
    {
        QString p = QDir::toNativeSeparators(path);
        if (!p.endsWith(QDir::separator()))
            p += QDir::separator();
        return p;
    };

    const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01FILL01"));

    QFile file(wd.filePath("IFILL.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);

    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = prepDirAbs;
        else if (isFixedFileLine(lineNo))
            value = defaultValueByLine(lineNo);
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    os << m_editX->text().trimmed() << "    X1  X2  X3\n";
    os << m_editAlfa->text().trimmed() << "    ALFA1  ALFA2  ALFA3\n";
    os << m_editAxis->text().trimmed() << "    IXUPDW XAMID YAMID XBMID YBMID\n";
    os << m_editRock->text().trimmed() << "    IFROCK IFISO\n";

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
    if (m_editMCase)
        m_editMCase->setText(QString::number(qMax(1, mcase)));
    updateConfigSummary();
}

QJsonObject DamAnalysisWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["x_values"] = m_editX->text();
    obj["alfa_values"] = m_editAlfa->text();
    obj["axis_values"] = m_editAxis->text();
    obj["rock_values"] = m_editRock->text();
    return obj;
}

void DamAnalysisWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
            continue;
        const QString value = (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }
    m_editX->setText(obj.value("x_values").toString(m_editX->text()));
    m_editAlfa->setText(obj.value("alfa_values").toString(m_editAlfa->text()));
    m_editAxis->setText(obj.value("axis_values").toString(m_editAxis->text()));
    m_editRock->setText(obj.value("rock_values").toString(m_editRock->text()));
    updateConfigSummary();
}

void DamAnalysisWidget::onComputeClicked()
{
    emit computeRequested();
}
