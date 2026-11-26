#include "SlopeStabilityWidget.h"
#include "SlopeExcavationSettings.h"
#include "SlopeExcavationProtocol.h"
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <QFileInfo>
#include <QSaveFile>

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QDir>
#include <QFile>
#include <QIntValidator>
#include <QHeaderView>
#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLineEdit>
#include <QList>
#include <QStringConverter>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QStringList>
#include <QVBoxLayout>

#include "ui_SlopeStabilityWidget.h"

#include "SolverUiLayoutHelper.h"
#include "SolverQuietBoundary.h"
#include "QuietBoundarySelector.h"
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
    auto* excavationBox=new QGroupBox(QStringLiteral("分级开挖与加固"),this);
    auto* excavationLayout=new QVBoxLayout(excavationBox);
    m_excavation=new SlopeExcavationSettings(excavationBox);excavationLayout->addWidget(m_excavation);
    ui->verticalLayout->insertWidget(1,excavationBox);
    connect(m_excavation,&SlopeExcavationSettings::initialStressRequested,this,&SlopeStabilityWidget::initialStressRequested);
    connect(m_excavation,&SlopeExcavationSettings::structureChanged,this,&SlopeStabilityWidget::excavationStructureChanged);
    connect(m_excavation,&SlopeExcavationSettings::navigateToStageRequested,this,&SlopeStabilityWidget::navigateToExcavationStageRequested);
    connect(m_excavation,&SlopeExcavationSettings::navigateToReinforcementRequested,this,&SlopeStabilityWidget::navigateToReinforcementRequested);
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);

    if (auto* standardButton =
            findChild<QPushButton*>(QStringLiteral("btnStandardCompute")))
    {
        // 极限平衡是独立分析节点，不再嵌入数值边坡分析页面。
        QObject::disconnect(standardButton, nullptr, this, nullptr);
        standardButton->setText(QStringLiteral("新建极限平衡分析"));
        standardButton->setToolTip(
            QStringLiteral("新建独立的边坡极限平衡分析节点，不会覆盖当前数值法边坡分析。"));
        connect(standardButton, &QPushButton::clicked,
                this, &SlopeStabilityWidget::onStandardComputeClicked);
    }

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
    ui->line5Combo->addItem(QStringLiteral("线弹性分析"),QStringLiteral("0"));
    ui->label5->setText(QStringLiteral("力学分析方法"));
    ui->line8Combo->setItemData(0, "1");
    ui->line8Combo->setItemData(1, "2");
    ui->line8Combo->setItemText(1, QStringLiteral("施加已设置的外部荷载"));
    ui->line15Combo->clear();
    ui->line15Combo->addItem(QStringLiteral("系统统一阻尼"), QStringLiteral("0"));
    ui->line15Combo->setEnabled(false);
    ui->line15Combo->setToolTip(
        QStringLiteral("当前仅支持系统统一阻尼。"));
    ui->line16Combo->setItemData(0, "0");
    ui->line16Combo->setItemData(1, "1");
    ui->line19Combo->setItemData(0, "1");
    ui->line19Combo->setItemData(1, "2");
    ui->line19Combo->setItemData(2, "3");
    // SOLVER AUTOSLOP passes IFMODE directly as NMODES (number of vibration modes).
    ui->ifModeCombo->clear();
    ui->ifModeCombo->addItem(QStringLiteral("0 - 不计算"), QStringLiteral("0"));
    for (int modes : {1, 2, 3, 5, 10})
        ui->ifModeCombo->addItem(QStringLiteral("%1 阶").arg(modes), QString::number(modes));
    ui->ifModeCombo->setEditable(true);
    if (auto* le = ui->ifModeCombo->lineEdit())
        le->setValidator(new QIntValidator(0, 99, le));
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

    SolverUiLayoutHelper::installTableRowContextMenu(
        ui->strengthReserveTable,
        [this](int insertAt, int sourceRow, bool duplicate)
        {
            QString factor = QStringLiteral("1.0000");
            QString output = QStringLiteral("1.0000");
            if (duplicate && sourceRow >= 0
                && sourceRow < ui->strengthReserveTable->rowCount())
            {
                if (const auto* item = ui->strengthReserveTable->item(sourceRow, 0))
                    factor = item->text().trimmed();
                output = strengthReserveOutputValue(sourceRow);
            }
            insertAt = qBound(0, insertAt, ui->strengthReserveTable->rowCount());
            ui->strengthReserveTable->insertRow(insertAt);
            ui->strengthReserveTable->setItem(
                insertAt, 0, new QTableWidgetItem(factor.isEmpty()
                                                      ? QStringLiteral("1.0000")
                                                      : factor));
            setStrengthReserveOutputCombo(insertAt, output);
            ui->strengthReserveTable->setCurrentCell(insertAt, 0);
            SolverUiLayoutHelper::fitTableToRows(
                ui->strengthReserveTable, ui->strengthReserveTable->rowCount(), 5, 7);
        },
        [this](int row)
        {
            if (row < 0 || row >= ui->strengthReserveTable->rowCount())
                return;
            ui->strengthReserveTable->removeRow(row);
            if (ui->strengthReserveTable->rowCount() > 0)
                ui->strengthReserveTable->setCurrentCell(
                    qMin(row, ui->strengthReserveTable->rowCount() - 1), 0);
            SolverUiLayoutHelper::fitTableToRows(
                ui->strengthReserveTable, ui->strengthReserveTable->rowCount(), 5, 7);
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
    m_postExcavationReserveCheck = new QCheckBox(
        QStringLiteral("计算开挖后强度储备"), ui->islopGroup);
    m_postExcavationReserveCheck->setObjectName(QStringLiteral("postExcavationReserveCheck"));
    m_postExcavationReserveCheck->setChecked(true);
    m_postExcavationReserveCheck->setToolTip(
        QStringLiteral("仅控制 SOLVER 的开挖后强度储备工况；分级开挖仍需完整的开挖模型。"));
    ui->formLayout->addRow(QString(), m_postExcavationReserveCheck);
    ui->dynamicGroup->setTitle(QStringLiteral("动力参数"));
    m_dynamicExcavationShape = new QComboBox(ui->dynamicGroup);
    m_dynamicExcavationShape->setObjectName(QStringLiteral("dynamicExcavationShape"));
    m_dynamicExcavationShape->setToolTip(QStringLiteral(
        "仅对含开挖级的网格生效；没有开挖级时求解器可能强制采用开挖前构型。"));
    m_dynamicExcavationShape->addItem(QStringLiteral("开挖前构型"), QStringLiteral("1"));
    m_dynamicExcavationShape->addItem(QStringLiteral("开挖后构型"), QStringLiteral("2"));
    ui->dynamicForm->addRow(QStringLiteral("动力计算构型"), m_dynamicExcavationShape);
    m_quietBoundaryCheck = new QCheckBox(QStringLiteral("考虑粘弹性边界"), ui->dynamicGroup);
    m_quietBoundaryCheck->setObjectName(QStringLiteral("quietBoundaryCheck"));
    m_quietBoundarySelection = new QuietBoundarySelector(ui->dynamicGroup);
    m_quietBoundaryStatus = m_quietBoundarySelection->statusLabel();
    ui->dynamicForm->addRow(QString(),m_quietBoundaryCheck);
    ui->dynamicForm->addRow(m_quietBoundarySelection);
    m_quietBoundarySelection->setEnabled(false);
    connect(m_quietBoundaryCheck,&QCheckBox::toggled,this,[this](bool enabled){
        m_quietBoundarySelection->setEnabled(enabled);
        if(!enabled)m_quietBoundarySelection->stop();
    });
}

SlopeStabilityWidget::~SlopeStabilityWidget()
{
    delete ui;
}

void SlopeStabilityWidget::beforeLoad()
{
    m_excavation->enter();
}

void SlopeStabilityWidget::beforeUnload()
{
    m_excavation->leave();
    if(m_quietBoundarySelection)m_quietBoundarySelection->stop();
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

    if (m_quietBoundaryStatus)
        m_quietBoundaryStatus->setText(m_quietBoundarySelection->summary());
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
    if (m_postExcavationReserveCheck)
        ui->formLayout->setRowVisible(m_postExcavationReserveCheck,
                                     showStatic && strengthReduction && excavationEnabled());
    if (m_dynamicExcavationShape)
    {
        m_dynamicExcavationShape->setEnabled(excavationEnabled());
        if (!excavationEnabled() && m_dynamicExcavationShape->currentIndex() != 0)
            m_dynamicExcavationShape->setCurrentIndex(0);
    }
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
    if (lineNo == 21) return QStringLiteral("-1");
    if (lineNo == 22) return QString::number(excavationEnabled()?-m_excavation->stageCount():-1);
    if (lineNo == 23) return excavationEnabled()&&!m_excavation->reinforced()?QStringLiteral("1"):QStringLiteral("0");
    if (lineNo == 24) return excavationEnabled()&&m_excavation->reinforced()?QStringLiteral("1"):QStringLiteral("0");
    if (lineNo == 4)
        return gravityLineValue();
    if (lineNo == 10)
        return (excavationEnabled() && m_dynamicExcavationShape)
                   ? m_dynamicExcavationShape->currentData().toString()
                   : QStringLiteral("1");
    if (lineNo == 26)
        return m_postExcavationReserveCheck && m_postExcavationReserveCheck->isChecked()
            ? QStringLiteral("1") : QStringLiteral("0");
    if (lineNo == 27)
        return m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
            && (ui->line19Combo->currentData().toString() == QStringLiteral("2")
                || ui->line19Combo->currentData().toString() == QStringLiteral("3"))
            ? QStringLiteral("1") : QStringLiteral("0");
    // MACHAN.DAT 当前始终导出 0 个材料映射，分材料阻尼不可成立。
    if (lineNo == 15)
        return QStringLiteral("0");

    QWidget* w = m_lineEditors.value(lineNo, nullptr);
    if (auto* combo = qobject_cast<QComboBox*>(w))
        return combo->currentData().toString();
    if (auto* edit = qobject_cast<QLineEdit*>(w))
        return edit->text().trimmed();
    return defaultValueByLine(lineNo);
}

void SlopeStabilityWidget::setLineValue(int lineNo, const QString& value)
{
    if (lineNo == 10 && m_dynamicExcavationShape)
    {
        const int index = m_dynamicExcavationShape->findData(value.trimmed());
        m_dynamicExcavationShape->setCurrentIndex(index >= 0 ? index : 0);
        return;
    }
    if (lineNo == 26 && m_postExcavationReserveCheck)
    {
        m_postExcavationReserveCheck->setChecked(value.trimmed() != QStringLiteral("0"));
        return;
    }
    if (lineNo == 27 && m_quietBoundaryCheck)
    {
        m_quietBoundaryCheck->setChecked(value.trimmed() == QStringLiteral("1"));
        return;
    }
    if (lineNo == 4)
    {
        setGravityLineValue(value);
        return;
    }
    if (lineNo == 15)
    {
        Q_UNUSED(value);
        ui->line15Combo->setCurrentIndex(0);
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
                                         "1", "---", "JDTRACE.DAT", "KETRACE.DAT",
                                         "TDTRACE.DAT", "0", "0", "0.05", "0.0", "1", "MACHAN.DAT",
                                         "-1", "-1", "0", "0", "4", "1", "0", "FA_DYNA.DAT", "---", "---"};
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
    SolverUiLayoutHelper::configureComboSizing(combo);
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

    const int calcMode = ui->line19Combo->currentData().toString().toInt();
    const bool staticRun = (calcMode != 2);
    const bool dynamicRun = (calcMode == 2 || calcMode == 3);
    // 同一边坡分析重新计算静力时，新结果应替换旧静力目录；
    // 仅动力模式（calcMode==2）必须保留已有静力结果目录供 DIR_DYNA 继承。
    if (staticRun)
    {
        for (const QString& folder : {QStringLiteral("CASE01"), QStringLiteral("CASE02"),
                                      QStringLiteral("CASE01EXCA"), QStringLiteral("CASE01JACA")})
        {
            QDir previous(wd.filePath(folder));
            if (previous.exists() && !previous.removeRecursively())
                return false;
        }
    }
    for (const QString& folder : {QStringLiteral("CASE01MODE"), QStringLiteral("CASE01WAVE"),
                                  QStringLiteral("CASE02MODE"), QStringLiteral("CASE02WAVE")})
    {
        QDir previous(wd.filePath(folder));
        if (previous.exists() && !previous.removeRecursively())
            return false;
    }

    // 同步清理该分析的后处理缓存，计算完成后按新工况重新生成。
    QDir previousPostDir(wd.filePath(
        QString::fromUtf8(PublicConstants::kPostResultDirectoryName)));
    if (previousPostDir.exists() && !previousPostDir.removeRecursively())
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
    const QString dynamicState=excavationEnabled() && lineValue(10)==QStringLiteral("2")
        ? QStringLiteral("CASE01%1/%2%3").arg(m_excavation->reinforced()?"JACA":"EXCA",m_excavation->reinforced()?"J":"E",QStringLiteral("%1").arg(m_excavation->stageCount(),2,10,QLatin1Char('0')))
        : QStringLiteral("CASE01");
    const QString caseDirAbs = ensureTrailingSep(wd.filePath(dynamicState));
    const bool strengthReduction =
        ui->line3Combo->currentData().toString() != QStringLiteral("0");

    // SOLVER AUTOSLOP gates ITASK=1..6 via independent flags:
    // NAME(19)=ITYPE_JISUAN (0 skips ITASK=1), NAME(23/24)=IF_E1TOA/IF_J1TOA,
    // NAME(26)=ITYPE_EXSRF, IFMODE/IFWAVE control ITASK=5/6.
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        QString value = lineValue(lineNo);
        if (lineNo == 1 || (lineNo == 29 && excavationEnabled()))
            value = inputDirAbs;
        else if (lineNo == 19)
            value = staticRun ? QStringLiteral("1") : QStringLiteral("0");
        else if (lineNo == 23 || lineNo == 24)
            value = staticRun ? value : QStringLiteral("0");
        else if (lineNo == 26)
            value = (staticRun && strengthReduction && excavationEnabled()) ? value : QStringLiteral("0");
        os << quoteValue(value) << "      " << lineNo << "\n";
    }
    int nsrf = 0;
    for (int row = 0; strengthReduction && row < ui->strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        if (factorItem && !factorItem->text().trimmed().isEmpty())
            ++nsrf;
    }
    // SLOPIKEY uses NSRF for NSTEP even when strength reduction is off.
    // A single unit load step is required for an ordinary elastic solve.
    os << (strengthReduction?nsrf:1) << "    NSRF\n";
    if(!strengthReduction)os << "1.0000 1.0000 1 SRF IFPR II\n";
    int exportedIndex = 0;
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
        os << formattedFactor << " " << formattedIfpr <<" "<< QString("%1").arg(++exportedIndex)
           << " SRF    IFPR   II\n";
    }

    auto parsedIfMode = [this]() -> int
    {
        const QVariant data = ui->ifModeCombo->currentData();
        bool ok = false;
        if (data.isValid())
        {
            const int v = data.toString().toInt(&ok);
            if (ok && v >= 0 && ui->ifModeCombo->currentText() == ui->ifModeCombo->itemText(ui->ifModeCombo->currentIndex()))
                return v;
        }
        const QString raw = ui->ifModeCombo->currentText().trimmed().section(QLatin1Char(' '), 0, 0);
        const int v = raw.toInt(&ok);
        return (ok && v >= 0) ? v : 0;
    };
    const int exportedIfMode = dynamicRun ? parsedIfMode() : 0;
    const QString exportedIfWave = dynamicRun ? ui->ifWaveCombo->currentData().toString() : QStringLiteral("0");
    os << quoteValue(caseDirAbs) << "\n";
    os << ui->dtEdit->text().trimmed() << " " << ui->tstaEdit->text().trimmed() << " "
       << ui->tendEdit->text().trimmed() << " " << exportedIfMode
       << " " << exportedIfWave << "    DT TSTA TEND IFMODE IFWAVE\n";
    os << quoteValue(QString::fromLatin1(kAccXFileName)) << "  "
       << quoteValue(QString::fromLatin1(kAccYFileName)) << "  "
       << quoteValue(QString::fromLatin1(kAccZFileName)) << "    ACCEX ACCEY ACCEZ\n";

    return exportTdtraceDatFile() && exportMaterialMappingsFile();
}

bool SlopeStabilityWidget::validateInputs(QString* errorMessage) const
{
    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };
    if(excavationEnabled())
    {
        const auto stages=m_excavation->selectedStages();QSet<int> used;
        if(stages.isEmpty())return fail(QStringLiteral("请新增开挖级并选择区域。"));
        for(int i=0;i<stages.size();++i)
        {
            if(stages[i].isEmpty())return fail(QStringLiteral("第 %1 级没有选择开挖区域。").arg(i+1));
            for(int id:stages[i]){if(used.contains(id))return fail(QStringLiteral("同一区域不能分配到多个开挖级。"));used.insert(id);}
        }
        if(m_excavation->reinforced() && m_excavation->reinforcement()->mode()==CaveReinforcementFieldPage::Mode::Disabled)
            return fail(QStringLiteral("请在开挖面加固中选择加固方式。"));
    }
    auto number = [](const QLineEdit* edit, double* value = nullptr)
    {
        bool ok = false;
        const double parsed = edit->text().trimmed().toDouble(&ok);
        if (ok && value)
            *value = parsed;
        return ok;
    };

    if (m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
        && (ui->line19Combo->currentData().toString() == QStringLiteral("2")
            || ui->line19Combo->currentData().toString() == QStringLiteral("3"))
        && !m_quietBoundarySelection->generate())
        return fail(QStringLiteral("请在模型中选择粘弹性边界区域；当前支持线性边界面（二维选择边）。"));
    const int type = ui->line19Combo->currentData().toString().toInt();
    if (type == 1 || type == 3)
    {
        if (!number(ui->gravityXEdit) || !number(ui->gravityYEdit) || !number(ui->gravityZEdit))
            return fail(QStringLiteral("重力加速度必须填写三个有效数值。"));

        bool iterationsOk = false;
        const QString iterationsText = ui->line6Edit->text().trimmed();
        const int iterations = iterationsText.toInt(&iterationsOk);
        double tolerance = 0.0;
        if (!iterationsOk || iterations <= 0 || QString::number(iterations) != iterationsText)
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
    }
    if (type == 2)
    {
        const QString rawMode = ui->ifModeCombo->currentText().trimmed().section(QLatin1Char(' '), 0, 0);
        const int modeCount = rawMode.toInt();
        const bool hasWave = ui->ifWaveCombo->currentData().toString() == QStringLiteral("1");
        if (modeCount <= 0 && !hasWave)
            return fail(QStringLiteral("仅动力计算模式下至少需要启用振动模态或时程分析之一。"));
    }
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

bool SlopeStabilityWidget::exportMaterialMappingsFile() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(QStringLiteral("MACHAN.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&file);
    os << "           0   NUM\n";
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
    obj["excavation"]=m_excavation->save();
    obj["quiet_boundary_selection"]=m_quietBoundarySelection?m_quietBoundarySelection->save():QJsonObject();
    QJsonArray arr;
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
        arr.append(lineValue(lineNo));
    obj["base_values"] = arr;
    obj["quiet_boundary_enabled"] =
        m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked();
    QJsonArray reserve;
    for (int row = 0; row < ui->strengthReserveTable->rowCount(); ++row)
    {
        const auto* factorItem = ui->strengthReserveTable->item(row, 0);
        const QString factor = factorItem ? factorItem->text().trimmed() : QString();
        // 空白行不是有效的强度储备系数，不写入工程文件。
        // 这样既避免用户删除后的尾部空行再次出现，也能清理旧工程中已保存的空行。
        if (factor.isEmpty())
            continue;

        QJsonObject one;
        one["factor"] = factor;
        one["ifpr"] = strengthReserveOutputValue(row);
        reserve.append(one);
    }
    obj["strength_reserve"] = reserve;
    obj["dt"] = ui->dtEdit->text().trimmed();
    obj["tsta"] = ui->tstaEdit->text().trimmed();
    obj["tend"] = ui->tendEdit->text().trimmed();
    {
        const QVariant data = ui->ifModeCombo->currentData();
        const bool matchesPreset = data.isValid() &&
            ui->ifModeCombo->currentText() == ui->ifModeCombo->itemText(ui->ifModeCombo->currentIndex());
        const QString raw = ui->ifModeCombo->currentText().trimmed().section(QLatin1Char(' '), 0, 0);
        bool ok = false;
        const int v = raw.toInt(&ok);
        obj["ifmode"] = matchesPreset ? data.toString() : QString::number((ok && v >= 0) ? v : 0);
    }
    obj["ifwave"] = ui->ifWaveCombo->currentData().toString();
    obj["acc_x"] = QString::fromLatin1(kAccXFileName);
    obj["acc_y"] = QString::fromLatin1(kAccYFileName);
    obj["acc_z"] = QString::fromLatin1(kAccZFileName);
    return obj;
}

void SlopeStabilityWidget::loadJson(const QJsonObject& obj)
{
    m_excavation->load(obj.value("excavation").toObject());
    m_quietBoundarySelection->load(obj.value("quiet_boundary_selection").toObject(),obj.value("quiet_boundary_enabled").toBool(false));
    const QJsonArray arr = obj.value("base_values").toArray();
    for (int lineNo = 1; lineNo <= 30; ++lineNo)
    {
        if (lineNo == 1)
            continue;
        const QString value =
            (lineNo - 1 < arr.size() ? arr.at(lineNo - 1).toString() : defaultValueByLine(lineNo));
        setLineValue(lineNo, value);
    }

    if (obj.contains("quiet_boundary_enabled") && m_quietBoundaryCheck)
        m_quietBoundaryCheck->setChecked(
            obj.value("quiet_boundary_enabled").toBool(false));
    if (obj.contains("strength_reserve"))
    {
        const QJsonArray savedReserve = obj.value("strength_reserve").toArray();

        // 旧版本在加载时会把初始化的 12 行与工程中已保存的行数取最大值，
        // 因此被删除的行会重新以空行出现。这里仅恢复真正保存的有效系数。
        QList<QJsonObject> reserveRows;
        reserveRows.reserve(savedReserve.size());
        for (const QJsonValue& value : savedReserve)
        {
            const QJsonObject one = value.toObject();
            if (!one.value("factor").toString().trimmed().isEmpty())
                reserveRows.append(one);
        }

        ui->strengthReserveTable->clearContents();
        ui->strengthReserveTable->setRowCount(reserveRows.size());
        for (int row = 0; row < reserveRows.size(); ++row)
        {
            const QJsonObject& one = reserveRows.at(row);
            const QString factor = one.value("factor").toString().trimmed();
            const QString ifpr = one.value("ifpr").toString();
            ui->strengthReserveTable->setItem(row, 0, new QTableWidgetItem(factor));
            setStrengthReserveOutputCombo(
                row, ifpr.isEmpty() ? QStringLiteral("1.0000") : ifpr);
        }
        SolverUiLayoutHelper::fitTableToRows(
            ui->strengthReserveTable, ui->strengthReserveTable->rowCount(), 5, 7);
    }

    ui->dtEdit->setText(obj.value("dt").toString(ui->dtEdit->text()));
    ui->tstaEdit->setText(obj.value("tsta").toString(ui->tstaEdit->text()));
    ui->tendEdit->setText(obj.value("tend").toString(ui->tendEdit->text()));
    const QString ifMode = obj.value("ifmode").toString(QStringLiteral("0")).trimmed();
    const QString ifWave = obj.value("ifwave").toString(ui->ifWaveCombo->currentData().toString());
    const int ifModeIndex = ui->ifModeCombo->findData(ifMode);
    const int ifWaveIndex = ui->ifWaveCombo->findData(ifWave);
    if (ifModeIndex >= 0)
        ui->ifModeCombo->setCurrentIndex(ifModeIndex);
    else
        ui->ifModeCombo->setEditText(ifMode.isEmpty() ? QStringLiteral("0") : ifMode);
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
        QMessageBox::warning(this, QStringLiteral("请完善边坡设置"), error);
        return;
    }
    emit computeRequested();
}

void SlopeStabilityWidget::onStandardComputeClicked()
{
    emit limitEquilibriumRequested();
}

void SlopeStabilityWidget::openStandardMethodDialog()
{
    // 兼容旧调用入口：现在不再打开/嵌入弹窗，而是请求 Solver 新建独立分析节点。
    emit limitEquilibriumRequested();
}

void SlopeStabilityWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}

bool SlopeStabilityWidget::excavationEnabled() const { return m_excavation && m_excavation->enabled(); }

bool SlopeStabilityWidget::prepareExcavationFiles(const QString& inputDirectory,const QString& stressDirectory,QString* error)
{
    auto fail=[&](const QString& message){if(error)*error=message;return false;};
    auto* mesh=ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if(!mesh)return fail(QStringLiteral("无法读取当前网格。"));
    auto grid=mesh->getVtkUnstructuredGrid();
    const int dimension=PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QDir input(inputDirectory);
    for(const auto& pair:{qMakePair(QStringLiteral("MODSTR.TXT"),QStringLiteral("MODSTR_CZ100CB100.TXT")),
                            qMakePair(QStringLiteral("ISGMAR.TXT"),QStringLiteral("ISGMAR_CZ100CB100.TXT"))})
    {
        QFile source(QDir(stressDirectory).filePath(pair.first));
        if(!source.open(QIODevice::ReadOnly) || source.size()==0)return fail(QStringLiteral("请先通过“设置／计算初始地应力”完成本工况的初始地应力计算。"));
        const auto bytes=source.readAll();
        for(const QString& targetName:{pair.first,pair.second})
        {
            QSaveFile output(input.filePath(targetName));
            if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())return fail(QStringLiteral("无法准备初始地应力文件。"));
        }
    }
    SlopeExcavationProtocol::Prepared prepared;QString message;
    const bool preserve=QFileInfo(mesh->sourceMeshFilePath()).suffix().compare("dat",Qt::CaseInsensitive)==0;
    if(!SlopeExcavationProtocol::prepare(input,grid,dimension,preserve,m_excavation->selectedStages(),prepared,message))return fail(message);
    if(m_excavation->reinforced())
    {
        if(!m_excavation->reinforcement()->exportFieldFiles(inputDirectory,grid,dimension,prepared.layers,prepared.faces,&message))return fail(message);
        if(!SlopeExcavationProtocol::remapReinforcementIds(input,grid,prepared,message))return fail(message);
    }
    else {QFile::remove(input.filePath("ALFAIE.DAT"));QFile::remove(input.filePath("ALFAJD.DAT"));}
    // AUTOSLOP tests the input directory, but READ_ALFAIE reads the run root.
    // Supply both native locations; keep both fresh when support is disabled.
    QDir runtime(inputDirectory);runtime.cdUp();
    for(const QString& name:{QStringLiteral("ALFAIE.DAT"),QStringLiteral("ALFAJD.DAT")})
    {
        if(!m_excavation->reinforced()){QFile::remove(runtime.filePath(name));continue;}
        QFile source(input.filePath(name));if(!source.open(QIODevice::ReadOnly))return fail(QStringLiteral("无法读取加固参数场。"));
        const auto bytes=source.readAll();QSaveFile output(runtime.filePath(name));
        if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())return fail(QStringLiteral("无法准备工况加固参数场。"));
    }
    return true;
}
