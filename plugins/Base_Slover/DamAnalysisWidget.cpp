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
    m_comboMethodIter = ui->comboMethodIter;
    m_editIter = ui->editIter;
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

    ui->comboLine15->setItemText(0, QStringLiteral("统一使用系统阻尼"));
    ui->comboLine15->setItemText(1, QStringLiteral("按材料分别设置阻尼"));
    ui->comboLine16->setItemText(0, QStringLiteral("不使用 Rayleigh 阻尼"));
    ui->comboLine16->setItemText(1, QStringLiteral("使用 Rayleigh 阻尼"));
    ui->comboLine19->setItemText(0, QStringLiteral("仅静力分析"));
    ui->comboLine19->setItemText(1, QStringLiteral("仅动力分析（需时程文件）"));
    ui->comboLine19->setItemText(2, QStringLiteral("静力后继续动力分析"));
    ui->comboLine29->setItemText(0, QStringLiteral("不计算特征值"));
    ui->comboLine29->setItemText(1, QStringLiteral("计算特征值"));

    m_comboMethodIter->setItemText(0, QStringLiteral("初应变法（常刚度）"));
    m_comboMethodIter->setItemText(1, QStringLiteral("初应力法（常刚度）"));
    m_comboMethodIter->setItemText(2, QStringLiteral("变刚度法（切线刚度）"));
    m_comboMethodIter->setItemText(3, QStringLiteral("中点增量法（土坝填筑）"));
    m_comboAxisUp->setItemText(0, QStringLiteral("X 轴"));
    m_comboAxisUp->setItemText(1, QStringLiteral("Y 轴"));
    m_comboAxisUp->setItemText(2, QStringLiteral("Z 轴"));
    m_comboAxisDown->setItemText(0, QStringLiteral("X 轴"));
    m_comboAxisDown->setItemText(1, QStringLiteral("Y 轴"));
    m_comboAxisDown->setItemText(2, QStringLiteral("Z 轴"));
    m_comboIfRock->setItemText(0, QStringLiteral("不重复计算（已有岩体自重结果）"));
    m_comboIfRock->setItemText(1, QStringLiteral("本次计算岩体自重"));
    m_comboIfIso->setItemText(0, QStringLiteral("按建模材料计算"));
    m_comboIfIso->setItemText(1, QStringLiteral("按指定均质材料计算"));

    ui->comboLine19->setToolTip(
        QStringLiteral("仅动力分析需要 JDTRACE、KETRACE、TDTRACE 等动力输入文件。"));
    m_comboMethodIter->setToolTip(
        QStringLiteral("界面显示算法名称，导出文件仍使用对应数字代码。"));

}

QString DamAnalysisWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString DamAnalysisWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {
        "F:\\FEMCLASS\\Z03_JUNZ3D\\PREP\\", "FA_DAMUP.DAT", "FA_DAMDW.DAT", "FA_DAMBASE.DAT",
        "NONE", "MA_ROCK.DAT", "MA_DAMUP.DAT", "MA_DAMDW.DAT", "MA_CORE.DAT", "4",
        "MA_PLATE.DAT", "JDTRACE.DAT", "KETRACE.DAT", "TDTRACE.DAT", "0", "0", "0.05", "0.01",
        "2", "4   10", "0.000", "1.962", "1.308", "0.1", "0.2", "2.0", "0.9", "7.0", "0",
        "10"};
    return defaults.value(lineNo - 1);
}

QString DamAnalysisWidget::descriptionByLine(int lineNo) const
{
    static const QStringList descriptions = {
        "计算默认文件目录", "上游水头边界集文件", "下游水头边界集文件", "心墙建基面文件",
        "坝体中心面文件（已停用，占位参数）", "基岩材料集（层号为1）", "上游材料集（面板坝不需要）",
        "下游材料集", "心墙材料集（面板坝不需要）",
        "坝型：1面板堆石坝 / 2心墙堆石坝 / 3沥青心墙堆石坝 / 4均质坝",
        "面板材料集文件", "特征节点集文件", "特征单元集文件", "特征时刻文件",
        "阻尼：0系统阻尼 / 1分材料阻尼", "瑞雷阻尼：0否 / 1是", "质量阻尼系数",
        "刚度阻尼系数", "计算类型：1静力 / 2动力 / 3静动力", "迭代方法与迭代次数",
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
    if (lineNo == 20)
        return QString("%1 %2").arg(m_comboMethodIter->currentData().toString(),
                                    m_editIter->text().trimmed());

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
            value.trimmed().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
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
    static const QSet<int> fixedLines = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
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
        if (oneTokenLines.contains(lineNo))
        {
            const QStringList parts = text.split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
            if (parts.size() != 1)
            {
                setError(QString("第 %1 行应为单一值，当前输入：%2").arg(lineNo).arg(text));
                return false;
            }
        }
    }

    if (m_editIter->text().trimmed().isEmpty())
    {
        setError("迭代次数不能为空。");
        return false;
    }
    if (m_editX1->text().trimmed().isEmpty() || m_editX2->text().trimmed().isEmpty() ||
        m_editX3->text().trimmed().isEmpty())
    {
        setError("请填写 X1、X2、X3。");
        return false;
    }
    if (m_editAlfa1->text().trimmed().isEmpty() || m_editAlfa2->text().trimmed().isEmpty() ||
        m_editAlfa3->text().trimmed().isEmpty())
    {
        setError("请填写 α1、α2、α3。");
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
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["x_values"] = QJsonArray {m_editX1->text(), m_editX2->text(), m_editX3->text()};
    obj["alfa_values"] = QJsonArray {m_editAlfa1->text(), m_editAlfa2->text(), m_editAlfa3->text()};
    obj["axis_values"] = QJsonArray {m_comboAxisUp->currentData().toString(),
                                     m_comboAxisDown->currentData().toString()};
    obj["rock_values"] = QJsonArray {m_comboIfRock->currentData().toString(),
                                     m_comboIfIso->currentData().toString()};
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
            xVal.toString().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
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
            alfaVal.toString().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
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
            axisVal.toString().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
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
            rockVal.toString().split(QRegularExpression("\\s+"), QString::SkipEmptyParts);
        const int rockIdx = m_comboIfRock->findData(parts.value(0));
        const int isoIdx = m_comboIfIso->findData(parts.value(1));
        if (rockIdx >= 0)
            m_comboIfRock->setCurrentIndex(rockIdx);
        if (isoIdx >= 0)
            m_comboIfIso->setCurrentIndex(isoIdx);
    }
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
