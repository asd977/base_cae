#include "SlopeStabilityWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QList>
#include <QStringConverter>
#include <QMessageBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QStringList>
#include <QVBoxLayout>

#include "ui_SlopeStabilityWidget.h"

#include "SolverUiLayoutHelper.h"
namespace
{
QStringList splitGravityComponents(const QString& value)
{
    const QStringList defaults = {"0.0", "-9.81", "0.0"};
    const QStringList parts = value.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);

    QStringList result;
    for (int i = 0; i < 3; ++i)
        result.append(parts.value(i, defaults.value(i)));
    return result;
}

struct FormRowWidgets
{
    QWidget* label = nullptr;
    QWidget* field = nullptr;
};

FormRowWidgets takeFormRow(QFormLayout* layout, QWidget* field)
{
    FormRowWidgets result;
    if (!layout || !field)
        return result;

    QFormLayout::TakeRowResult row = layout->takeRow(field);
    if (row.labelItem)
    {
        result.label = row.labelItem->widget();
        delete row.labelItem;
    }
    if (row.fieldItem)
    {
        result.field = row.fieldItem->widget();
        delete row.fieldItem;
    }
    return result;
}

void appendFormRow(QFormLayout* layout, const FormRowWidgets& row)
{
    if (!layout || !row.field)
        return;
    if (row.label)
        layout->addRow(row.label, row.field);
    else
        layout->addRow(row.field);
}
}

SlopeStabilityWidget::SlopeStabilityWidget(QWidget* parent)
    : BasePage("SlopeStabilityAnalysis"), ui(new Ui::SlopeStabilityWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    setupStableModeSections();
    SolverUiLayoutHelper::polishPage(this);

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
    ui->line8Combo->setItemText(1, QStringLiteral("施加已设置的外部荷载"));
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

    connect(ui->line19Combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateAnalysisModeVisibility(); });
    connect(ui->ifWaveCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateTimeHistoryVisibility(); });
    connect(ui->line3Combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateMethodVisibility(); });
    connect(ui->line16Combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this]() { updateDampingControls(); });

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
    updateAnalysisModeVisibility();
}

void SlopeStabilityWidget::setupStableModeSections()
{
    // “计算类型”必须固定在页面最上方。原先它位于基本参数表单底部，
    // 切换静力/动力时前面的行被隐藏，触发控件自身会突然移动，造成明显跳变。
    const FormRowWidgets modeRow = takeFormRow(ui->formLayout, ui->line19Combo);

    auto* modeGroup = new QGroupBox(QStringLiteral("计算模式"), this);
    modeGroup->setObjectName(QStringLiteral("analysisModeGroup"));
    auto* modeForm = new QFormLayout(modeGroup);
    appendFormRow(modeForm, modeRow);
    ui->contentLayout->insertWidget(0, modeGroup);

    // 阻尼参数本质上属于动力设置，从静力表单中移到动力分组。
    // 同时重新排序：先选择动力任务，再填写其依赖的时程参数。
    const FormRowWidgets dampingTypeRow = takeFormRow(ui->formLayout, ui->line15Combo);
    const FormRowWidgets rayleighRow = takeFormRow(ui->formLayout, ui->line16Combo);
    const FormRowWidgets dampingRatioRow = takeFormRow(ui->formLayout, ui->line17Edit);
    const FormRowWidgets stiffnessDampingRow = takeFormRow(ui->formLayout, ui->line18Edit);

    const FormRowWidgets timeStepRow = takeFormRow(ui->dynamicForm, ui->dtEdit);
    const FormRowWidgets startTimeRow = takeFormRow(ui->dynamicForm, ui->tstaEdit);
    const FormRowWidgets endTimeRow = takeFormRow(ui->dynamicForm, ui->tendEdit);
    const FormRowWidgets modalRow = takeFormRow(ui->dynamicForm, ui->ifModeCombo);
    const FormRowWidgets timeHistoryRow = takeFormRow(ui->dynamicForm, ui->ifWaveCombo);

    appendFormRow(ui->dynamicForm, dampingTypeRow);
    appendFormRow(ui->dynamicForm, rayleighRow);
    appendFormRow(ui->dynamicForm, dampingRatioRow);
    appendFormRow(ui->dynamicForm, stiffnessDampingRow);
    appendFormRow(ui->dynamicForm, modalRow);
    appendFormRow(ui->dynamicForm, timeHistoryRow);
    appendFormRow(ui->dynamicForm, timeStepRow);
    appendFormRow(ui->dynamicForm, startTimeRow);
    appendFormRow(ui->dynamicForm, endTimeRow);

    ui->islopGroup->setTitle(QStringLiteral("静力参数"));
    ui->dynamicGroup->setTitle(QStringLiteral("动力参数"));
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

void SlopeStabilityWidget::updateAnalysisModeVisibility()
{
    // 以计算类型下拉框为视觉锚点，切换后它在滚动面板中的位置保持不变。
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this, ui->line19Combo);

    const int type = ui->line19Combo->currentData().toString().toInt();
    const bool showStatic = (type == 1 || type == 3);
    const bool showDynamic = (type == 2 || type == 3);

    // 只切换完整分组，不再逐行隐藏同一表单中的控件。
    // 这样布局只发生一次整体变化，不会产生连续的上下抖动。
    ui->islopGroup->setVisible(showStatic);
    ui->reserveGroup->setVisible(showStatic);
    ui->dynamicGroup->setVisible(showDynamic);

    updateMethodVisibility();
    updateDampingControls();
    updateTimeHistoryVisibility();
}

void SlopeStabilityWidget::updateMethodVisibility()
{
    const int type = ui->line19Combo->currentData().toString().toInt();
    const bool showStatic = type == 1 || type == 3;
    const bool strengthReduction =
        ui->line3Combo->currentData().toString() != QStringLiteral("0");
    ui->reserveGroup->setVisible(showStatic && strengthReduction);
}

void SlopeStabilityWidget::updateDampingControls()
{
    const int type = ui->line19Combo->currentData().toString().toInt();
    const bool dynamic = type == 2 || type == 3;
    const bool rayleigh = ui->line16Combo->currentData().toString() == QStringLiteral("1");
    ui->line17Edit->setEnabled(dynamic);
    ui->line18Edit->setEnabled(dynamic && rayleigh);
    ui->label17->setText(rayleigh ? QStringLiteral("质量阻尼系数") : QStringLiteral("阻尼比"));
}

void SlopeStabilityWidget::updateTimeHistoryVisibility()
{
    const int type = ui->line19Combo->currentData().toString().toInt();
    const bool showDynamic = (type == 2 || type == 3);
    const bool enableTime = showDynamic &&
                            ui->ifWaveCombo->currentData().toString() == QStringLiteral("1");

    // 时程开关变化时保留行高，仅切换可用状态，避免动力分组再次跳动。
    const QList<QWidget*> timeWidgets = {
        ui->d2, ui->dtEdit, ui->d3, ui->tstaEdit, ui->d4, ui->tendEdit
    };
    for (QWidget* widget : timeWidgets)
    {
        widget->setVisible(true);
        widget->setEnabled(enableTime);
    }
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
    static const QStringList defaults = {"", "MACB.DAT", "3",
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
    SolverUiLayoutHelper::fitTableToRows(ui->strengthReserveTable,
                                          ui->strengthReserveTable->rowCount(), 5, 7);
}

void SlopeStabilityWidget::setStrengthReserveOutputCombo(int row, const QString& value)
{
    if (row < 0 || row >= ui->strengthReserveTable->rowCount())
        return;

    auto* combo = new QComboBox(ui->strengthReserveTable);
    combo->addItem(QStringLiteral("不输出该储备系数结果"), QStringLiteral("0.0000"));
    combo->addItem(QStringLiteral("输出该储备系数结果"), QStringLiteral("1.0000"));
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
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
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
    os.setEncoding(QStringConverter::System);
    const QString inputDirAbs = ensureTrailingSep(inputDir.absolutePath());
    const QString caseDirAbs = ensureTrailingSep(wd.filePath("CASE01"));

    for (int lineNo = 1; lineNo <= 20; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1)
            value = inputDirAbs;
        os << quoteValue(value) << "      " << lineNo << "\n";
    }

    const bool strengthReduction =
        ui->line3Combo->currentData().toString() != QStringLiteral("0");
    int nsrf = 0;
    for (int row = 0; strengthReduction && row < ui->strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        if (factorItem && !factorItem->text().trimmed().isEmpty())
            ++nsrf;
    }
    os << nsrf << "    NSRF\n";
    for (int row = 0; strengthReduction && row < ui->strengthReserveTable->rowCount(); ++row)
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

bool SlopeStabilityWidget::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    auto number = [](const QLineEdit* edit, double* value = nullptr)
    {
        bool ok = false;
        const double parsed = edit->text().trimmed().toDouble(&ok);
        if (ok && value)
            *value = parsed;
        return ok;
    };

    if (!number(ui->gravityXEdit) || !number(ui->gravityYEdit) || !number(ui->gravityZEdit))
        return fail(QStringLiteral("重力加速度必须填写三个有效数值。"));

    double iterations = 0.0;
    double tolerance = 0.0;
    if (!number(ui->line6Edit, &iterations) || iterations <= 0.0)
        return fail(QStringLiteral("最大迭代次数必须为正整数。"));
    if (!number(ui->line7Edit, &tolerance) || tolerance <= 0.0)
        return fail(QStringLiteral("迭代误差阈值必须为正数。"));

    if (ui->line3Combo->currentData().toString() != QStringLiteral("0"))
    {
        if (ui->strengthReserveTable->rowCount() == 0)
            return fail(QStringLiteral("强度折减计算至少需要一个储备系数。"));
        for (int row = 0; row < ui->strengthReserveTable->rowCount(); ++row)
        {
            const auto* item = ui->strengthReserveTable->item(row, 0);
            bool ok = false;
            const double factor = item ? item->text().trimmed().toDouble(&ok) : 0.0;
            if (!ok || factor <= 0.0)
                return fail(QStringLiteral("第 %1 个强度储备系数必须为正数。").arg(row + 1));
        }
    }

    const int type = ui->line19Combo->currentData().toString().toInt();
    if ((type == 2 || type == 3) &&
        ui->ifWaveCombo->currentData().toString() == QStringLiteral("1"))
    {
        double dt = 0.0;
        double start = 0.0;
        double end = 0.0;
        if (!number(ui->dtEdit, &dt) || dt <= 0.0)
            return fail(QStringLiteral("时程计算步长必须为正数。"));
        if (!number(ui->tstaEdit, &start) || !number(ui->tendEdit, &end) || end <= start)
            return fail(QStringLiteral("时程计算终止时刻必须大于起始时刻。"));
    }
    return true;
}

bool SlopeStabilityWidget::exportTdtraceDatFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(QStringLiteral("TDTRACE.DAT")));
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
    ui->ifModeCombo->setCurrentIndex(ifModeIndex >= 0 ? ifModeIndex : 0);
    ui->ifWaveCombo->setCurrentIndex(ifWaveIndex >= 0 ? ifWaveIndex : 0);
    updateAnalysisModeVisibility();
}

void SlopeStabilityWidget::onComputeClicked()
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

void SlopeStabilityWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止") : QStringLiteral("计算"));
}
