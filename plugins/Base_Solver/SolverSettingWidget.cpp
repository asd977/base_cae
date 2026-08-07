#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHeaderView>
#include <QHash>
#include <QJsonObject>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include "SolverUiLayoutHelper.h"

namespace
{
constexpr int kBatchUuidRole = Qt::UserRole + 1;
}

SolverSettingWidget::SolverSettingWidget(QWidget* parent)
    : BasePage("display"), ui(new Ui::SolverSettingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    m_computeDeviceCombo = ui->comboComputeDevice;
    m_gpuModeCombo = ui->comboGpuMode;
    m_assemblyLogSpin = ui->spinAssemblyLogInterval;
    m_solverLogSpin = ui->spinSolverLogInterval;
    m_computeDeviceCombo->clear();
    m_computeDeviceCombo->addItem(QStringLiteral("CPU 计算"), QStringLiteral("cpu"));
    m_computeDeviceCombo->addItem(QStringLiteral("GPU 计算"), QStringLiteral("gpu"));
    m_computeDeviceCombo->setToolTip(
        QStringLiteral("GPU 计算可提升部分大型模型的计算速度；块体稳定分析仍使用 CPU。"));

    m_gpuModeCombo->clear();
    m_gpuModeCombo->addItem(QStringLiteral("默认求解"), 1);
    m_gpuModeCombo->addItem(QStringLiteral("GPU 求解"), 2);
    m_gpuModeCombo->setToolTip(
        QStringLiteral("默认模式沿用工况设置；GPU 模式优先使用显卡计算。"));

    ui->batchAnalysisTree->setColumnCount(2);
    ui->batchAnalysisTree->setHeaderLabels(
        {QStringLiteral("计算节点"), QStringLiteral("状态")});
    ui->batchAnalysisTree->setRootIsDecorated(false);
    ui->batchAnalysisTree->setAlternatingRowColors(true);
    ui->batchAnalysisTree->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->batchAnalysisTree->header()->setStretchLastSection(false);
    ui->batchAnalysisTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->batchAnalysisTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    connect(ui->seepageButton, &QPushButton::clicked, this,
            &SolverSettingWidget::createSeepageRequested);
    connect(ui->damButton, &QPushButton::clicked, this,
            &SolverSettingWidget::createDamRequested);
    connect(ui->slopeButton, &QPushButton::clicked, this,
            &SolverSettingWidget::createSlopeRequested);
    connect(ui->caveButton, &QPushButton::clicked, this,
            &SolverSettingWidget::createCaveRequested);
    connect(ui->blockButton, &QPushButton::clicked, this,
            &SolverSettingWidget::createBlockRequested);

    connect(ui->batchSelectAllButton, &QPushButton::clicked, this,
            [this]()
            {
                for (int i = 0; i < ui->batchAnalysisTree->topLevelItemCount(); ++i)
                {
                    if (auto* item = ui->batchAnalysisTree->topLevelItem(i))
                        item->setCheckState(0, Qt::Checked);
                }
            });
    connect(ui->batchClearButton, &QPushButton::clicked, this,
            [this]()
            {
                for (int i = 0; i < ui->batchAnalysisTree->topLevelItemCount(); ++i)
                {
                    if (auto* item = ui->batchAnalysisTree->topLevelItem(i))
                        item->setCheckState(0, Qt::Unchecked);
                }
            });
    connect(ui->batchComputeButton, &QPushButton::clicked, this,
            [this]()
            {
                const QStringList uuids = checkedBatchAnalysisUuids();
                if (uuids.isEmpty())
                {
                    QMessageBox::information(this, QStringLiteral("批量计算"),
                                             QStringLiteral("请先勾选至少一个计算节点。"));
                    return;
                }
                emit batchComputeRequested(uuids);
            });
    connect(ui->batchStopButton, &QPushButton::clicked, this,
            &SolverSettingWidget::batchStopRequested);

    connect(m_computeDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
            {
                const QString device = computeDevice();
                QSettings().setValue(QStringLiteral("Solver/ComputeDevice"), device);
                updateDeviceControls();
                emit computeDeviceChanged(device);
            });
    connect(m_gpuModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
            {
                QSettings().setValue(QStringLiteral("Solver/GpuDeviceMode"), gpuDeviceMode());
            });
    connect(m_assemblyLogSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int value)
            {
                QSettings().setValue(QStringLiteral("Solver/AssemblyLogInterval"), value);
            });
    connect(m_solverLogSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [](int value)
            {
                QSettings().setValue(QStringLiteral("Solver/SolveLogInterval"), value);
            });

    QSettings settings;
    setComputeDevice(settings.value(QStringLiteral("Solver/ComputeDevice"),
                                    QStringLiteral("cpu")).toString());
    const int gpuMode = qBound(
        1, settings.value(QStringLiteral("Solver/GpuDeviceMode"), 2).toInt(), 2);
    const int gpuModeIndex = m_gpuModeCombo->findData(gpuMode);
    m_gpuModeCombo->setCurrentIndex(gpuModeIndex >= 0 ? gpuModeIndex : 1);
    m_assemblyLogSpin->setValue(qMax(
        0, settings.value(QStringLiteral("Solver/AssemblyLogInterval"), 0).toInt()));
    m_solverLogSpin->setValue(
        qMax(0, settings.value(QStringLiteral("Solver/SolveLogInterval"), 0).toInt()));
    updateDeviceControls();

    setAnalysisCounts(0, 0, 0, 0, 0);
    setBatchRunning(false);
}

SolverSettingWidget::~SolverSettingWidget()
{
    delete ui;
}

void SolverSettingWidget::beforeLoad() {}
void SolverSettingWidget::beforeUnload() {}

QJsonObject SolverSettingWidget::saveJson()
{
    QJsonObject obj;
    obj[QStringLiteral("compute_device")] = computeDevice();
    obj[QStringLiteral("gpu_device_mode")] = gpuDeviceMode();
    obj[QStringLiteral("assembly_log_interval")] = assemblyLogInterval();
    obj[QStringLiteral("solver_log_interval")] = solverLogInterval();
    return obj;
}

void SolverSettingWidget::loadJson(const QJsonObject& obj)
{
    setComputeDevice(obj.value(QStringLiteral("compute_device")).toString(
        QStringLiteral("cpu")));
    const int gpuMode = qBound(
        1, obj.value(QStringLiteral("gpu_device_mode")).toInt(2), 2);
    const int gpuModeIndex = m_gpuModeCombo ? m_gpuModeCombo->findData(gpuMode) : -1;
    if (gpuModeIndex >= 0)
        m_gpuModeCombo->setCurrentIndex(gpuModeIndex);
    if (m_assemblyLogSpin)
        m_assemblyLogSpin->setValue(qMax(
            0, obj.value(QStringLiteral("assembly_log_interval")).toInt(0)));
    if (m_solverLogSpin)
        m_solverLogSpin->setValue(
            qMax(0, obj.value(QStringLiteral("solver_log_interval")).toInt(0)));
    updateDeviceControls();
}

QString SolverSettingWidget::computeDevice() const
{
    if (!m_computeDeviceCombo)
        return QStringLiteral("cpu");
    const QString device = m_computeDeviceCombo->currentData().toString().toLower();
    return device == QStringLiteral("gpu") ? device : QStringLiteral("cpu");
}

void SolverSettingWidget::setComputeDevice(const QString& device)
{
    if (!m_computeDeviceCombo)
        return;
    const QString normalized = device.trimmed().toLower() == QStringLiteral("gpu")
                                   ? QStringLiteral("gpu")
                                   : QStringLiteral("cpu");
    const int index = m_computeDeviceCombo->findData(normalized);
    if (index >= 0)
        m_computeDeviceCombo->setCurrentIndex(index);
    QSettings().setValue(QStringLiteral("Solver/ComputeDevice"), normalized);
    updateDeviceControls();
}

int SolverSettingWidget::gpuDeviceMode() const
{
    if (!m_gpuModeCombo)
        return 2;
    return qBound(1, m_gpuModeCombo->currentData().toInt(), 2);
}

int SolverSettingWidget::assemblyLogInterval() const
{
    return m_assemblyLogSpin ? qMax(0, m_assemblyLogSpin->value()) : 0;
}

int SolverSettingWidget::solverLogInterval() const
{
    return m_solverLogSpin ? qMax(0, m_solverLogSpin->value()) : 0;
}

void SolverSettingWidget::updateDeviceControls()
{
    const bool gpu = computeDevice() == QStringLiteral("gpu");
    if (ui->gpuModeLabel)
        ui->gpuModeLabel->setEnabled(gpu);
    if (m_gpuModeCombo)
        m_gpuModeCombo->setEnabled(gpu);
}

void SolverSettingWidget::setAnalysisCounts(int seepageCount, int damCount, int slopeCount,
                                            int caveCount, int blockCount)
{
    const int total = seepageCount + damCount + slopeCount + caveCount + blockCount;
    ui->analysisSummaryLabel->setText(
        tr("共 %1 个分析：渗流 %2、土石坝 %3、边坡 %4、洞室开挖 %5、块体稳定 %6")
            .arg(total)
            .arg(seepageCount)
            .arg(damCount)
            .arg(slopeCount)
            .arg(caveCount)
            .arg(blockCount));
}

void SolverSettingWidget::setBatchAnalyses(const QVector<QPair<QString, QString>>& analyses)
{
    QHash<QString, Qt::CheckState> checkedStates;
    QHash<QString, QString> statuses;
    for (int i = 0; i < ui->batchAnalysisTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = ui->batchAnalysisTree->topLevelItem(i);
        if (!item)
            continue;
        const QString uuid = item->data(0, kBatchUuidRole).toString();
        checkedStates.insert(uuid, item->checkState(0));
        statuses.insert(uuid, item->text(1));
    }

    ui->batchAnalysisTree->clear();
    for (const auto& analysis : analyses)
    {
        auto* item = new QTreeWidgetItem(ui->batchAnalysisTree);
        item->setText(0, analysis.second);
        item->setText(1, statuses.value(analysis.first, QStringLiteral("待计算")));
        item->setData(0, kBatchUuidRole, analysis.first);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, checkedStates.value(analysis.first, Qt::Unchecked));
        item->setToolTip(0, analysis.second);
    }

    const bool hasAnalyses = !analyses.isEmpty();
    ui->batchEmptyLabel->setVisible(!hasAnalyses);
    ui->batchAnalysisTree->setVisible(hasAnalyses);
    ui->batchSelectAllButton->setEnabled(!m_batchRunning && hasAnalyses);
    ui->batchClearButton->setEnabled(!m_batchRunning && hasAnalyses);
    ui->batchComputeButton->setEnabled(!m_batchRunning && hasAnalyses);
}

void SolverSettingWidget::setBatchRunning(bool running, int total)
{
    m_batchRunning = running;
    ui->calculationSettingsGroup->setEnabled(!running);
    ui->createAnalysisGroup->setEnabled(!running);
    ui->batchAnalysisTree->setEnabled(!running);
    ui->batchSelectAllButton->setEnabled(!running);
    ui->batchClearButton->setEnabled(!running);
    ui->batchComputeButton->setEnabled(!running && ui->batchAnalysisTree->topLevelItemCount() > 0);
    ui->batchStopButton->setEnabled(running);

    if (running)
        ui->batchProgressLabel->setText(QStringLiteral("正在批量计算：0 / %1").arg(total));
}

void SolverSettingWidget::setBatchItemStatus(const QString& uuid, const QString& status)
{
    if (QTreeWidgetItem* item = findBatchItem(uuid))
    {
        item->setText(1, status);
        ui->batchAnalysisTree->scrollToItem(item, QAbstractItemView::PositionAtCenter);
        ui->batchAnalysisTree->setCurrentItem(item);
    }
}

void SolverSettingWidget::setBatchProgress(int finished, int total, int failed)
{
    if (total <= 0)
    {
        ui->batchProgressLabel->setText(QStringLiteral("未开始"));
        return;
    }

    const int succeeded = qMax(0, finished - failed);
    if (finished < total)
    {
        ui->batchProgressLabel->setText(
            QStringLiteral("正在批量计算：%1 / %2（成功 %3，失败 %4）")
                .arg(finished)
                .arg(total)
                .arg(succeeded)
                .arg(failed));
    }
    else
    {
        ui->batchProgressLabel->setText(
            QStringLiteral("批量计算完成：%1 / %2（成功 %3，失败 %4）")
                .arg(finished)
                .arg(total)
                .arg(succeeded)
                .arg(failed));
    }
}

void SolverSettingWidget::setBatchStatusText(const QString& text)
{
    ui->batchProgressLabel->setText(text.isEmpty() ? QStringLiteral("未开始") : text);
}

QStringList SolverSettingWidget::checkedBatchAnalysisUuids() const
{
    QStringList uuids;
    for (int i = 0; i < ui->batchAnalysisTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = ui->batchAnalysisTree->topLevelItem(i);
        if (!item || item->checkState(0) != Qt::Checked)
            continue;
        const QString uuid = item->data(0, kBatchUuidRole).toString();
        if (!uuid.isEmpty())
            uuids.push_back(uuid);
    }
    return uuids;
}

QTreeWidgetItem* SolverSettingWidget::findBatchItem(const QString& uuid) const
{
    if (uuid.isEmpty())
        return nullptr;
    for (int i = 0; i < ui->batchAnalysisTree->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = ui->batchAnalysisTree->topLevelItem(i);
        if (item && item->data(0, kBatchUuidRole).toString() == uuid)
            return item;
    }
    return nullptr;
}
