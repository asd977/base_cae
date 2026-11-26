#include "SeepageWidget.h"

#include "SeepageBoundaryPage.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>

#include "ui_SeepageWidget.h"
#include "SolverUiLayoutHelper.h"

SeepageWidget::SeepageWidget(QWidget* parent)
    : BasePage("SeepageAnalysis"), ui(new Ui::SeepageWidget)
{
    ui->setupUi(this);
    SolverUiLayoutHelper::polishPage(this);
    SolverUiLayoutHelper::setupComputeControls(this);

    ui->comboComputeType->addItem(QStringLiteral("稳定渗流"), 1);
    ui->comboComputeType->addItem(QStringLiteral("非稳定渗流"), 2);
    ui->comboComputeType->addItem(QStringLiteral("稳定 + 非稳定"), 3);
    ui->comboItagFree->addItem(QStringLiteral("不继承天然水头"), 0);
    ui->comboItagFree->addItem(QStringLiteral("继承天然水头"), 1);

    connect(ui->btnCompute, &QPushButton::clicked,
            this, &SeepageWidget::onComputeClicked);
    connect(ui->comboComputeType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int)
            {
                updateComputeTypeVisibility();
                syncBoundaryTransientSettings();
            });
    connect(ui->checkNaturalFreeSurface, &QCheckBox::toggled, this,
            [this](bool)
            {
                updateOptionalControls();
                emit requirementsChanged();
            });
    connect(ui->checkDrainWell, &QCheckBox::toggled, this,
            [this](bool) { emit requirementsChanged(); });

    auto transientChanged = [this](double)
    {
        syncBoundaryTransientSettings();
    };
    connect(ui->spinTsta, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, transientChanged);
    connect(ui->spinTend, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, transientChanged);

    updateComputeTypeVisibility();
    updateOptionalControls();
    updateBoundarySummary();
}

SeepageWidget::~SeepageWidget()
{
    delete ui;
}

void SeepageWidget::beforeLoad()
{
    updateBoundarySummary();
    syncBoundaryTransientSettings();
}

void SeepageWidget::beforeUnload()
{
}

void SeepageWidget::setBoundaryPages(SeepageBoundaryPage* upstream,
                                     SeepageBoundaryPage* downstream)
{
    upstreamBoundaryPage_ = upstream;
    downstreamBoundaryPage_ = downstream;

    if (legacyHydraulicStateLoaded_)
    {
        if (upstreamBoundaryPage_)
            upstreamBoundaryPage_->setLegacyHydraulicState(
                legacyUpstreamWaterLevel_, legacyUpstreamFlux_, legacyUpstreamCurve_);
        if (downstreamBoundaryPage_)
            downstreamBoundaryPage_->setLegacyHydraulicState(
                legacyDownstreamWaterLevel_, legacyDownstreamFlux_, legacyDownstreamCurve_);
    }

    auto connectBoundary = [this](SeepageBoundaryPage* page)
    {
        if (!page)
            return;
        connect(page, &SeepageBoundaryPage::selectionCountChanged,
                this, [this](int) { updateBoundarySummary(); });
        connect(page, &SeepageBoundaryPage::hydraulicParametersChanged,
                this, [this]() { updateBoundarySummary(); });
    };
    connectBoundary(upstreamBoundaryPage_);
    connectBoundary(downstreamBoundaryPage_);

    syncBoundaryTransientSettings();
    updateBoundarySummary();
}

double SeepageWidget::upstreamWaterLevel() const
{
    return upstreamBoundaryPage_ ? upstreamBoundaryPage_->waterLevel()
                                 : legacyUpstreamWaterLevel_;
}

double SeepageWidget::downstreamWaterLevel() const
{
    return downstreamBoundaryPage_ ? downstreamBoundaryPage_->waterLevel()
                                   : legacyDownstreamWaterLevel_;
}

double SeepageWidget::upstreamFlux() const
{
    return upstreamBoundaryPage_ ? upstreamBoundaryPage_->effectiveFlux()
                                 : legacyUpstreamFlux_;
}

double SeepageWidget::downstreamFlux() const
{
    return downstreamBoundaryPage_ ? downstreamBoundaryPage_->effectiveFlux()
                                   : legacyDownstreamFlux_;
}

bool SeepageWidget::naturalFreeSurfaceEnabled() const
{
    return ui->checkNaturalFreeSurface->isChecked();
}

bool SeepageWidget::drainWellEnabled() const
{
    return ui->checkDrainWell->isChecked();
}

int SeepageWidget::computeType() const
{
    return ui->comboComputeType->currentData().toInt();
}

bool SeepageWidget::validateInputs(QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!upstreamBoundaryPage_ || !downstreamBoundaryPage_)
        return fail(QStringLiteral("请先设置上下游边界。"));

    // 求解水头场至少需要一个水位（Dirichlet）边界作为基准。
    // 允许另一侧使用定通量（Neumann）边界，但不允许两侧都只给通量。
    if (!upstreamBoundaryPage_->usesWaterLevel()
        && !downstreamBoundaryPage_->usesWaterLevel())
        return fail(QStringLiteral("至少保留一个水位边界。"));

    const int type = ui->comboComputeType->currentData().toInt();
    const bool transient = type == 2 || type == 3;
    const double start = ui->spinTsta->value();
    const double end = ui->spinTend->value();

    if (transient)
    {
        const double dt = ui->spinDt->value();
        if (!std::isfinite(dt) || dt <= 0.0)
            return fail(QStringLiteral("非稳态渗流的时间步长必须为正数。"));
        if (!std::isfinite(start) || !std::isfinite(end) || end <= start)
            return fail(QStringLiteral("非稳态渗流的终止时间必须大于起始时间。"));
    }

    QString boundaryError;
    if (!upstreamBoundaryPage_->validateHydraulicInputs(
            transient, start, end, &boundaryError))
        return fail(boundaryError);
    if (!downstreamBoundaryPage_->validateHydraulicInputs(
            transient, start, end, &boundaryError))
        return fail(boundaryError);

    const QVector<int> upstreamIds = upstreamBoundaryPage_->selectedNodeIds();
    const QVector<int> downstreamIds = downstreamBoundaryPage_->selectedNodeIds();
    if (!upstreamIds.isEmpty() && !downstreamIds.isEmpty())
    {
        QSet<int> upstreamSet;
        upstreamSet.reserve(upstreamIds.size());
        for (int id : upstreamIds)
            upstreamSet.insert(id);
        int duplicateCount = 0;
        for (int id : downstreamIds)
        {
            if (upstreamSet.contains(id))
                ++duplicateCount;
        }
        if (duplicateCount > 0)
        {
            return fail(QStringLiteral("上下游边界有 %1 个重复节点，请重新分配。")
                            .arg(duplicateCount));
        }
    }
    return true;
}

void SeepageWidget::updateComputeTypeVisibility()
{
    SolverUiLayoutHelper::StableLayoutUpdate stableLayout(this);
    const int type = ui->comboComputeType->currentData().toInt();
    ui->groupBoxTransient->setVisible(type == 2 || type == 3);
    ui->groupBoxParameters->setTitle(
        type == 1 ? QStringLiteral("稳定渗流设置")
                  : type == 2 ? QStringLiteral("非稳定渗流设置")
                              : QStringLiteral("稳定与非稳定渗流设置"));
}

void SeepageWidget::updateOptionalControls()
{
    const bool enabled = naturalFreeSurfaceEnabled();
    ui->comboItagFree->setEnabled(enabled);
    if (!enabled && ui->comboItagFree->currentData().toInt() != 0)
    {
        const QSignalBlocker blocker(ui->comboItagFree);
        const int index = ui->comboItagFree->findData(0);
        if (index >= 0)
            ui->comboItagFree->setCurrentIndex(index);
    }
}

void SeepageWidget::syncBoundaryTransientSettings()
{
    const int type = ui->comboComputeType->currentData().toInt();
    const bool transient = type == 2 || type == 3;
    if (upstreamBoundaryPage_)
        upstreamBoundaryPage_->setTransientCurveEnabled(
            transient, ui->spinTsta->value(), ui->spinTend->value());
    if (downstreamBoundaryPage_)
        downstreamBoundaryPage_->setTransientCurveEnabled(
            transient, ui->spinTsta->value(), ui->spinTend->value());
}

void SeepageWidget::updateBoundarySummary()
{
    auto summary = [](SeepageBoundaryPage* page, const QString& title)
    {
        if (!page)
            return QStringLiteral("%1：未设置").arg(title);
        const int count = page->selectionCount();
        if (page->usesFlux())
        {
            const double value = page->effectiveFlux();
            const QString direction = value > 0.0 ? QStringLiteral("流出")
                                                   : value < 0.0 ? QStringLiteral("流入")
                                                                 : QStringLiteral("未设置");
            return QStringLiteral("%1：%2 个节点，定通量 %3 m/s（%4）")
                .arg(title).arg(count).arg(value, 0, 'g', 10).arg(direction);
        }
        return QStringLiteral("%1：%2 个节点，水位 %3 m")
            .arg(title).arg(count).arg(page->waterLevel(), 0, 'g', 10);
    };

    ui->labelUpstreamSummary->setText(summary(upstreamBoundaryPage_, QStringLiteral("上游")));
    ui->labelDownstreamSummary->setText(summary(downstreamBoundaryPage_, QStringLiteral("下游")));

    if (upstreamBoundaryPage_ && downstreamBoundaryPage_
        && upstreamBoundaryPage_->usesWaterLevel()
        && downstreamBoundaryPage_->usesWaterLevel())
    {
        const double difference = upstreamWaterLevel() - downstreamWaterLevel();
        ui->labelHeadDifferenceSummary->setText(
            difference >= 0.0
                ? QStringLiteral("上下游水头差：%1 m").arg(difference, 0, 'g', 10)
                : QStringLiteral("上下游水头差：%1 m（下游较高）")
                      .arg(difference, 0, 'g', 10));
    }
    else
    {
        ui->labelHeadDifferenceSummary->setText(
            QStringLiteral("水头场由水位与定通量共同确定。"));
    }
}

bool SeepageWidget::exportIseepDatFile(int computeTypeOverride) const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(wd.absolutePath()) || !QDir().mkpath(inputDir.absolutePath()))
        return false;

    auto ensureTrailingSep = [](const QString& path)
    {
        QString result = QDir::toNativeSeparators(path);
        if (!result.endsWith(QDir::separator()))
            result += QDir::separator();
        return result;
    };

    const QString inputDirAbs = ensureTrailingSep(inputDir.absolutePath());
    const QString seepCaseDirName = QStringLiteral("SEEPCASE01");
    const int computeType = computeTypeOverride > 0
        ? computeTypeOverride : ui->comboComputeType->currentData().toInt();
    QString dynamicInitialDir = wd.filePath(seepCaseDirName);
    if (computeType == 2)
    {
        dynamicInitialDir = transientInitialStateDirectory();
        if (dynamicInitialDir.isEmpty())
            return false;
    }

    // 稳态和非稳态结果目录均由求解器的 NEWDIR 创建。前端只清理旧结果；
    // 若预先建立空目录，GPU 求解器会因缺少 FLAG_DIR.DAT 而报告创建失败。
    QStringList solverCaseDirectories = {QStringLiteral("SEEPDYNA01")};
    if (computeType != 2)
        solverCaseDirectories.prepend(seepCaseDirName);
    for (const QString& directoryName : solverCaseDirectories)
    {
        QDir previousCaseDir(wd.filePath(directoryName));
        if (previousCaseDir.exists() && !previousCaseDir.removeRecursively())
            return false;
    }
    const QString dynamicInitialDirAbs = ensureTrailingSep(dynamicInitialDir);

    QFile file(wd.filePath(QStringLiteral("ISEEP.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os.setEncoding(QStringConverter::System);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);

    os << "'" << inputDirAbs << "'\n";
    os << "'JD_UPSEEP.DAT'\n";
    os << "'JD_DWSEEP.DAT'\n";
    os << "'" << ui->spinMainPropertyNo->value() << "'\n";
    os << "'" << ui->spinControlledPropertyNo->value() << "'\n";
    // Pure transient mode (computeType == 2) must not re-run SEEPFREE.
    const int ifFree = (computeType != 2 && ui->checkNaturalFreeSurface->isChecked()) ? 1 : 0;
    os << "'" << ifFree << "'\n";
    os << "'JDFREE.DAT'\n";
    os << "'FA_UPSEEP.DAT'\n";
    os << "'FA_DWSEEP.DAT'\n";
    os << "'" << (ui->checkDrainWell->isChecked() ? 1 : 0) << "'\n";
    os << "'JDJING.DAT'\n";
    os << "'JDTRACE.DAT'\n";
    os << "'KETRACE.DAT'\n";
    os << "'TDTRACE.DAT'\n";
    os << "'------------'\n'------------'\n'------------'\n'------------'\n";
    os << "'" << computeType << "'\n";
    os << "'------------'\n";
    os << "0     NCHANGE\n";
    os << "1     MCASE\n";
    os << "1 1   JISUAN CASE  II\n";
    os << "1     NCASE\n";
    os << "'------------'\n";
    os << "1     ICASE\n";
    os << "0     NCHANGE_CASE\n";
    // SOLVER transient SEEPIIWL allocates only NIC=2 (HWUP/HWDW); ITAG_FREE=1 in
    // transient/steady+transient causes SEEPIIID to index CONA(-(NUMJD+2)) out of bounds.
    const int itagFree = (computeType == 1 && naturalFreeSurfaceEnabled())
                             ? ui->comboItagFree->currentData().toInt() : 0;
    os << ui->spinIter->value() << " " << itagFree
       << " ITER  ITAG_FREE\n";
    os << upstreamWaterLevel() << " " << downstreamWaterLevel() << " "
       << upstreamFlux() << " " << downstreamFlux() << "    HWUP HWDW HQUP HQDW\n";
    os << "'" << dynamicInitialDirAbs << "'     DIRDYNA\n";
    os << ui->spinDt->value() << " " << ui->spinTsta->value() << " "
       << ui->spinTend->value() << "    DT TSTA TEND\n";
    os << "'HWDROP_UP.DAT'  'HWDROP_DW.DAT'   HWDROP_UP  HWDROP_DW\n";
    file.close();

    return exportHwDropDatFiles(seepCaseDirName) && exportTraceDatFiles();
}

QString SeepageWidget::transientInitialStateDirectory(const QString& workspaceRoot) const
{
    QString rootPath = workspaceRoot.trimmed();
    if (rootPath.isEmpty())
    {
        const QString projectDir =
            PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
        if (projectDir.isEmpty())
            return {};
        rootPath = SolverFilePaths::outputRoot(projectDir);
    }
    if (rootPath.trimmed().isEmpty())
        return {};

    const QDir currentWorkspace(rootPath);
    const QStringList localCaseDirectories = {
        currentWorkspace.filePath(QStringLiteral("SEEPCASE01")),
        currentWorkspace.filePath(QStringLiteral("计算结果/SEEPCASE01"))};
    for (const QString& localCaseDirectory : localCaseDirectories)
    {
        const QFileInfo initialStateFile(
            QDir(localCaseDirectory).filePath(QStringLiteral("IIDISP.TXT")));
        if (initialStateFile.exists() && initialStateFile.isFile()
            && initialStateFile.size() > 0)
            return QDir::cleanPath(localCaseDirectory);
    }
    return {};
}

bool SeepageWidget::exportHwDropDatFiles(const QString& seepCaseDirName) const
{
    Q_UNUSED(seepCaseDirName);
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    const double start = ui->spinTsta->value();
    const double end = ui->spinTend->value() > start
                           ? ui->spinTend->value() : start + 1.0;
    auto normalizedCurve = [start, end](QVector<QPair<double, double>> curve,
                                         double constantLevel)
    {
        if (curve.size() < 2)
            return QVector<QPair<double, double>>{
                qMakePair(start, constantLevel), qMakePair(end, constantLevel)};
        std::sort(curve.begin(), curve.end(),
                  [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });
        return curve;
    };

    auto writeCurve = [&](const QString& fileName,
                          const QVector<QPair<double, double>>& curve)
    {
        QFile file(inputDir.filePath(fileName));
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream os(&file);
        os.setRealNumberNotation(QTextStream::FixedNotation);
        os.setRealNumberPrecision(6);
        os << curve.size() << "\n";
        os << "'TDT(s)   HEAD(m)'\n";
        for (const auto& point : curve)
            os << point.first << " " << point.second << "\n";
        return true;
    };

    QVector<QPair<double, double>> upstreamCurve = upstreamBoundaryPage_
        ? upstreamBoundaryPage_->waterLevelCurve() : legacyUpstreamCurve_;
    QVector<QPair<double, double>> downstreamCurve = downstreamBoundaryPage_
        ? downstreamBoundaryPage_->waterLevelCurve() : legacyDownstreamCurve_;
    upstreamCurve = normalizedCurve(upstreamCurve, upstreamWaterLevel());
    downstreamCurve = normalizedCurve(downstreamCurve, downstreamWaterLevel());

    return writeCurve(QStringLiteral("HWDROP_UP.DAT"), upstreamCurve)
           && writeCurve(QStringLiteral("HWDROP_DW.DAT"), downstreamCurve);
}

bool SeepageWidget::exportTraceDatFiles() const
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    auto ensureEmptyIndexFile = [&inputDir](const QString& fileName)
    {
        const QString path = inputDir.filePath(fileName);
        if (QFile::exists(path))
            return true;
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        QTextStream os(&file);
        os << "0   NUM\n";
        return true;
    };
    if (!ensureEmptyIndexFile(QStringLiteral("JDTRACE.DAT"))
        || !ensureEmptyIndexFile(QStringLiteral("KETRACE.DAT")))
        return false;

    QVector<double> times;
    const int type = ui->comboComputeType->currentData().toInt();
    if (type == 2 || type == 3)
    {
        const double start = ui->spinTsta->value();
        const double end = ui->spinTend->value();
        const double dt = ui->spinDt->value();
        auto appendCurveTimes = [&](SeepageBoundaryPage* page)
        {
            if (!page || !page->usesWaterLevel())
                return;
            for (const auto& point : page->waterLevelCurve())
            {
                if (!std::isfinite(point.first) || point.first < start - 1.0e-9
                    || point.first > end + 1.0e-9)
                    continue;
                double time = point.first;
                if (dt > 0.0)
                    time = start + std::round((time - start) / dt) * dt;
                time = std::clamp(time, start, end);
                times.push_back(time);
            }
        };
        appendCurveTimes(upstreamBoundaryPage_);
        appendCurveTimes(downstreamBoundaryPage_);
        times.push_back(start);
        times.push_back(end);
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end(),
                                [](double lhs, double rhs)
                                { return std::abs(lhs - rhs) <= 1.0e-8; }),
                    times.end());
        if (times.size() > 100)
        {
            QVector<double> reduced;
            reduced.reserve(100);
            for (int i = 0; i < 100; ++i)
            {
                const int index = int(std::lround(
                    double(i) * double(times.size() - 1) / 99.0));
                reduced.push_back(times.at(index));
            }
            times = reduced;
        }
    }

    QFile file(inputDir.filePath(QStringLiteral("TDTRACE.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&file);
    os.setRealNumberNotation(QTextStream::FixedNotation);
    os.setRealNumberPrecision(6);
    os << times.size() << "   NUM\n";
    for (double time : times)
        os << time << "\n";
    return true;
}

QJsonObject SeepageWidget::saveJson()
{
    QJsonObject obj;
    // 保留标量兼容字段，旧版本仍可读取；新版的真实设置保存在上下游边界节点。
    obj[QStringLiteral("upstream_water_level")] = upstreamWaterLevel();
    obj[QStringLiteral("downstream_water_level")] = downstreamWaterLevel();
    obj[QStringLiteral("upstream_flux")] = upstreamFlux();
    obj[QStringLiteral("downstream_flux")] = downstreamFlux();
    obj[QStringLiteral("main_property_no")] = ui->spinMainPropertyNo->value();
    obj[QStringLiteral("controlled_property_no")] = ui->spinControlledPropertyNo->value();
    obj[QStringLiteral("natural_free_surface")] = ui->checkNaturalFreeSurface->isChecked();
    obj[QStringLiteral("drain_well")] = ui->checkDrainWell->isChecked();
    obj[QStringLiteral("compute_type")] = ui->comboComputeType->currentData().toInt();
    obj[QStringLiteral("iter")] = ui->spinIter->value();
    obj[QStringLiteral("itag_free")] = naturalFreeSurfaceEnabled()
                                               ? ui->comboItagFree->currentData().toInt() : 0;
    obj[QStringLiteral("seep_case_dir")] = QStringLiteral("SEEPCASE01");
    obj[QStringLiteral("dt")] = ui->spinDt->value();
    obj[QStringLiteral("tsta")] = ui->spinTsta->value();
    obj[QStringLiteral("tend")] = ui->spinTend->value();
    obj[QStringLiteral("time_unit")] = QStringLiteral("s");
    return obj;
}

void SeepageWidget::loadJson(const QJsonObject& obj)
{
    // 旧工程时间数值不做自动换算；新版统一按秒解释，并写入 time_unit=s。
    legacyUpstreamWaterLevel_ = obj.value(QStringLiteral("upstream_water_level")).toDouble(0.0);
    legacyDownstreamWaterLevel_ = obj.value(QStringLiteral("downstream_water_level")).toDouble(0.0);
    legacyUpstreamFlux_ = obj.value(QStringLiteral("upstream_flux")).toDouble(0.0);
    legacyDownstreamFlux_ = obj.value(QStringLiteral("downstream_flux")).toDouble(0.0);
    legacyHydraulicStateLoaded_ =
        obj.contains(QStringLiteral("upstream_water_level"))
        || obj.contains(QStringLiteral("downstream_water_level"))
        || obj.contains(QStringLiteral("upstream_flux"))
        || obj.contains(QStringLiteral("downstream_flux"));

    legacyUpstreamCurve_.clear();
    legacyDownstreamCurve_.clear();
    for (const auto& value : obj.value(QStringLiteral("water_level_points")).toArray())
    {
        const QJsonObject row = value.toObject();
        const double time = row.value(QStringLiteral("time")).toDouble();
        legacyUpstreamCurve_.push_back(
            {time, row.value(QStringLiteral("up_head")).toDouble()});
        legacyDownstreamCurve_.push_back(
            {time, row.value(QStringLiteral("dw_head")).toDouble()});
    }

    ui->spinMainPropertyNo->setValue(
        obj.value(QStringLiteral("main_property_no")).toInt(ui->spinMainPropertyNo->value()));
    ui->spinControlledPropertyNo->setValue(
        obj.value(QStringLiteral("controlled_property_no")).toInt(
            ui->spinControlledPropertyNo->value()));
    ui->checkNaturalFreeSurface->setChecked(
        obj.value(QStringLiteral("natural_free_surface")).toBool(
            ui->checkNaturalFreeSurface->isChecked()));
    ui->checkDrainWell->setChecked(
        obj.value(QStringLiteral("drain_well")).toBool(ui->checkDrainWell->isChecked()));

    const int computeType = obj.value(QStringLiteral("compute_type"))
                                .toInt(ui->comboComputeType->currentData().toInt());
    int computeTypeIndex = ui->comboComputeType->findData(computeType);
    if (computeTypeIndex < 0)
        computeTypeIndex = 0;
    ui->comboComputeType->setCurrentIndex(computeTypeIndex);
    ui->spinIter->setValue(obj.value(QStringLiteral("iter")).toInt(ui->spinIter->value()));

    const int itagFree = obj.value(QStringLiteral("itag_free"))
                             .toInt(ui->comboItagFree->currentData().toInt());
    int itagIndex = ui->comboItagFree->findData(itagFree);
    if (itagIndex < 0)
        itagIndex = 0;
    ui->comboItagFree->setCurrentIndex(itagIndex);
    ui->spinDt->setValue(obj.value(QStringLiteral("dt")).toDouble(ui->spinDt->value()));
    ui->spinTsta->setValue(obj.value(QStringLiteral("tsta")).toDouble(ui->spinTsta->value()));
    ui->spinTend->setValue(obj.value(QStringLiteral("tend")).toDouble(ui->spinTend->value()));

    updateComputeTypeVisibility();
    updateOptionalControls();
    syncBoundaryTransientSettings();
    updateBoundarySummary();
    emit requirementsChanged();
}

void SeepageWidget::onComputeClicked()
{
    if (ui->btnCompute->property("computing").toBool())
    {
        emit stopRequested();
        return;
    }

    QString error;
    if (!validateInputs(&error))
    {
        QMessageBox::warning(this, QStringLiteral("请完善渗流设置"), error);
        return;
    }
    emit computeRequested();
}

void SeepageWidget::setComputing(bool computing)
{
    ui->btnCompute->setProperty("computing", computing);
    ui->btnCompute->setText(computing ? QStringLiteral("停止计算")
                                      : QStringLiteral("数值法计算"));
}
