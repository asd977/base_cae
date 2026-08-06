#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QComboBox>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>

#include "SolverUiLayoutHelper.h"

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
