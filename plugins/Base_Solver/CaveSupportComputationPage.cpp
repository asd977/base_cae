#include "CaveSupportComputationPage.h"
#include "CaveExcavationProtocol.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <struct/MeshMap.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirIterator>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHash>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringConverter>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTextStream>
#include <QVariantMap>
#include <QMap>
#include <QWidget>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkPointData.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <utility>

#include "CaveReinforcementFieldPage.h"
#include "SolverUiLayoutHelper.h"
#include "SolverQuietBoundary.h"
#include "QuietBoundarySelector.h"
#include "ui_CaveSupportComputationPage.h"

CaveSupportComputationPage::CaveSupportComputationPage(QWidget* parent)
    : BasePage("CaveSupportComputation"), ui(new Ui::CaveSupportComputationPage)
{
    ui->setupUi(this);
    Q_UNUSED(parent);

    m_ifSeepCombo = ui->comboIfSeep;
    m_dampingModeCombo = ui->comboDampingMode;
    m_calculationTypeCombo = ui->comboCalculationType;
    m_excavationLoadMethodCombo = ui->comboExcavationLoadMethod;
    m_iterationMethodCombo = ui->comboIterationMethod;
    m_iterationCountSpin = ui->spinIterationCount;
    m_iexcaMaxSpin = ui->spinIexcaMax;
    m_excavationDaysTable = ui->tableExcavationDays;
    m_iexcaStaEdit = ui->editIexcaSta;
    m_iexcaEndEdit = ui->editIexcaEnd;
    m_ifEallCombo = ui->comboIfEall;
    m_ifJallCombo = ui->comboIfJall;
    m_ifE1toaCombo = ui->comboIfE1toa;
    m_ifJ1toaCombo = ui->comboIfJ1toa;
    m_jaMethodCombo = ui->comboJaMethod;
    m_ifChenCombo = ui->comboIfChen;
    m_idayCaseEdit = ui->editIdayCase;
    m_modalCountSpin = ui->spinModalCount;
    m_ifWaveCombo = ui->comboIfWave;
    m_dtSpin = ui->spinDt;
    m_tstaSpin = ui->spinTsta;
    m_tendSpin = ui->spinTend;
    m_accXEdit = ui->editAccX;
    m_accYEdit = ui->editAccY;
    m_accZEdit = ui->editAccZ;

    m_reinforcementFieldPage = new CaveReinforcementFieldPage(this);
    ui->contentLayout->insertWidget(4, m_reinforcementFieldPage);
    setupBusinessUi();
    hideInternalControls();

    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);

    m_accXEdit->setText(QStringLiteral("WAVE-AX.DAT"));
    m_accYEdit->setText(QStringLiteral("WAVE-AY.DAT"));
    m_accZEdit->setText(QStringLiteral("WAVE-AZ.DAT"));

    m_excavationDaysTable->setColumnCount(2);
    m_excavationDaysTable->setHorizontalHeaderLabels(
        {QStringLiteral("开挖级"), QStringLiteral("用时（天）")});
    m_excavationDaysTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);

    auto setupBoolCombo = [](QComboBox* combo, const QString& enableText,
                             const QString& disableText)
    {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(disableText, QStringLiteral("0"));
        combo->addItem(enableText, QStringLiteral("1"));
    };

    setupBoolCombo(m_ifEallCombo, QStringLiteral("计算不加固条件下一次开挖成洞"),
                   QStringLiteral("不进行不加固条件下一次开挖成洞计算"));
    setupBoolCombo(m_ifJallCombo, QStringLiteral("计算加固条件下一次开挖成洞"),
                   QStringLiteral("不进行加固条件下一次开挖成洞计算"));
    setupBoolCombo(m_ifE1toaCombo, QStringLiteral("计算不加固条件下逐级开挖成洞"),
                   QStringLiteral("不进行不加固条件下逐级开挖成洞计算"));
    setupBoolCombo(m_ifJ1toaCombo, QStringLiteral("计算加固条件下逐级开挖成洞"),
                   QStringLiteral("不进行加固条件下逐级开挖成洞计算"));
    setupBoolCombo(m_ifChenCombo, QStringLiteral("计算"), QStringLiteral("不计算"));

    // 以下下拉框只负责生成求解器控制码，实际业务入口由本页的新设置区承载。
    m_ifSeepCombo->clear();
    m_ifSeepCombo->addItem(QStringLiteral("不考虑"), QStringLiteral("0"));
    m_ifSeepCombo->addItem(QStringLiteral("考虑"), QStringLiteral("3"));

    m_dampingModeCombo->clear();
    m_dampingModeCombo->addItem(QStringLiteral("使用系统统一阻尼"), QStringLiteral("0"));
    m_dampingModeCombo->addItem(QStringLiteral("使用分材料阻尼"), QStringLiteral("1"));

    m_calculationTypeCombo->clear();
    m_calculationTypeCombo->addItem(QStringLiteral("仅进行静力开挖计算"), QStringLiteral("1"));
    m_calculationTypeCombo->addItem(QStringLiteral("仅进行动力计算"), QStringLiteral("2"));
    m_calculationTypeCombo->addItem(QStringLiteral("静力开挖后继续动力计算"), QStringLiteral("3"));

    m_excavationLoadMethodCombo->clear();
    m_excavationLoadMethodCombo->addItem(QStringLiteral("按开挖面计算"), QStringLiteral("1"));
    m_excavationLoadMethodCombo->addItem(QStringLiteral("按开挖单元计算"), QStringLiteral("2"));
    m_excavationLoadMethodCombo->setCurrentIndex(1);

    m_iterationMethodCombo->clear();
    m_iterationMethodCombo->addItem(QStringLiteral("线弹性分析"), QStringLiteral("0"));
    m_iterationMethodCombo->addItem(QStringLiteral("初应变法"), QStringLiteral("1"));
    m_iterationMethodCombo->addItem(QStringLiteral("初应力法"), QStringLiteral("2"));
    m_iterationMethodCombo->addItem(QStringLiteral("变刚度法（切线刚度法）"), QStringLiteral("3"));
    m_iterationMethodCombo->setCurrentIndex(1);
    m_iterationCountSpin->setValue(100);

    m_jaMethodCombo->clear();
    m_jaMethodCombo->addItem(QStringLiteral("普通加固"), QStringLiteral("1"));
    m_jaMethodCombo->addItem(QStringLiteral("考虑再生区"), QStringLiteral("2"));
    m_idayCaseEdit->setText(QStringLiteral("0"));

    m_modalCountSpin->setToolTip(QStringLiteral("0 表示不进行模态分析；正整数表示计算的模态阶数。"));
    m_ifWaveCombo->clear();
    m_ifWaveCombo->addItem(QStringLiteral("不进行时程分析"), QStringLiteral("0"));
    m_ifWaveCombo->addItem(QStringLiteral("进行时程分析"), QStringLiteral("1"));

    connect(m_iexcaMaxSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            &CaveSupportComputationPage::onExcavationLevelCountChanged);
    connect(m_calculationTypeCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::updateAnalysisModeUi);
    connect(m_ifWaveCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::updateDynamicDetailsUi);
    for (QComboBox* combo : {m_ifEallCombo, m_ifJallCombo, m_ifE1toaCombo,
                             m_ifJ1toaCombo})
    {
        connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                &CaveSupportComputationPage::updateStaticDependenciesUi);
    }
    connect(m_ifJallCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::onReinforcedTaskSelectionChanged);
    connect(m_ifJ1toaCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &CaveSupportComputationPage::onReinforcedTaskSelectionChanged);
    connect(ui->btnCompute, &QPushButton::clicked, this,
            &CaveSupportComputationPage::onComputeClicked);

    SolverUiLayoutHelper::installTableRowContextMenu(
        m_excavationDaysTable,
        [this](int insertAt, int sourceRow, bool duplicate)
        {
            if (!m_excavationDaysTable || !m_iexcaMaxSpin)
                return;
            QStringList days;
            for (int row = 0; row < m_excavationDaysTable->rowCount(); ++row)
            {
                const auto* item = m_excavationDaysTable->item(row, 1);
                days.push_back(item ? item->text().trimmed() : QStringLiteral("0"));
            }
            insertAt = qBound(0, insertAt, int(days.size()));
            QString newValue = QStringLiteral("0");
            if (duplicate && sourceRow >= 0 && sourceRow < int(days.size()))
                newValue = days.at(sourceRow);
            days.insert(insertAt, newValue);
            m_iexcaMaxSpin->setValue(int(days.size()));
            for (int row = 0; row < int(days.size()); ++row)
            {
                if (!m_excavationDaysTable->item(row, 1))
                    m_excavationDaysTable->setItem(row, 1, new QTableWidgetItem(days.at(row)));
                else
                    m_excavationDaysTable->item(row, 1)->setText(days.at(row));
            }
            m_excavationDaysTable->setCurrentCell(insertAt, 1);
        },
        [this](int row)
        {
            if (!m_excavationDaysTable || !m_iexcaMaxSpin
                || m_excavationDaysTable->rowCount() <= 1
                || row < 0 || row >= m_excavationDaysTable->rowCount())
                return;
            QStringList days;
            for (int i = 0; i < m_excavationDaysTable->rowCount(); ++i)
            {
                if (i == row)
                    continue;
                const auto* item = m_excavationDaysTable->item(i, 1);
                days.push_back(item ? item->text().trimmed() : QStringLiteral("0"));
            }
            m_iexcaMaxSpin->setValue(int(days.size()));
            for (int i = 0; i < int(days.size()); ++i)
            {
                if (!m_excavationDaysTable->item(i, 1))
                    m_excavationDaysTable->setItem(i, 1, new QTableWidgetItem(days.at(i)));
                else
                    m_excavationDaysTable->item(i, 1)->setText(days.at(i));
            }
            m_excavationDaysTable->setCurrentCell(qMin(row, int(days.size()) - 1), 1);
        },
        1);

    m_caseData = CaseData();
    onExcavationLevelCountChanged(m_iexcaMaxSpin->value());
    loadCaseToUi();
    refreshGroundwaterStatus();
    updateAnalysisModeUi();
}


void CaveSupportComputationPage::setupBusinessUi()
{
    m_quietBoundaryCheck = new QCheckBox(QStringLiteral("考虑粘弹性边界"), ui->groupDyna);
    m_quietBoundaryCheck->setObjectName(QStringLiteral("quietBoundaryCheck"));
    m_quietBoundaryCheck->setToolTip(QStringLiteral("用于吸收人工截断边界的波动；启用后在模型中选择边界区域。"));
    m_quietBoundarySelection = new QuietBoundarySelector(ui->groupDyna);
    m_quietBoundaryStatus = m_quietBoundarySelection->statusLabel();
    ui->formLayoutDyna->addRow(QString(),m_quietBoundaryCheck);
    ui->formLayoutDyna->addRow(m_quietBoundarySelection);
    m_quietBoundarySelection->setEnabled(false);
    connect(m_quietBoundaryCheck,&QCheckBox::toggled,this,[this](bool enabled){
        m_quietBoundarySelection->setEnabled(enabled);
        if(!enabled)m_quietBoundarySelection->stop();
    });

    m_liningGroundwaterGroup = new QGroupBox(QStringLiteral("衬砌与地下水"), this);
    auto* form = new QFormLayout(m_liningGroundwaterGroup);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_liningEnabledCheck = new QCheckBox(QStringLiteral("计算衬砌内力"),
                                         m_liningGroundwaterGroup);
    form->addRow(QString(), m_liningEnabledCheck);

    m_liningStatusLabel = new QLabel(m_liningGroundwaterGroup);
    m_liningStatusLabel->setWordWrap(true);
    form->addRow(m_liningStatusLabel);

    m_groundwaterCheck = new QCheckBox(QStringLiteral("考虑地下水影响"),
                                       m_liningGroundwaterGroup);
    form->addRow(QString(), m_groundwaterCheck);

    m_groundwaterStatusLabel = new QLabel(m_liningGroundwaterGroup);
    m_groundwaterStatusLabel->setWordWrap(true);
    form->addRow(m_groundwaterStatusLabel);

    ui->contentLayout->insertWidget(5, m_liningGroundwaterGroup);

    connect(m_liningEnabledCheck, &QCheckBox::toggled, this,
            [this](bool enabled)
            {
                if (m_isUpdatingUi)
                    return;
                if (!enabled && m_groundwaterCheck)
                {
                    const QSignalBlocker blocker(m_groundwaterCheck);
                    m_groundwaterCheck->setChecked(false);
                }

                // 求解器的衬砌任务以开挖结果作为基准。用户只决定是否计算衬砌，
                // 必要的基准开挖工况由软件自动补齐。
                if (enabled
                    && m_ifEallCombo->currentData().toString() != QStringLiteral("1")
                    && m_ifE1toaCombo->currentData().toString() != QStringLiteral("1"))
                {
                    QComboBox* target =
                        m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1")
                            ? m_ifE1toaCombo : m_ifEallCombo;
                    const QSignalBlocker blocker(target);
                    const int index = target->findData(QStringLiteral("1"));
                    if (index >= 0)
                        target->setCurrentIndex(index);
                }
                refreshLiningStatus();
                refreshGroundwaterStatus();
                updateStaticDependenciesUi();
                emit liningEnabledChanged(enabled);
            });
    connect(m_groundwaterCheck, &QCheckBox::toggled, this,
            [this](bool)
            {
                if (m_isUpdatingUi)
                    return;
                refreshGroundwaterStatus();
                updateStaticDependenciesUi();
            });

    m_craneBeamGroup = new QGroupBox(QStringLiteral("吊车梁施工"), this);
    auto* craneForm = new QFormLayout(m_craneBeamGroup);
    craneForm->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    craneForm->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_craneBeamEnabledCheck = new QCheckBox(QStringLiteral("考虑吊车梁施工"),
                                             m_craneBeamGroup);
    craneForm->addRow(QString(), m_craneBeamEnabledCheck);

    m_craneBeamInstallStageCombo = new QComboBox(m_craneBeamGroup);
    craneForm->addRow(QStringLiteral("安装阶段"), m_craneBeamInstallStageCombo);

    m_craneBeamReleaseSpin = new QDoubleSpinBox(m_craneBeamGroup);
    m_craneBeamReleaseSpin->setRange(0.0, 1.0);
    m_craneBeamReleaseSpin->setDecimals(4);
    m_craneBeamReleaseSpin->setSingleStep(0.05);
    m_craneBeamReleaseSpin->setValue(1.0);
    m_craneBeamReleaseSpin->setToolTip(
        QStringLiteral("吊车梁安装后承担的围岩剩余释放荷载比例，范围 0～1。"));
    craneForm->addRow(QStringLiteral("围岩荷载释放系数"), m_craneBeamReleaseSpin);

    m_craneBeamStatusLabel = new QLabel(m_craneBeamGroup);
    m_craneBeamStatusLabel->setWordWrap(true);
    craneForm->addRow(m_craneBeamStatusLabel);

    ui->contentLayout->insertWidget(6, m_craneBeamGroup);

    connect(m_craneBeamEnabledCheck, &QCheckBox::toggled, this,
            [this](bool enabled)
            {
                if (m_isUpdatingUi)
                    return;
                refreshCraneBeamUi();
                emit craneBeamEnabledChanged(enabled);
            });
    connect(m_craneBeamInstallStageCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int)
            {
                if (!m_isUpdatingUi)
                    refreshCraneBeamUi();
            });
    connect(m_craneBeamReleaseSpin,
            qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double)
            {
                if (!m_isUpdatingUi)
                    refreshCraneBeamUi();
            });

    updateCraneBeamInstallStages();
    refreshCraneBeamUi();
}

void CaveSupportComputationPage::hideInternalControls()
{
    QWidget* const hiddenWidgets[] = {
        ui->label3, ui->comboIfSeep,
        ui->label5, ui->line5Edit,
        ui->label7, ui->line7Edit,
        ui->label10, ui->line10Edit,
        ui->label11, ui->line11Edit,
        ui->labelIexcaSta, ui->editIexcaSta,
        ui->labelIexcaEnd, ui->editIexcaEnd,
        ui->labelJaMethod, ui->comboJaMethod,
        ui->labelIfChen, ui->comboIfChen};
    for (QWidget* widget : hiddenWidgets)
    {
        if (widget)
            widget->hide();
    }

    // 吊车梁业务设置会在计算前自动映射到内部控制字段。
    ui->line10Edit->setText(QStringLiteral("0"));
    ui->line11Edit->setText(QStringLiteral("1.0"));
    // 当前地下水采用渗流节点场；洞内默认无水，外侧压力来自节点孔压。
    ui->line5Edit->setText(QStringLiteral("0.0"));
    ui->line7Edit->setText(QStringLiteral("0.0"));
}

CaveSupportComputationPage::~CaveSupportComputationPage()
{
    delete ui;
}

void CaveSupportComputationPage::beforeLoad()
{
    updateCraneBeamInstallStages();
    refreshLiningStatus();
    refreshGroundwaterStatus();
    refreshCraneBeamUi();
    updateAnalysisModeUi();
}


void CaveSupportComputationPage::beforeUnload() {
    if(m_quietBoundarySelection)m_quietBoundarySelection->stop();}

void CaveSupportComputationPage::updateCraneBeamInstallStages()
{
    if (!m_craneBeamInstallStageCombo)
        return;

    const int stageCount = excavationLevelCount();
    const int previousStage = m_craneBeamInstallStageCombo->currentData().toInt();
    const int targetStage = qBound(1, previousStage > 0 ? previousStage : 1, stageCount);

    const QSignalBlocker blocker(m_craneBeamInstallStageCombo);
    m_craneBeamInstallStageCombo->clear();
    for (int stage = 1; stage <= stageCount; ++stage)
    {
        m_craneBeamInstallStageCombo->addItem(
            QStringLiteral("第 %1 级开挖后").arg(stage), stage);
    }

    const int targetIndex = m_craneBeamInstallStageCombo->findData(targetStage);
    m_craneBeamInstallStageCombo->setCurrentIndex(targetIndex >= 0 ? targetIndex : 0);
}

void CaveSupportComputationPage::setExcavationLevelCount(int count)
{
    const int target = qMax(1, count);

    if (m_iexcaMaxSpin)
    {
        const QSignalBlocker blocker(m_iexcaMaxSpin);
        m_iexcaMaxSpin->setValue(target);
    }

    onExcavationLevelCountChanged(target);

    // Fortran 中开挖层号使用 -1、-2、...、-N。
    m_caseData.iexcaSta = QStringLiteral("-1");
    m_caseData.iexcaEnd = QString::number(-target);

    if (m_iexcaStaEdit)
        m_iexcaStaEdit->setText(m_caseData.iexcaSta);
    if (m_iexcaEndEdit)
        m_iexcaEndEdit->setText(m_caseData.iexcaEnd);
}

int CaveSupportComputationPage::excavationLevelCount() const
{
    return m_iexcaMaxSpin ? qMax(1, m_iexcaMaxSpin->value()) : 1;
}

int CaveSupportComputationPage::calculationTypeCode() const
{
    if (!m_calculationTypeCombo)
        return 1;

    bool ok = false;
    const int code = m_calculationTypeCombo->currentData().toString().toInt(&ok);
    return ok ? code : 1;
}

int CaveSupportComputationPage::excavationLoadMethodCode() const
{
    if (!m_excavationLoadMethodCombo)
        return 2;

    bool ok = false;
    const int code = m_excavationLoadMethodCombo->currentData().toString().toInt(&ok);
    return ok ? code : 2;
}

bool CaveSupportComputationPage::modalAnalysisEnabled() const
{
    return modalCount() > 0;
}

int CaveSupportComputationPage::modalCount() const
{
    return m_modalCountSpin ? m_modalCountSpin->value() : 0;
}

bool CaveSupportComputationPage::timeHistoryEnabled() const
{
    return m_ifWaveCombo &&
           m_ifWaveCombo->currentData().toString() == QStringLiteral("1");
}

bool CaveSupportComputationPage::reinforcedTaskEnabled() const
{
    return (m_ifJallCombo && m_ifJallCombo->currentData().toString() == QStringLiteral("1"))
           || (m_ifJ1toaCombo
               && m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1"));
}

int CaveSupportComputationPage::groundwaterModeCode() const
{
    return m_groundwaterCheck && m_groundwaterCheck->isChecked() ? 3 : 0;
}


bool CaveSupportComputationPage::liningEnabled() const
{
    return m_liningEnabledCheck && m_liningEnabledCheck->isChecked();
}

bool CaveSupportComputationPage::craneBeamEnabled() const
{
    return m_craneBeamEnabledCheck && m_craneBeamEnabledCheck->isChecked();
}

int CaveSupportComputationPage::craneBeamInstallStage() const
{
    return m_craneBeamInstallStageCombo
        ? qMax(1, m_craneBeamInstallStageCombo->currentData().toInt()) : 1;
}

double CaveSupportComputationPage::craneBeamReleaseFactor() const
{
    return m_craneBeamReleaseSpin ? m_craneBeamReleaseSpin->value() : 1.0;
}

CaveReinforcementFieldPage* CaveSupportComputationPage::reinforcementFieldPage() const
{
    return m_reinforcementFieldPage;
}

void CaveSupportComputationPage::loadLegacyReinforcementFieldJson(const QJsonObject& obj)
{
    if (!m_reinforcementFieldPage || obj.isEmpty())
        return;
    m_reinforcementFieldPage->loadJson(obj);
    updateStaticDependenciesUi();
}



QJsonObject CaveSupportComputationPage::legacyCraneBeamAreaState() const
{
    return m_legacyCraneBeamAreaState;
}



void CaveSupportComputationPage::refreshCraneBeamUi()
{
    const bool staticEnabled = calculationTypeCode() == 1 || calculationTypeCode() == 3;
    const bool enabled = staticEnabled && craneBeamEnabled();
    if (m_craneBeamInstallStageCombo)
        m_craneBeamInstallStageCombo->setEnabled(enabled);
    if (m_craneBeamReleaseSpin)
        m_craneBeamReleaseSpin->setEnabled(enabled);

    if (!m_craneBeamStatusLabel)
        return;
    m_craneBeamStatusLabel->setText(
        craneBeamEnabled()
            ? QStringLiteral("请在本节点下的“吊车梁区域”中选择实体区域。进入该节点后其余模型会半透明显示。")
            : QStringLiteral("未启用。"));
}

QVector<QVariantMap> CaveSupportComputationPage::availableSeepageResults() const
{
    QVector<QVariantMap> results;
    const QString projectDirectory =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (projectDirectory.trimmed().isEmpty() || !QDir(projectDirectory).exists())
        return results;

    QHash<QString, QVariantMap> newestByAnalysis;
    const QStringList manifestNames = {
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName),
        QStringLiteral("analysis.json")};

    for (const QString& manifestName : manifestNames)
    {
        QDirIterator manifests(projectDirectory, QStringList{manifestName}, QDir::Files,
                               QDirIterator::Subdirectories);
        while (manifests.hasNext())
        {
            const QFileInfo manifestInfo(manifests.next());
            QFile manifestFile(manifestInfo.absoluteFilePath());
            if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;
            const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll());
            if (!document.isObject())
                continue;

            const QJsonObject object = document.object();
            const QString state = object.value(QStringLiteral("state")).toString();
            const QString kind = object.value(QStringLiteral("analysisKind")).toString();
            const QString lastType = object.value(QStringLiteral("lastAnalysisType")).toString();
            if (state.compare(QStringLiteral("completed"), Qt::CaseInsensitive) != 0)
                continue;
            if (kind != QStringLiteral("SeepageAnalysis")
                && !lastType.contains(QStringLiteral("渗流"), Qt::CaseInsensitive)
                && !lastType.contains(QStringLiteral("seepage"), Qt::CaseInsensitive))
                continue;

            const QString uuid = object.value(QStringLiteral("analysisUuid")).toString().trimmed();
            const QString key = uuid.isEmpty()
                ? QDir::fromNativeSeparators(manifestInfo.absolutePath()).toCaseFolded()
                : uuid;
            const QString name = object.value(QStringLiteral("analysisName"))
                                     .toString(QStringLiteral("渗流分析"));

            QVariantMap newest;
            QDirIterator files(manifestInfo.absolutePath(), QDir::Files,
                               QDirIterator::Subdirectories);
            while (files.hasNext())
            {
                const QFileInfo info(files.next());
                static const QRegularExpression resultName(
                    QStringLiteral("^IIDISP(?:[-_].*)?\\.TXT$"),
                    QRegularExpression::CaseInsensitiveOption);
                if (!resultName.match(info.fileName()).hasMatch())
                    continue;
                const QString normalized = QDir::fromNativeSeparators(info.absoluteFilePath());
                if (normalized.contains(QStringLiteral("/计算结果/"), Qt::CaseInsensitive)
                    || normalized.contains(QStringLiteral("/post/"), Qt::CaseInsensitive))
                    continue;

                QString validationError;
                if (!validateSeepageResult(info.absoluteFilePath(), &validationError))
                    continue;

                if (newest.isEmpty()
                    || info.lastModified().toMSecsSinceEpoch()
                           > newest.value(QStringLiteral("modified")).toLongLong())
                {
                    newest.insert(QStringLiteral("uuid"), key);
                    newest.insert(QStringLiteral("name"), name);
                    newest.insert(QStringLiteral("path"), info.absoluteFilePath());
                    newest.insert(QStringLiteral("modified"),
                                  info.lastModified().toMSecsSinceEpoch());
                }
            }

            if (!newest.isEmpty())
            {
                const QVariantMap existing = newestByAnalysis.value(key);
                if (existing.isEmpty()
                    || newest.value(QStringLiteral("modified")).toLongLong()
                           > existing.value(QStringLiteral("modified")).toLongLong())
                    newestByAnalysis.insert(key, newest);
            }
        }
    }

    results.reserve(newestByAnalysis.size());
    for (auto it = newestByAnalysis.cbegin(); it != newestByAnalysis.cend(); ++it)
        results.push_back(it.value());
    std::sort(results.begin(), results.end(), [](const QVariantMap& left,
                                                 const QVariantMap& right)
    {
        return left.value(QStringLiteral("modified")).toLongLong()
               > right.value(QStringLiteral("modified")).toLongLong();
    });
    return results;
}

QString CaveSupportComputationPage::selectedSeepageResult(QString* analysisName) const
{
    const QVector<QVariantMap> results = availableSeepageResults();
    if (results.isEmpty())
    {
        if (analysisName)
            analysisName->clear();
        return {};
    }

    const QVariantMap newest = results.first();
    if (analysisName)
        *analysisName = newest.value(QStringLiteral("name")).toString();
    return newest.value(QStringLiteral("path")).toString();
}

bool CaveSupportComputationPage::validateSeepageResult(
    const QString& path, QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法读取工程中的渗流分析结果。"));
    QTextStream input(&file);
    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));

    const QString firstLine = input.readLine();
    QVector<int> headerValues;
    auto matches = QRegularExpression(QStringLiteral("[-+]?\\d+")).globalMatch(firstLine);
    while (matches.hasNext())
        headerValues.push_back(matches.next().captured().toInt());
    if (headerValues.size() < 3)
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));

    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));
    input.readLine();
    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));
    input.readLine();

    QSet<int> expectedNodeIds;
    if (auto* meshService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        vtkUnstructuredGrid* grid = meshService->getVtkUnstructuredGrid();
        if (grid)
        {
            vtkDataArray* nodeIds = grid->GetPointData()
                ? grid->GetPointData()->GetArray("node_id") : nullptr;
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("nodeId");
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("point_id");
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("global_node_id");
            for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
            {
                expectedNodeIds.insert(nodeIds
                    ? static_cast<int>(nodeIds->GetTuple1(pointId))
                    : static_cast<int>(pointId + 1));
            }
        }
    }

    QSet<int> resultNodeIds;
    while (!input.atEnd())
    {
        const QStringList values = input.readLine().simplified().split(
            QLatin1Char(' '), Qt::SkipEmptyParts);
        if (values.size() < 3)
            continue;
        bool idOk = false;
        bool headOk = false;
        bool pressureOk = false;
        const int nodeId = values.at(0).toInt(&idOk);
        values.at(1).toDouble(&headOk);
        values.at(2).toDouble(&pressureOk);
        if (idOk && headOk && pressureOk)
            resultNodeIds.insert(nodeId);
    }

    if (resultNodeIds.isEmpty())
        return fail(QStringLiteral("渗流分析结果中没有可用的水头和孔压数据。请重新计算对应的渗流工况。"));
    for (int nodeId : std::as_const(expectedNodeIds))
    {
        if (!resultNodeIds.contains(nodeId))
            return fail(QStringLiteral("最近完成的渗流分析与当前洞室网格不一致。"));
    }
    return true;
}

void CaveSupportComputationPage::refreshLiningStatus()
{
    if (!m_liningStatusLabel)
        return;
    if (!liningEnabled())
    {
        m_liningStatusLabel->setText(QStringLiteral("未启用。"));
        return;
    }
    m_liningStatusLabel->setText(
        QStringLiteral("请在本节点下的“衬砌内边界”和“衬砌外边界”中选择网格边或网格面。"));
}

void CaveSupportComputationPage::refreshGroundwaterStatus()
{
    if (!m_groundwaterStatusLabel)
        return;
    if (!m_groundwaterCheck || !m_groundwaterCheck->isChecked())
    {
        m_groundwaterStatusLabel->setText(QStringLiteral("未启用。"));
        return;
    }

    QString analysisName;
    const QString path = selectedSeepageResult(&analysisName);
    QString error;
    if (path.isEmpty())
    {
        m_groundwaterStatusLabel->setText(
            QStringLiteral("当前工程没有已完成且与当前网格一致的渗流分析。"));
        return;
    }
    if (!validateSeepageResult(path, &error))
    {
        m_groundwaterStatusLabel->setText(error);
        return;
    }
    m_groundwaterStatusLabel->setText(
        QStringLiteral("将自动使用“%1”的节点水头和孔压结果。")
            .arg(analysisName.isEmpty() ? QStringLiteral("渗流分析") : analysisName));
}

bool CaveSupportComputationPage::prepareAuxiliaryInputFiles(
    const QString& inputDirectory, QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    QDir directory(inputDirectory);
    if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
        return fail(QStringLiteral("无法准备洞室计算数据。"));

    // 清除旧计算可能遗留的数据。求解器内部文件由业务设置自动生成。
    QFile::remove(directory.filePath(QStringLiteral("SHUIMX.DAT")));
    QFile::remove(directory.filePath(QStringLiteral("IISEEP.DAT")));
    // 吊车梁材料集合由独立的“吊车梁区域”页面生成。

    if (groundwaterModeCode() == 0)
        return true;

    const QString sourcePath = selectedSeepageResult();
    if (sourcePath.isEmpty())
        return fail(QStringLiteral("当前工程没有可用的渗流结果，请先完成渗流分析。"));
    if (!validateSeepageResult(sourcePath, errorMessage))
        return false;

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法读取工程中的渗流分析结果。"));

    QTextStream input(&source);
    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));
    input.readLine(); // 结果文件头
    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));
    input.readLine(); // 标题
    if (input.atEnd())
        return fail(QStringLiteral("渗流分析结果不完整。请重新计算对应的渗流工况。"));
    input.readLine(); // 变量说明

    // 节点编号 -> (水头, 孔压)。使用有序映射保证导出稳定，且自动去除重复行。
    QMap<int, QPair<double, double>> nodeValues;
    while (!input.atEnd())
    {
        const QStringList values = input.readLine().simplified().split(
            QLatin1Char(' '), Qt::SkipEmptyParts);
        if (values.size() < 3)
            continue;
        bool idOk = false;
        bool headOk = false;
        bool pressureOk = false;
        const int nodeId = values.at(0).toInt(&idOk);
        const double head = values.at(1).toDouble(&headOk);
        const double pressure = values.at(2).toDouble(&pressureOk);
        if (idOk && headOk && pressureOk && nodeId > 0)
            nodeValues.insert(nodeId, qMakePair(head, pressure));
    }
    if (nodeValues.isEmpty())
        return fail(QStringLiteral("渗流分析没有可用于洞室计算的节点水头和孔压。"));

    QVector<int> outputNodeIds;
    if (auto* meshService =
            ServiceRegistry::instance().getService<MeshModuleControllerService>())
    {
        vtkUnstructuredGrid* grid = meshService->getVtkUnstructuredGrid();
        if (grid)
        {
            vtkDataArray* nodeIds = grid->GetPointData()
                ? grid->GetPointData()->GetArray("node_id") : nullptr;
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("nodeId");
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("point_id");
            if (!nodeIds && grid->GetPointData())
                nodeIds = grid->GetPointData()->GetArray("global_node_id");
            outputNodeIds.reserve(static_cast<int>(grid->GetNumberOfPoints()));
            for (vtkIdType pointId = 0; pointId < grid->GetNumberOfPoints(); ++pointId)
            {
                const int nodeId = nodeIds
                    ? static_cast<int>(nodeIds->GetTuple1(pointId))
                    : static_cast<int>(pointId + 1);
                if (!nodeValues.contains(nodeId))
                    return fail(QStringLiteral("渗流分析与当前洞室网格不一致。"));
                outputNodeIds.push_back(nodeId);
            }
        }
    }
    if (outputNodeIds.isEmpty())
        outputNodeIds = nodeValues.keys().toVector();

    QSaveFile output(directory.filePath(QStringLiteral("IISEEP.DAT")));
    if (!output.open(QIODevice::WriteOnly | QIODevice::Text))
        return fail(QStringLiteral("无法准备洞室地下水计算数据。"));
    QTextStream stream(&output);
    stream << outputNodeIds.size() << "     NUMNP\n";
    stream << "     JD              HEAD              PCON\n";
    for (int nodeId : std::as_const(outputNodeIds))
    {
        const QPair<double, double> value = nodeValues.value(nodeId);
        stream << QStringLiteral("%1 %2 %3\n")
                      .arg(nodeId, 10)
                      .arg(value.first, 20, 'g', 15)
                      .arg(value.second, 20, 'g', 15);
    }
    stream.flush();
    if (stream.status() != QTextStream::Ok || !output.commit())
        return fail(QStringLiteral("无法保存洞室地下水计算数据。"));
    return true;
}


void CaveSupportComputationPage::onReinforcedTaskSelectionChanged()
{
    if (!m_isUpdatingUi)
        updateStaticDependenciesUi();
}

void CaveSupportComputationPage::updateAnalysisModeUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int typeCode = calculationTypeCode();
    const bool showStatic = (typeCode == 1 || typeCode == 3);
    const bool showDynamic = (typeCode == 2 || typeCode == 3);

    for (QWidget* group : {ui->groupStaticControl, ui->groupExcavationTime,
                           ui->groupParams})
    {
        if (group)
        {
            group->setVisible(true);
            group->setEnabled(showStatic);
        }
    }
    if (m_liningGroundwaterGroup)
        m_liningGroundwaterGroup->setVisible(showStatic);
    if (m_craneBeamGroup)
        m_craneBeamGroup->setVisible(showStatic);

    if (ui->groupDynamicControl)
        ui->groupDynamicControl->setVisible(showDynamic);
    if (ui->groupDyna)
        ui->groupDyna->setVisible(showDynamic);
    if (m_quietBoundaryStatus)
        m_quietBoundaryStatus->setText(m_quietBoundarySelection->summary());

    updateDynamicDetailsUi();
    updateStaticDependenciesUi();
}


void CaveSupportComputationPage::updateDynamicDetailsUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const int typeCode = calculationTypeCode();
    const bool dynamicVisible = (typeCode == 2 || typeCode == 3);
    const bool showTimeHistory = dynamicVisible && timeHistoryEnabled();

    ui->labelDt->setVisible(showTimeHistory);
    ui->widgetTimeParams->setVisible(showTimeHistory);
    ui->labelTsta->setVisible(showTimeHistory);
    ui->spinTsta->setVisible(showTimeHistory);
    ui->labelTend->setVisible(showTimeHistory);
    ui->spinTend->setVisible(showTimeHistory);
    ui->labelWaveFiles->hide();
    ui->widgetWaveFiles->hide();
}

void CaveSupportComputationPage::updateStaticDependenciesUi()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);

    const bool showStatic = calculationTypeCode() == 1 || calculationTypeCode() == 3;
    const bool support = showStatic && reinforcedTaskEnabled();
    if (m_reinforcementFieldPage)
    {
        m_reinforcementFieldPage->setVisible(support);
        m_reinforcementFieldPage->setEnabled(support);
    }

    const bool lining = showStatic && liningEnabled();
    const bool groundwater = showStatic && groundwaterModeCode() != 0;
    if (m_liningEnabledCheck)
        m_liningEnabledCheck->setEnabled(showStatic);
    if (m_liningStatusLabel)
        m_liningStatusLabel->setVisible(lining);
    if (m_groundwaterCheck)
        m_groundwaterCheck->setEnabled(showStatic && liningEnabled());
    if (m_groundwaterStatusLabel)
        m_groundwaterStatusLabel->setVisible(groundwater);

    // 业务设置自动映射为求解器控制码。
    if (m_ifSeepCombo)
    {
        const QSignalBlocker blocker(m_ifSeepCombo);
        const int index = m_ifSeepCombo->findData(
            QString::number(groundwaterModeCode()));
        if (index >= 0)
            m_ifSeepCombo->setCurrentIndex(index);
    }
    if (m_ifChenCombo)
    {
        const QSignalBlocker blocker(m_ifChenCombo);
        const int index = m_ifChenCombo->findData(lining ? QStringLiteral("1")
                                                         : QStringLiteral("0"));
        if (index >= 0)
            m_ifChenCombo->setCurrentIndex(index);
    }
    if (m_jaMethodCombo)
    {
        const QSignalBlocker blocker(m_jaMethodCombo);
        const int index = m_jaMethodCombo->findData(lining ? QStringLiteral("2")
                                                           : QStringLiteral("1"));
        if (index >= 0)
            m_jaMethodCombo->setCurrentIndex(index);
    }

    refreshLiningStatus();
    refreshGroundwaterStatus();
    refreshCraneBeamUi();
}


bool CaveSupportComputationPage::validateInputs(QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    const int type = calculationTypeCode();
    if (m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
        && (type == 2 || type == 3)
        && !m_quietBoundarySelection->generate())
        return fail(QStringLiteral("请在模型中选择粘弹性边界区域；当前支持线性边界面（二维选择边）。"));
    if (type == 1 || type == 3)
    {
        const bool hasTask =
            m_ifEallCombo->currentData().toString() == QStringLiteral("1")
            || m_ifJallCombo->currentData().toString() == QStringLiteral("1")
            || m_ifE1toaCombo->currentData().toString() == QStringLiteral("1")
            || m_ifJ1toaCombo->currentData().toString() == QStringLiteral("1");
        if (!hasTask)
            return fail(QStringLiteral("至少需要选择一种洞室开挖任务。"));

        if (m_reinforcementFieldPage
            && m_reinforcementFieldPage->mode()
                   != CaveReinforcementFieldPage::Mode::Disabled
            && !reinforcedTaskEnabled())
        {
            return fail(QStringLiteral("已设置围岩等效加固，但没有启用加固开挖任务。"));
        }

        if (liningEnabled())
        {
            const bool hasLiningBase =
                m_ifEallCombo->currentData().toString() == QStringLiteral("1")
                || m_ifE1toaCombo->currentData().toString() == QStringLiteral("1");
            if (!hasLiningBase)
                return fail(QStringLiteral("衬砌计算缺少基准开挖工况，请启用一种不加固开挖任务。"));
        }

        if (groundwaterModeCode() != 0)
        {
            if (!liningEnabled())
                return fail(QStringLiteral("地下水影响当前用于衬砌内力计算，请先启用衬砌计算。"));
            const QString path = selectedSeepageResult();
            if (path.isEmpty())
                return fail(QStringLiteral("当前工程没有可用的渗流结果，请先完成渗流分析。"));
            QString seepageError;
            if (!validateSeepageResult(path, &seepageError))
                return fail(seepageError);
        }

        if (craneBeamEnabled())
        {
            const int stage = craneBeamInstallStage();
            if (stage < 1 || stage > excavationLevelCount())
                return fail(QStringLiteral("吊车梁安装阶段必须位于当前开挖级范围内。"));
            const double release = craneBeamReleaseFactor();
            if (release < 0.0 || release > 1.0)
                return fail(QStringLiteral("吊车梁围岩荷载释放系数必须在 0～1 之间。"));
        }

        for (int row = 0; row < m_excavationDaysTable->rowCount(); ++row)
        {
            const auto* item = m_excavationDaysTable->item(row, 1);
            bool ok = false;
            const double days = item ? item->text().trimmed().toDouble(&ok) : 0.0;
            if (!ok || days < 0.0)
                return fail(QStringLiteral("第 %1 级开挖用时必须为非负数。")
                                .arg(row + 1));
        }
    }

    if ((type == 2 || type == 3) && timeHistoryEnabled())
    {
        if (m_dtSpin->value() <= 0.0)
            return fail(QStringLiteral("时程分析的时间步长必须为正数。"));
        if (m_tendSpin->value() <= m_tstaSpin->value())
            return fail(QStringLiteral("时程分析的终止时刻必须大于起始时刻。"));
    }
    return true;
}


QString CaveSupportComputationPage::sectionText()
{
    saveCurrentCase();
    QString text;
    const int iexcaMax = m_iexcaMaxSpin ? m_iexcaMaxSpin->value() : 1;
    text += QString("%1     IEXCA_MAX\n").arg(iexcaMax, 4);
    for (int i = 0; i < iexcaMax; ++i)
    {
        const QString dayValue =
            m_excavationDaysTable && m_excavationDaysTable->item(i, 1)
                ? m_excavationDaysTable->item(i, 1)->text().trimmed()
                : "0";
        text += QString("%1    %2    EXCA DATE  II \n").arg(dayValue.isEmpty() ? "0" : dayValue, 8).arg(i + 1, 8);
    }
    text += "0     NCHANGE\n";
    text += "1    MCASE\n";
    text += "1        1   JISUAN CASE  II\n";
    text += "1    NCASE\n";
    text += "--------------------------------------------------------------------\n";

    const CaseData& c = m_caseData;
    text += QString("%1    ICASE\n").arg(c.icase);
    text += "0    NCHANGE\n";
    text += QString("%1    %2    IEXCA_STA    IEXCA_END\n").arg(c.iexcaSta, c.iexcaEnd);
    text += QString("%1    %2    IF_EALL    IF_JALL\n").arg(c.ifEall, c.ifJall);
    text += QString("%1    %2    %3    IF_E1TOA    IF_J1TOA    JA_METHOD\n")
                .arg(c.ifE1toa, c.ifJ1toa, c.jaMethod);
    text += QString("%1    %2    IF_CHEN    IDAY_CASE\n").arg(c.ifChen, c.idayCase);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString stateSubdirectory=CaveExcavationProtocol::dynamicStateSubdirectory(
        liningEnabled(),c.ifJ1toa==QStringLiteral("1"),c.ifE1toa==QStringLiteral("1"),
        c.ifJall==QStringLiteral("1"),iexcaMax,craneBeamEnabled());
    QString caseDir = QDir(SolverFilePaths::outputRoot(projectDir)).filePath(stateSubdirectory);
    caseDir = QDir::toNativeSeparators(caseDir) + QDir::separator();
    text += QString("'%1'     DIRDYNA\n").arg(caseDir);
    text += QString("%1     %2   IFMODE   IFWAVE\n")
                .arg(QString::number(modalCount()),
                     m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : "0");
    text += QString("%1    %2   %3    DT TSTA TEND\n")
                .arg(m_dtSpin ? m_dtSpin->value() : 0.005, 0, 'f', 6)
                .arg(m_tstaSpin ? m_tstaSpin->value() : 0.0, 0, 'f', 6)
                .arg(m_tendSpin ? m_tendSpin->value() : 30.0, 0, 'f', 6);
    text += "'WAVE-AX.DAT'  'WAVE-AY.DAT'  'WAVE-AZ.DAT'   AX  AY  AZ\n";
    return text;
}

bool CaveSupportComputationPage::exportICaveDatFile(
    const QString& supportCaseSection) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(wd.filePath(QStringLiteral("ICAVE.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);
    const QHash<int, QString> fixedValues = {
        {1, QDir::toNativeSeparators(inputDir.absolutePath()) + QDir::separator()},
        {2, QStringLiteral("MAFACTOR.DAT")},
        {4, QStringLiteral("SHUIMX.DAT")},
        {6, QStringLiteral("IIJX_CHEN.DAT")},
        {8, QStringLiteral("IIJX_WAI.DAT")},
        {9, QStringLiteral("MASET_DCL.DAT")},
        {12, QStringLiteral("1")},
        {13, QStringLiteral("IISEEP.DAT")},
        {14, QDir::toNativeSeparators(wd.filePath(QStringLiteral("GEOSTR")))
                 + QDir::separator()},
        {15, QStringLiteral("JDTRACE.DAT")},
        {16, QStringLiteral("KETRACE.DAT")},
        {17, QStringLiteral("TDTRACE.DAT")},
        {27, QStringLiteral("FA_DYNA.DAT")},
        {28, QStringLiteral("---")},
        {29, QStringLiteral("---")},
        {30, QStringLiteral("---")}};

    const QSet<int> quotedFields = {1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14,
                                    15, 16, 17, 18, 19, 20, 21, 22, 23,
                                    24, 25, 26, 27, 28, 29, 30};
    for (int fieldNo = 1; fieldNo <= 30; ++fieldNo)
    {
        const QString value = fixedValues.contains(fieldNo)
            ? fixedValues.value(fieldNo) : controlFieldValue(fieldNo);
        if (quotedFields.contains(fieldNo))
            os << "'" << value << "'      " << fieldNo << "\n";
        else
            os << " " << value << "      " << fieldNo << "\n";
    }

    if (!supportCaseSection.trimmed().isEmpty())
        os << supportCaseSection.trimmed() << "\n";
    return true;
}


QJsonObject CaveSupportComputationPage::saveJson()
{
    saveCurrentCase();
    QJsonObject obj;
    obj["quiet_boundary_selection"]=m_quietBoundarySelection?m_quietBoundarySelection->save():QJsonObject();
    obj[QStringLiteral("quiet_boundary_enabled")] =
        m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked();
    obj[QStringLiteral("ncase")] = 1;
    obj[QStringLiteral("current_case")] = 0;
    obj[QStringLiteral("iexca_max")] = excavationLevelCount();

    QJsonArray excavationDays;
    for (int i = 0; i < excavationLevelCount(); ++i)
    {
        const QString dayValue =
            m_excavationDaysTable && m_excavationDaysTable->item(i, 1)
                ? m_excavationDaysTable->item(i, 1)->text().trimmed()
                : QStringLiteral("0");
        excavationDays.append(dayValue.isEmpty() ? QStringLiteral("0") : dayValue);
    }
    obj[QStringLiteral("excavation_days")] = excavationDays;
    obj[QStringLiteral("ifmode")] = QString::number(modalCount());
    obj[QStringLiteral("ifwave")] =
        m_ifWaveCombo ? m_ifWaveCombo->currentData().toString() : QStringLiteral("0");
    obj[QStringLiteral("dt")] = m_dtSpin ? m_dtSpin->value() : 0.005;
    obj[QStringLiteral("tsta")] = m_tstaSpin ? m_tstaSpin->value() : 0.0;
    obj[QStringLiteral("tend")] = m_tendSpin ? m_tendSpin->value() : 30.0;

    QJsonObject one;
    one[QStringLiteral("icase")] = m_caseData.icase;
    one[QStringLiteral("iexca_sta")] = m_caseData.iexcaSta;
    one[QStringLiteral("iexca_end")] = m_caseData.iexcaEnd;
    one[QStringLiteral("if_eall")] = m_caseData.ifEall;
    one[QStringLiteral("if_jall")] = m_caseData.ifJall;
    one[QStringLiteral("if_e1toa")] = m_caseData.ifE1toa;
    one[QStringLiteral("if_j1toa")] = m_caseData.ifJ1toa;
    one[QStringLiteral("ja_method")] = m_caseData.jaMethod;
    one[QStringLiteral("if_chen")] = m_caseData.ifChen;
    one[QStringLiteral("iday_case")] = m_caseData.idayCase;
    QJsonArray cases;
    cases.append(one);
    obj[QStringLiteral("cases")] = cases;

    const QStringList fixed = {QStringLiteral("1"), QStringLiteral("2"),
        QStringLiteral("4"), QStringLiteral("6"), QStringLiteral("8"),
        QStringLiteral("9"), QStringLiteral("12"), QStringLiteral("13"),
        QStringLiteral("14"), QStringLiteral("15"), QStringLiteral("16"),
        QStringLiteral("17")};
    for (int i = 1; i <= 30; ++i)
    {
        if (!fixed.contains(QString::number(i)))
            obj[QStringLiteral("line_%1").arg(i)] = controlFieldValue(i);
    }

    obj[QStringLiteral("lining_enabled")] = liningEnabled();
    obj[QStringLiteral("groundwater_enabled")] = groundwaterModeCode() != 0;
    obj[QStringLiteral("crane_beam_enabled")] = craneBeamEnabled();
    obj[QStringLiteral("crane_beam_install_stage")] = craneBeamInstallStage();
    obj[QStringLiteral("crane_beam_release_factor")] = craneBeamReleaseFactor();
    if (m_reinforcementFieldPage)
        obj[QStringLiteral("reinforcement_field")] =
            m_reinforcementFieldPage->saveJson();
    return obj;
}


void CaveSupportComputationPage::loadJson(const QJsonObject& obj)
{
    m_quietBoundarySelection->load(obj.value("quiet_boundary_selection").toObject(),obj.value("quiet_boundary_enabled").toBool(false));
    m_isUpdatingUi = true;
    m_caseData = CaseData();
    if (m_excavationDaysTable)
        m_excavationDaysTable->setUpdatesEnabled(false);

    const int iexcaMax = qMax(1, obj.value(QStringLiteral("iexca_max")).toInt(1));
    {
        const QSignalBlocker blocker(m_iexcaMaxSpin);
        m_iexcaMaxSpin->setValue(iexcaMax);
    }
    onExcavationLevelCountChanged(iexcaMax);
    const QJsonArray excavationDays =
        obj.value(QStringLiteral("excavation_days")).toArray();
    for (int i = 0; i < excavationDays.size() && i < iexcaMax; ++i)
    {
        if (!m_excavationDaysTable->item(i, 1))
            m_excavationDaysTable->setItem(i, 1, new QTableWidgetItem(QStringLiteral("0")));
        m_excavationDaysTable->item(i, 1)->setText(
            excavationDays.at(i).toString(QStringLiteral("0")));
    }

    auto setCombo = [](QComboBox* combo, const QJsonObject& source,
                       const QString& key, const QString& fallback)
    {
        if (!combo)
            return;
        const int index = combo->findData(source.value(key).toString(fallback));
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    m_modalCountSpin->setValue(obj.value(QStringLiteral("ifmode")).toVariant().toInt());
    setCombo(m_ifWaveCombo, obj, QStringLiteral("ifwave"), QStringLiteral("0"));
    m_dtSpin->setValue(obj.value(QStringLiteral("dt")).toDouble(0.005));
    m_tstaSpin->setValue(obj.value(QStringLiteral("tsta")).toDouble(0.0));
    m_tendSpin->setValue(obj.value(QStringLiteral("tend")).toDouble(30.0));
    m_accXEdit->setText(QStringLiteral("WAVE-AX.DAT"));
    m_accYEdit->setText(QStringLiteral("WAVE-AY.DAT"));
    m_accZEdit->setText(QStringLiteral("WAVE-AZ.DAT"));

    const QJsonArray cases = obj.value(QStringLiteral("cases")).toArray();
    if (!cases.isEmpty())
    {
        const QJsonObject one = cases.at(0).toObject();
        m_caseData.icase = 1;
        m_caseData.iexcaSta = one.value(QStringLiteral("iexca_sta"))
                                  .toString(m_caseData.iexcaSta);
        m_caseData.iexcaEnd = one.value(QStringLiteral("iexca_end"))
                                  .toString(m_caseData.iexcaEnd);
        m_caseData.ifEall = one.value(QStringLiteral("if_eall"))
                                .toString(m_caseData.ifEall);
        m_caseData.ifJall = one.value(QStringLiteral("if_jall"))
                                .toString(m_caseData.ifJall);
        m_caseData.ifE1toa = one.value(QStringLiteral("if_e1toa"))
                                 .toString(m_caseData.ifE1toa);
        m_caseData.ifJ1toa = one.value(QStringLiteral("if_j1toa"))
                                 .toString(m_caseData.ifJ1toa);
        m_caseData.jaMethod = one.value(QStringLiteral("ja_method"))
                                  .toString(m_caseData.jaMethod);
        m_caseData.ifChen = one.value(QStringLiteral("if_chen"))
                                .toString(m_caseData.ifChen);
        m_caseData.idayCase = one.value(QStringLiteral("iday_case"))
                                 .toString(m_caseData.idayCase);
    }

    loadLegacyCaveJson(obj);
    if (obj.contains(QStringLiteral("quiet_boundary_enabled")) && m_quietBoundaryCheck)
        m_quietBoundaryCheck->setChecked(
            obj.value(QStringLiteral("quiet_boundary_enabled")).toBool(false));

    const bool legacyLining = m_caseData.ifChen == QStringLiteral("1");
    const QString legacySeepCode =
        obj.value(QStringLiteral("line_3")).toString(controlFieldValue(3));
    const bool legacyGroundwater = !legacySeepCode.trimmed().isEmpty()
        && legacySeepCode.trimmed() != QStringLiteral("0");
    const bool lining = obj.value(QStringLiteral("lining_enabled")).toBool(legacyLining);
    const bool groundwater =
        obj.value(QStringLiteral("groundwater_enabled")).toBool(legacyGroundwater);
    {
        const QSignalBlocker blocker(m_liningEnabledCheck);
        m_liningEnabledCheck->setChecked(lining || groundwater);
    }
    {
        const QSignalBlocker blocker(m_groundwaterCheck);
        m_groundwaterCheck->setChecked(groundwater);
    }

    m_legacyCraneBeamAreaState = QJsonObject();
    const QJsonArray legacyCraneGroups =
        obj.value(QStringLiteral("crane_beam_groups")).toArray();
    if (!legacyCraneGroups.isEmpty())
        m_legacyCraneBeamAreaState[QStringLiteral("crane_beam_groups")] = legacyCraneGroups;

    bool legacyInstallOk = false;
    const int legacyInstall =
        obj.value(QStringLiteral("line_10"))
            .toString(controlFieldValue(10)).trimmed().toInt(&legacyInstallOk);
    bool legacyReleaseOk = false;
    const double legacyRelease =
        obj.value(QStringLiteral("line_11"))
            .toString(controlFieldValue(11)).trimmed().toDouble(&legacyReleaseOk);
    const bool craneEnabled = obj.value(QStringLiteral("crane_beam_enabled"))
                                  .toBool(legacyInstallOk && legacyInstall < 0);
    {
        const QSignalBlocker blocker(m_craneBeamEnabledCheck);
        m_craneBeamEnabledCheck->setChecked(craneEnabled);
    }

    updateCraneBeamInstallStages();
    const int savedStage = obj.value(QStringLiteral("crane_beam_install_stage"))
                               .toInt(legacyInstallOk && legacyInstall < 0
                                          ? qAbs(legacyInstall) : 1);
    if (m_craneBeamInstallStageCombo)
    {
        const int stageIndex = m_craneBeamInstallStageCombo->findData(
            qBound(1, savedStage, excavationLevelCount()));
        m_craneBeamInstallStageCombo->setCurrentIndex(stageIndex >= 0 ? stageIndex : 0);
    }
    if (m_craneBeamReleaseSpin)
    {
        m_craneBeamReleaseSpin->setValue(
            obj.value(QStringLiteral("crane_beam_release_factor"))
                .toDouble(legacyReleaseOk ? legacyRelease : 1.0));
    }

    if (m_reinforcementFieldPage)
    {
        const QJsonObject field =
            obj.value(QStringLiteral("reinforcement_field")).toObject();
        if (!field.isEmpty())
            m_reinforcementFieldPage->loadJson(field);
    }

    if (m_excavationDaysTable)
        m_excavationDaysTable->setUpdatesEnabled(true);
    m_isUpdatingUi = false;

    loadCaseToUi();
    refreshLiningStatus();
    refreshGroundwaterStatus();
    refreshCraneBeamUi();
    updateAnalysisModeUi();
}


void CaveSupportComputationPage::loadLegacyCaveJson(const QJsonObject& obj)
{
    const QStringList fixed = {"1", "2", "4", "6", "8", "9", "12", "13", "14", "15", "16", "17"};
    for (int i = 1; i <= 30; ++i)
    {
        if (fixed.contains(QString::number(i)))
            continue;
        const QString key = QString("line_%1").arg(i);
        if (obj.contains(key))
            setControlFieldValue(i, obj.value(key).toString(controlFieldValue(i)));
    }
}

void CaveSupportComputationPage::onExcavationLevelCountChanged(int count)
{
    if (!m_excavationDaysTable)
        return;

    const int target = qMax(1, count);
    const int oldRows = m_excavationDaysTable->rowCount();
    m_excavationDaysTable->setRowCount(target);
    for (int row = 0; row < target; ++row)
    {
        if (!m_excavationDaysTable->item(row, 0))
            m_excavationDaysTable->setItem(
                row, 0, new QTableWidgetItem(QString::number(row + 1)));
        else
            m_excavationDaysTable->item(row, 0)->setText(QString::number(row + 1));
        m_excavationDaysTable->item(row, 0)->setFlags(
            m_excavationDaysTable->item(row, 0)->flags() & ~Qt::ItemIsEditable);

        if (!m_excavationDaysTable->item(row, 1))
            m_excavationDaysTable->setItem(
                row, 1, new QTableWidgetItem(row < oldRows
                                                ? QStringLiteral("0")
                                                : QStringLiteral("0")));
    }
    SolverUiLayoutHelper::fitTableToRows(m_excavationDaysTable, target, 2, 6);

    if (!m_isUpdatingUi)
    {
        m_caseData.iexcaSta = QStringLiteral("-1");
        m_caseData.iexcaEnd = QString::number(-target);
        m_iexcaStaEdit->setText(m_caseData.iexcaSta);
        m_iexcaEndEdit->setText(m_caseData.iexcaEnd);
    }
    updateCraneBeamInstallStages();
    refreshLiningStatus();
    refreshCraneBeamUi();
}


QString CaveSupportComputationPage::controlFieldValue(int fieldNo) const
{
    auto comboValue = [](QComboBox* combo, const QString& fallback)
    {
        if (!combo)
            return fallback;
        const QString value = combo->currentData().toString().trimmed();
        return value.isEmpty() ? fallback : value;
    };

    switch (fieldNo)
    {
    case 3:
        return comboValue(m_ifSeepCombo, QStringLiteral("0"));
    case 18:
        return comboValue(m_dampingModeCombo, QStringLiteral("0"));
    case 22:
        return comboValue(m_calculationTypeCombo, QStringLiteral("1"));
    case 23:
        return comboValue(m_excavationLoadMethodCombo, QStringLiteral("2"));
    case 26:
        return m_quietBoundaryCheck && m_quietBoundaryCheck->isChecked()
            && (calculationTypeCode() == 2 || calculationTypeCode() == 3)
            ? QStringLiteral("1") : QStringLiteral("0");
    case 24:
        return QStringLiteral("%1 %2")
            .arg(comboValue(m_iterationMethodCombo, QStringLiteral("1")))
            .arg(m_iterationCountSpin ? m_iterationCountSpin->value() : 100);
    default:
        break;
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("line%1Edit").arg(fieldNo)))
        return edit->text().trimmed();
    return QString();
}

void CaveSupportComputationPage::setControlFieldValue(int fieldNo, const QString& value)
{
    auto setCombo = [](QComboBox* combo, const QString& code)
    {
        if (!combo)
            return;
        const int index = combo->findData(code.trimmed());
        if (index >= 0)
            combo->setCurrentIndex(index);
    };

    switch (fieldNo)
    {
    case 3:
        setCombo(m_ifSeepCombo,
                 value.trimmed() == QStringLiteral("0")
                     ? QStringLiteral("0") : QStringLiteral("3"));
        return;
    case 18:
        setCombo(m_dampingModeCombo, value);
        return;
    case 22:
        setCombo(m_calculationTypeCombo, value);
        return;
    case 23:
        setCombo(m_excavationLoadMethodCombo, value);
        return;
    case 26:
        if (m_quietBoundaryCheck)
            m_quietBoundaryCheck->setChecked(value.trimmed() == QStringLiteral("1"));
        return;
    case 24:
    {
        const QStringList parts = value.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        setCombo(m_iterationMethodCombo, parts.value(0, QStringLiteral("1")));
        bool ok = false;
        const int count = parts.value(1, QStringLiteral("100")).toInt(&ok);
        if (m_iterationCountSpin)
            m_iterationCountSpin->setValue(ok ? qMax(1, count) : 100);
        return;
    }
    default:
        break;
    }

    if (auto* edit = findChild<QLineEdit*>(QStringLiteral("line%1Edit").arg(fieldNo)))
        edit->setText(value);
}

void CaveSupportComputationPage::saveCurrentCase()
{
    CaseData& c = m_caseData;
    c.icase = 1;
    c.iexcaSta = QStringLiteral("-1");
    c.iexcaEnd = QString::number(-excavationLevelCount());
    c.ifEall = m_ifEallCombo->currentData().toString().trimmed();
    c.ifJall = m_ifJallCombo->currentData().toString().trimmed();
    c.ifE1toa = m_ifE1toaCombo->currentData().toString().trimmed();
    c.ifJ1toa = m_ifJ1toaCombo->currentData().toString().trimmed();
    c.jaMethod = liningEnabled() ? QStringLiteral("2") : QStringLiteral("1");
    c.ifChen = liningEnabled() ? QStringLiteral("1") : QStringLiteral("0");
    c.idayCase = m_idayCaseEdit->text().trimmed();
    if (c.idayCase.isEmpty())
        c.idayCase = QStringLiteral("0");

    m_iexcaStaEdit->setText(c.iexcaSta);
    m_iexcaEndEdit->setText(c.iexcaEnd);
    ui->line10Edit->setText(
        craneBeamEnabled() ? QString::number(-craneBeamInstallStage())
                           : QStringLiteral("0"));
    ui->line11Edit->setText(QString::number(craneBeamReleaseFactor(), 'g', 12));
    ui->line5Edit->setText(QStringLiteral("0.0"));
    ui->line7Edit->setText(QStringLiteral("0.0"));
}


void CaveSupportComputationPage::loadCaseToUi()
{
    m_isUpdatingUi = true;
    const CaseData& c = m_caseData;
    m_iexcaStaEdit->setText(QStringLiteral("-1"));
    m_iexcaEndEdit->setText(QString::number(-excavationLevelCount()));

    auto select = [](QComboBox* combo, const QString& value)
    {
        const int index = combo->findData(value);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    select(m_ifEallCombo, c.ifEall);
    select(m_ifJallCombo, c.ifJall);
    select(m_ifE1toaCombo, c.ifE1toa);
    select(m_ifJ1toaCombo, c.ifJ1toa);
    select(m_ifChenCombo, liningEnabled() ? QStringLiteral("1") : QStringLiteral("0"));
    select(m_jaMethodCombo, liningEnabled() ? QStringLiteral("2") : QStringLiteral("1"));
    m_idayCaseEdit->setText(c.idayCase);

    m_isUpdatingUi = false;
    updateStaticDependenciesUi();
}


void CaveSupportComputationPage::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(this, QStringLiteral("请完善开挖与加固设置"), error);
        return;
    }
    emit computeRequested();
}

void CaveSupportComputationPage::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}
