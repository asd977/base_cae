#include "DamAnalysisWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
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
    m_formLayout = new QFormLayout();
    m_formLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    auto addCombo = [this](int lineNo, const QMap<QString, QString>& options) {
        auto* combo = new QComboBox(this);
        for (auto it = options.begin(); it != options.end(); ++it)
            combo->addItem(it.value(), it.key());
        const QString def = defaultValueByLine(lineNo).trimmed();
        const int idx = combo->findData(def);
        if (idx >= 0)
            combo->setCurrentIndex(idx);
        connect(combo, &QComboBox::currentTextChanged, this, [this]() { updateConfigSummary(); });
        m_lineEditors[lineNo] = combo;
    };

    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        if (isFixedFileLine(lineNo))
        {
            auto* fixedLabel = new QLabel(defaultValueByLine(lineNo), this);
            fixedLabel->setStyleSheet("QLabel { color: #606060; }");
            fixedLabel->setToolTip("该文件名按规范固定，不在界面中编辑。");
            m_lineEditors[lineNo] = fixedLabel;
        }
        else if (lineNo == 10)
        {
            addCombo(lineNo, {{"1", "1 面板堆石坝"}, {"2", "2 心墙堆石坝"}, {"3", "3 沥青心墙堆石坝"}, {"4", "4 均质坝"}});
        }
        else if (lineNo == 15)
        {
            addCombo(lineNo, {{"0", "0 系统阻尼"}, {"1", "1 分材料阻尼"}});
        }
        else if (lineNo == 16)
        {
            addCombo(lineNo, {{"0", "0 非瑞雷阻尼"}, {"1", "1 瑞雷阻尼"}});
        }
        else if (lineNo == 19)
        {
            addCombo(lineNo, {{"1", "1 静力计算"}, {"2", "2 动力计算"}, {"3", "3 静动力全部计算"}});
        }
        else if (lineNo == 29)
        {
            addCombo(lineNo, {{"0", "0 不计算特征值特征向量"}, {"1", "1 计算特征值特征向量"}});
        }
        else
        {
            auto* edit = new QLineEdit(defaultValueByLine(lineNo), this);
            edit->setPlaceholderText(descriptionByLine(lineNo));
            connect(edit, &QLineEdit::textChanged, this, [this]() { updateConfigSummary(); });
            m_lineEditors[lineNo] = edit;
        }

        m_formLayout->addRow(QString("4.3.1 第%1行：%2").arg(lineNo).arg(descriptionByLine(lineNo)), m_lineEditors.value(lineNo));
    }

    m_editX = new QLineEdit("0.000000  0.000000  0.000000", this);
    m_editAlfa = new QLineEdit("0.000000  0.000000  0.000000", this);
    m_editAxis = new QLineEdit("1  3  0.000000  0.000000  0.000000  0.000000", this);
    m_editRock = new QLineEdit("1  0", this);
    m_editMCase = new QLineEdit("1", this);
    m_editMCase->setReadOnly(true);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setWordWrap(true);
    m_summaryLabel->setStyleSheet("QLabel { color: #404040; }");

    m_btnValidation = new QPushButton("校验配置", this);
    connect(m_btnValidation, &QPushButton::clicked, this, [this]() {
        QString error;
        if (validateInputs(&error))
        {
            QMessageBox::information(this, "校验通过", "IFILL.DAT 参数格式校验通过。");
            return;
        }
        QMessageBox::warning(this, "校验失败", error);
    });

    m_formLayout->addRow("4.3.4 MCASE（自动统计填筑-蓄水节点数）", m_editMCase);
    m_formLayout->addRow("4.3.2(1) X1 X2 X3", m_editX);
    m_formLayout->addRow("4.3.2(2) α1 α2 α3", m_editAlfa);
    m_formLayout->addRow("4.3.2(3) IXUPDW XAMID YAMID XBMID YBMID", m_editAxis);
    m_formLayout->addRow("4.3.3 IFROCK IFISO", m_editRock);
    m_formLayout->addRow("当前配置摘要", m_summaryLabel);
    m_formLayout->addRow("配置检查", m_btnValidation);

    connect(m_editRock, &QLineEdit::textChanged, this, [this]() { updateConfigSummary(); });

    ui->verticalLayout->insertLayout(2, m_formLayout);
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
