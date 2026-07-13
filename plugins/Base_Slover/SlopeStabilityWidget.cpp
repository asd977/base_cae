#include "SlopeStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QJsonArray>
#include <QLineEdit>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QStringList>

#include "ui_SlopeStabilityWidget.h"

namespace
{
QStringList splitGravityComponents(const QString& value)
{
    const QStringList defaults = {"0.0", "-9.81", "0.0"};
    const QStringList parts = value.simplified().split(QLatin1Char(' '), QString::SkipEmptyParts);

    QStringList result;
    for (int i = 0; i < 3; ++i)
        result.append(parts.value(i, defaults.value(i)));
    return result;
}
}

SlopeStabilityWidget::SlopeStabilityWidget(QWidget* parent)
    : BasePage("SlopeStabilityAnalysis"), ui(new Ui::SlopeStabilityWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);

    m_lineEditors[1] = nullptr;
    m_lineEditors[3] = ui->line3Combo;
    m_lineEditors[4] = nullptr;
    m_lineEditors[5] = ui->line5Combo;
    m_lineEditors[6] = ui->line6Edit;
    m_lineEditors[7] = ui->line7Edit;
    m_lineEditors[8] = ui->line8Combo;
    m_lineEditors[9] = nullptr;
    m_lineEditors[10] = nullptr;
    m_lineEditors[11] = nullptr;
    m_lineEditors[12] = nullptr;
    m_lineEditors[13] = nullptr;
    m_lineEditors[14] = nullptr;
    m_lineEditors[15] = ui->line15Combo;
    m_lineEditors[16] = ui->line16Combo;
    m_lineEditors[17] = ui->line17Edit;
    m_lineEditors[18] = ui->line18Edit;
    m_lineEditors[19] = ui->line19Combo;
    m_lineEditors[20] = nullptr;

    ui->line3Combo->setItemData(0, "0");
    ui->line3Combo->setItemData(1, "1");
    ui->line3Combo->setItemData(2, "2");
    ui->line3Combo->setItemData(3, "3");
    ui->line3Combo->setItemData(4, "4");
    ui->line5Combo->setItemData(0, "1");
    ui->line5Combo->setItemData(1, "2");
    ui->line5Combo->setItemData(2, "3");
    ui->line8Combo->setItemData(0, "1");
    ui->line8Combo->setItemData(1, "2");
    ui->line15Combo->setItemData(0, "0");
    ui->line15Combo->setItemData(1, "1");
    ui->line16Combo->setItemData(0, "0");
    ui->line16Combo->setItemData(1, "1");
    ui->line19Combo->setItemData(0, "1");
    ui->line19Combo->setItemData(1, "2");
    ui->line19Combo->setItemData(2, "3");
    ui->ifModeCombo->setItemData(0, "0");
    ui->ifModeCombo->setItemData(1, "1");
    ui->ifWaveCombo->setItemData(0, "0");
    ui->ifWaveCombo->setItemData(1, "1");
    ui->ifModeCombo->setCurrentIndex(0);
    ui->ifWaveCombo->setCurrentIndex(0);

    initStrengthReserveTable();
    connect(ui->btnCompute, &QPushButton::clicked, this, &SlopeStabilityWidget::onComputeClicked);
    connect(ui->addReserveRowBtn, &QPushButton::clicked, this, [this]()
    {
        const int row = ui->strengthReserveTable->rowCount();
        ui->strengthReserveTable->insertRow(row);
        ui->strengthReserveTable->setItem(row, 0, new QTableWidgetItem("1.0000"));
        setStrengthReserveOutputCombo(row, QStringLiteral("1.0000"));
    });
    connect(ui->removeReserveRowBtn, &QPushButton::clicked, this, [this]()
    {
        const int row = ui->strengthReserveTable->currentRow();
        if (row >= 0)
            ui->strengthReserveTable->removeRow(row);
    });
}

SlopeStabilityWidget::~SlopeStabilityWidget()
{
    delete ui;
}

void SlopeStabilityWidget::beforeLoad()
{
}

void SlopeStabilityWidget::beforeUnload()
{
}

QString SlopeStabilityWidget::quoteValue(const QString& value) const
{
    return "'" + value.trimmed() + "'";
}

QString SlopeStabilityWidget::lineValue(int lineNo) const
{
    if (lineNo == 4)
        return gravityLineValue();

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
        return combo->currentData().toString();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    return defaultValueByLine(lineNo);
}

void SlopeStabilityWidget::setLineValue(int lineNo, const QString& value)
{
    if (lineNo == 4)
    {
        setGravityLineValue(value);
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
        edit->setText(value);
}

QString SlopeStabilityWidget::gravityLineValue() const
{
    const QStringList defaults = splitGravityComponents(defaultValueByLine(4));
    const QStringList values = {
        ui->gravityXEdit->text().trimmed().isEmpty() ? defaults.value(0) : ui->gravityXEdit->text().trimmed(),
        ui->gravityYEdit->text().trimmed().isEmpty() ? defaults.value(1) : ui->gravityYEdit->text().trimmed(),
        ui->gravityZEdit->text().trimmed().isEmpty() ? defaults.value(2) : ui->gravityZEdit->text().trimmed()
    };
    return values.join("   ");
}

void SlopeStabilityWidget::setGravityLineValue(const QString& value)
{
    const QStringList values = splitGravityComponents(value);
    ui->gravityXEdit->setText(values.value(0));
    ui->gravityYEdit->setText(values.value(1));
    ui->gravityZEdit->setText(values.value(2));
}

QString SlopeStabilityWidget::defaultValueByLine(int lineNo) const
{
    static const QStringList defaults = {"F:\\FEMCLASS\\Z08_SLOP3D\\PREP\\", "MACB.DAT", "3",
                                         "0.0   -9.81   0.0", "1", "500", "0.0001", "1", "IIWL.DAT",
                                         "--------------", "--------------", "JDTRACE.DAT", "KETRACE.DAT",
                                         "TDTRACE.DAT", "0", "0", "0.05", "0.0", "1", "MACHAN.DAT"};
    return defaults.value(lineNo - 1);
}

namespace
{
constexpr char kAccXFileName[] = "WAVE-AX.DAT";
constexpr char kAccYFileName[] = "WAVE-AY.DAT";
constexpr char kAccZFileName[] = "WAVE-AZ.DAT";
}

void SlopeStabilityWidget::initStrengthReserveTable()
{
    ui->strengthReserveTable->setColumnCount(2);
    ui->strengthReserveTable->setHorizontalHeaderLabels({"强储倍数", "是否输出"});
    ui->strengthReserveTable->setRowCount(12);

    const QStringList factors = {"1.0000", "1.2000", "1.4000", "1.5000", "1.5500", "1.6000",
                                 "1.6200", "1.6400", "1.6600", "1.6800", "1.7000", "1.7200"};
    const QStringList ifpr = {"1.0000", "1.0000", "1.0000", "1.0000", "1.0000", "1.0000",
                              "1.0000", "1.0000", "1.0000", "1.0000", "1.0000", "1.0000"};
    for (int row = 0; row < factors.size(); ++row)
    {
        ui->strengthReserveTable->setItem(row, 0, new QTableWidgetItem(factors.at(row)));
        setStrengthReserveOutputCombo(row, ifpr.at(row));
    }
    ui->strengthReserveTable->horizontalHeader()->setStretchLastSection(true);
}

void SlopeStabilityWidget::setStrengthReserveOutputCombo(int row, const QString& value)
{
    if (row < 0 || row >= ui->strengthReserveTable->rowCount())
        return;

    auto* combo = new QComboBox(ui->strengthReserveTable);
    combo->addItem(QStringLiteral("0 - 不输出该储备系数结果"), QStringLiteral("0.0000"));
    combo->addItem(QStringLiteral("1 - 输出该储备系数结果"), QStringLiteral("1.0000"));
    const QString normalized = value.trimmed().startsWith(QLatin1Char('0'))
                                   ? QStringLiteral("0.0000")
                                   : QStringLiteral("1.0000");
    const int index = combo->findData(normalized);
    combo->setCurrentIndex(index >= 0 ? index : 1);
    ui->strengthReserveTable->setCellWidget(row, 1, combo);
}

QString SlopeStabilityWidget::strengthReserveOutputValue(int row) const
{
    if (row < 0 || row >= ui->strengthReserveTable->rowCount())
        return QStringLiteral("1.0000");
    if (auto* combo = qobject_cast<QComboBox*>(ui->strengthReserveTable->cellWidget(row, 1)))
        return combo->currentData().toString();
    const auto* item = ui->strengthReserveTable->item(row, 1);
    const QString value = item ? item->text().trimmed() : QString();
    return value.isEmpty() ? QStringLiteral("1.0000") : value;
}

bool SlopeStabilityWidget::exportIslopDatFile() const
{
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

    QFile file(wd.filePath("ISLOP.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const QString prepDirAbs = ensureTrailingSep(wd.filePath("PREP"));
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01"));

    for (int lineNo = 1; lineNo <= 20; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = prepDirAbs;
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    int nsrf = 0;
    for (int row = 0; row < ui->strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        if (factorItem && !factorItem->text().trimmed().isEmpty())
            ++nsrf;
    }
    os << nsrf << "    NSRF\n";
    for (int row = 0; row < ui->strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        if (!factorItem)
            continue;
        const QString factor = factorItem->text().trimmed();
        if (factor.isEmpty())
            continue;

        const QString ifpr = strengthReserveOutputValue(row);

        bool factorOk = false;
        bool ifprOk = false;
        const double factorVal = factor.toDouble(&factorOk);
        const double ifprVal = ifpr.toDouble(&ifprOk);

        const QString formattedFactor =
            factorOk ? QString::asprintf("%11.4f", factorVal) : factor.rightJustified(11, ' ');
        const QString formattedIfpr =
            ifprOk ? QString::asprintf("%11.4f", ifprVal) : ifpr.rightJustified(11, ' ');
        os << formattedFactor << " " << formattedIfpr <<" "<< QString("%1").arg(row + 1)
           << " SRF    IFPR   II\n";
    }

    os << quoteValue(caseDirAbs) << "\n";
    os << ui->dtEdit->text().trimmed() << " " << ui->tstaEdit->text().trimmed() << " "
       << ui->tendEdit->text().trimmed() << " " << ui->ifModeCombo->currentData().toString()
       << " " << ui->ifWaveCombo->currentData().toString() << "    DT TSTA TEND IFMODE IFWAVE\n";
    os << quoteValue(QString::fromLatin1(kAccXFileName)) << "  "
       << quoteValue(QString::fromLatin1(kAccYFileName)) << "  "
       << quoteValue(QString::fromLatin1(kAccZFileName)) << "    ACCEX ACCEY ACCEZ\n";

    exportTdtraceDatFile();
    return true;
}

bool SlopeStabilityWidget::exportTdtraceDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(QDir(projectDir).absolutePath());
    if (!wd.exists("PREP") && !wd.mkpath("PREP"))
        return false;

    QFile file(wd.filePath("PREP/TDTRACE.DAT"));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}

QJsonObject SlopeStabilityWidget::saveJson()
{
    QJsonObject obj;
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 20; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    QJsonArray reserve;
    for (int row = 0; row < ui->strengthReserveTable->rowCount(); ++row)
    {
        QJsonObject one;
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        one["factor"] = factorItem ? factorItem->text().trimmed() : "";
        one["ifpr"] = strengthReserveOutputValue(row);
        reserve.append(one);
    }
    obj["strength_reserve"] = reserve;
    obj["dt"] = ui->dtEdit->text().trimmed();
    obj["tsta"] = ui->tstaEdit->text().trimmed();
    obj["tend"] = ui->tendEdit->text().trimmed();
    obj["ifmode"] = ui->ifModeCombo->currentData().toString();
    obj["ifwave"] = ui->ifWaveCombo->currentData().toString();
    obj["acc_x"] = QString::fromLatin1(kAccXFileName);
    obj["acc_y"] = QString::fromLatin1(kAccYFileName);
    obj["acc_z"] = QString::fromLatin1(kAccZFileName);
    return obj;
}

void SlopeStabilityWidget::loadJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 20; ++lineNo)
    {
        if (lineNo == 1)
            continue;
        const QString value =
            (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }

    const QJsonArray reserve = obj.value("strength_reserve").toArray();
    const int rowCount = qMax(ui->strengthReserveTable->rowCount(), reserve.size());
    ui->strengthReserveTable->setRowCount(rowCount);
    for (int row = 0; row < rowCount; ++row)
    {
        QString factor;
        QString ifpr;
        if (row < reserve.size())
        {
            const QJsonObject one = reserve.at(row).toObject();
            factor = one.value("factor").toString();
            ifpr = one.value("ifpr").toString();
        }
        ui->strengthReserveTable->setItem(
            row, 0, new QTableWidgetItem(factor.isEmpty() ? QString() : factor));
        setStrengthReserveOutputCombo(row, ifpr.isEmpty() ? QStringLiteral("1.0000") : ifpr);
    }

    ui->dtEdit->setText(obj.value("dt").toString(ui->dtEdit->text()));
    ui->tstaEdit->setText(obj.value("tsta").toString(ui->tstaEdit->text()));
    ui->tendEdit->setText(obj.value("tend").toString(ui->tendEdit->text()));
    const QString ifMode = obj.value("ifmode").toString(ui->ifModeCombo->currentData().toString());
    const QString ifWave = obj.value("ifwave").toString(ui->ifWaveCombo->currentData().toString());
    const int ifModeIndex = ui->ifModeCombo->findData(ifMode);
    const int ifWaveIndex = ui->ifWaveCombo->findData(ifWave);
    ui->ifModeCombo->setCurrentIndex(ifModeIndex >= 0 ? ifModeIndex : 1);
    ui->ifWaveCombo->setCurrentIndex(ifWaveIndex >= 0 ? ifWaveIndex : 1);
}

void SlopeStabilityWidget::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    emit computeRequested();
}

void SlopeStabilityWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止") : QStringLiteral("计算"));
}
