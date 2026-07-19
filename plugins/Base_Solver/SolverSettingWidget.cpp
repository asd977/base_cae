#include "SolverSettingWidget.h"
#include "ui_SolverSettingWidget.h"

#include <QPushButton>

#include "SolverUiLayoutHelper.h"

SolverSettingWidget::SolverSettingWidget(QWidget* parent)
    : BasePage("display"), ui(new Ui::SolverSettingWidget)
{
    Q_UNUSED(parent);
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);

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

    setAnalysisCounts(0, 0, 0, 0, 0);
}

SolverSettingWidget::~SolverSettingWidget()
{
    delete ui;
}

void SolverSettingWidget::beforeLoad() {}
void SolverSettingWidget::beforeUnload() {}

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
