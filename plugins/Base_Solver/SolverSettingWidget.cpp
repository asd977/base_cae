#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QComboBox>
#include <QJsonObject>
#include <QPushButton>
#include <QSettings>

#include "SolverUiLayoutHelper.h"

SolverSettingWidget::SolverSettingWidget(QWidget* parent)
    : BasePage("display"), ui(new Ui::SolverSettingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

    m_computeDeviceCombo = ui->comboComputeDevice;
    m_computeDeviceCombo->clear();
    m_computeDeviceCombo->addItem(QStringLiteral("CPU"), QStringLiteral("cpu"));
    m_computeDeviceCombo->addItem(QStringLiteral("GPU（接口预留）"), QStringLiteral("gpu"));
    m_computeDeviceCombo->setToolTip(
        QStringLiteral("统一设置所有数值计算使用的设备；GPU 求解接口目前仍为预留。"));

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
                emit computeDeviceChanged(device);
            });

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
    return obj;
}

void SolverSettingWidget::loadJson(const QJsonObject& obj)
{
    setComputeDevice(obj.value(QStringLiteral("compute_device")).toString(
        QStringLiteral("cpu")));
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
