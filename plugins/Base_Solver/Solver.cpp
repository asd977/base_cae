#include "Solver.h"

#include <ContextMenuHub.h>
#include <EventManager.h>
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <ServiceRegistry.h>
#include <services/MaterialManagerService.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhysicalFieldService.h>
#include <services/ProjectManagerService.h>

#include <QDir>
#include <QDirIterator>
#include <QBrush>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QInputDialog>
#include <QLineEdit>
#include <QMap>
#include <QMessageBox>
#include <QMenu>
#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStringList>
#include <QStringConverter>
#include <QTextStream>
#include <QTimer>
#include <QProcess>
#include <QUuid>
#include <QVariant>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellType.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkSmartPointer.h>
#include <vtkStaticCellLocator.h>
#include <vtkUnstructuredGrid.h>

#include "FillStorageCyclePage.h"
#include "CaveExcavationBodyPage.h"
#include "CaveCraneBeamAreaPage.h"
#include "CaveBlockPlanePage.h"
#include "CaveBlockStabilityWidget.h"
#include "CaveExcavationWidget.h"
#include "CaveSupportComputationPage.h"
#include "CaveReinforcementFieldPage.h"
#include "SeepageBoundaryPage.h"
#include "SeepageWidget.h"
#include "StressBoundaryPage.h"
#include "DamAnalysisWidget.h"
#include "DamDynamicAnalysisPage.h"
#include "GeoStressRegressionWidget.h"
#include "GeoStressMeasurePointPage.h"
#include "GeoStressOuterForcePage.h"
#include "GeoStressSideFacePage.h"
#include "SlopeStabilityWidget.h"
#include "SolverSettingWidget.h"

namespace
{
constexpr int kSolverCustomTitleRole = Qt::UserRole + 104;
constexpr char kLegacyNamedAnalysisWorkspaceRoot[] = u8"计算结果";
constexpr char kLegacyAnalysisWorkspaceRoot[] = ".analysis_runs";
constexpr char kLegacyAnalysisManifestFileName[] = "analysis.json";

int readFirstLineInteger(const QString& filePath, int preferredIndex)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return -1;

    const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
    const QStringList tokens =
        line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return -1;

    const int index = qBound(0, preferredIndex, int(tokens.size()) - 1);
    bool ok = false;
    const int value = tokens.at(index).toInt(&ok);
    return ok ? value : -1;
}

int seepageFieldNodeCount(const QString& filePath)
{
    const QString name = QFileInfo(filePath).fileName();
    return name.contains(QStringLiteral("IIDISP"), Qt::CaseInsensitive)
               ? readFirstLineInteger(filePath, 1)
               : readFirstLineInteger(filePath, 0);
}

bool waveInputHasSamples(const QString& filePath, QString* detail = nullptr)
{
    QFileInfo info(filePath);
    if (!info.isFile() || info.size() <= 0)
    {
        if (detail)
            *detail = QStringLiteral("文件不存在或为空");
        return false;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (detail)
            *detail = QStringLiteral("文件无法读取");
        return false;
    }

    const QString firstLine = QString::fromLocal8Bit(file.readLine()).trimmed();
    const QStringList headerTokens =
        firstLine.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    bool countOk = false;
    const int sampleCount = headerTokens.isEmpty() ? -1 : headerTokens.first().toInt(&countOk);
    if (!countOk || sampleCount <= 0)
    {
        if (detail)
            *detail = countOk ? QStringLiteral("数据点数量为 0")
                              : QStringLiteral("首行缺少有效的数据点数量");
        return false;
    }

    // 求解器示例格式第二行为列标题，随后应恰有 sampleCount 组“时间 + 加速度 + 序号”。
    if (file.atEnd())
    {
        if (detail)
            *detail = QStringLiteral("缺少地震波数据行");
        return false;
    }
    file.readLine();

    double previousTime = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < sampleCount; ++i)
    {
        if (file.atEnd())
        {
            if (detail)
                *detail = QStringLiteral("声明 %1 个数据点，但文件仅包含 %2 个")
                              .arg(sampleCount).arg(i);
            return false;
        }
        const QString line = QString::fromLocal8Bit(file.readLine()).trimmed();
        const QStringList tokens =
            line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (tokens.size() < 2)
        {
            if (detail)
                *detail = QStringLiteral("第 %1 个数据点格式错误").arg(i + 1);
            return false;
        }
        bool timeOk = false;
        bool accelerationOk = false;
        const double time = tokens.at(0).toDouble(&timeOk);
        const double acceleration = tokens.at(1).toDouble(&accelerationOk);
        if (!timeOk || !accelerationOk || !std::isfinite(time)
            || !std::isfinite(acceleration) || time < previousTime)
        {
            if (detail)
                *detail = QStringLiteral("第 %1 个数据点的时间或加速度无效").arg(i + 1);
            return false;
        }
        previousTime = time;
    }
    return true;
}

QString seepageResultDirectory(const QString& workspace, const QString& resultName)
{
    const QString direct = QDir(workspace).filePath(resultName);
    if (QDir(direct).exists())
        return QDir(direct).absolutePath();

    const QString nested =
        QDir(workspace).filePath(QStringLiteral("计算结果/%1").arg(resultName));
    if (QDir(nested).exists())
        return QDir(nested).absolutePath();

    return {};
}

QVector<int> transientResultSteps(const QString& resultDir, bool needHead, bool needBody)
{
    QSet<int> headSteps;
    QSet<int> bodySteps;
    const QRegularExpression headPattern(
        QStringLiteral("^STEP(\\d+)_IIDISP\\.TXT$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression bodyPattern(
        QStringLiteral("^STEP(\\d+)_INBODY\\.TXT$"),
        QRegularExpression::CaseInsensitiveOption);

    const QStringList files = QDir(resultDir).entryList(QDir::Files, QDir::Name);
    for (const QString& name : files)
    {
        QRegularExpressionMatch match = headPattern.match(name);
        if (match.hasMatch())
            headSteps.insert(match.captured(1).toInt());

        match = bodyPattern.match(name);
        if (match.hasMatch())
            bodySteps.insert(match.captured(1).toInt());
    }

    QSet<int> usable;
    if (needHead && needBody)
    {
        usable = headSteps;
        usable.intersect(bodySteps);
    }
    else if (needHead)
    {
        usable = headSteps;
    }
    else if (needBody)
    {
        usable = bodySteps;
    }

    QVector<int> steps;
    steps.reserve(usable.size());
    for (int step : std::as_const(usable))
        steps.push_back(step);
    std::sort(steps.begin(), steps.end());
    return steps;
}

struct TransientTimeInfo
{
    bool valid = false;
    double start = 0.0;
    double increment = 0.0;
    QString unit = QStringLiteral("s");
};

struct WaterLevelCurve
{
    QVector<QPair<double, double>> samples;
};

QString resultDisplayNumber(double value)
{
    const double rounded = std::round(value);
    return std::abs(value - rounded) < 1.0e-9
               ? QString::number(static_cast<qint64>(rounded))
               : QString::number(value, 'g', 12);
}

TransientTimeInfo transientTimeInfo(const QString& resultDir)
{
    QString keyPath;
    for (const QString& name : {QStringLiteral("IKEY.DAT"), QStringLiteral("IKEY.TXT")})
    {
        const QString candidate = QDir(resultDir).filePath(name);
        if (QFileInfo(candidate).isFile())
        {
            keyPath = candidate;
            break;
        }
    }
    QFile file(keyPath);
    if (keyPath.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString header = in.readLine().simplified();
        if (!header.contains(QStringLiteral("TSTA"), Qt::CaseInsensitive) ||
            !header.contains(QStringLiteral("DT"), Qt::CaseInsensitive))
            continue;
        const QStringList names = header.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        int startIndex = -1;
        int incrementIndex = -1;
        for (int i = 0; i < names.size(); ++i)
        {
            if (names.at(i).compare(QStringLiteral("TSTA"), Qt::CaseInsensitive) == 0)
                startIndex = i;
            else if (names.at(i).compare(QStringLiteral("DT"), Qt::CaseInsensitive) == 0)
                incrementIndex = i;
        }
        if (startIndex < 0 || incrementIndex < 0)
            continue;
        while (!in.atEnd())
        {
            const QString valuesLine = in.readLine().simplified();
            if (valuesLine.isEmpty())
                continue;
            const QStringList values =
                valuesLine.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (values.size() <= qMax(startIndex, incrementIndex))
                break;
            bool startOk = false;
            bool incrementOk = false;
            const double start = values.at(startIndex).toDouble(&startOk);
            const double increment = values.at(incrementIndex).toDouble(&incrementOk);
            if (!startOk || !incrementOk || !std::isfinite(start) ||
                !std::isfinite(increment) || increment <= 0.0)
                break;
            return {true, start, increment, QStringLiteral("s")};
        }
    }
    return {};
}

QString workspaceInputFile(const QString& workspace, const QString& fileName)
{
    const QStringList inputDirectories = {
        QDir(workspace).filePath(QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)),
        QDir(workspace).filePath(QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)),
        QDir(workspace).filePath(QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName))};
    for (const QString& inputDirectory : inputDirectories)
    {
        const QDir dir(inputDirectory);
        const QFileInfoList files = dir.entryInfoList(QDir::Files);
        for (const QFileInfo& info : files)
        {
            if (info.fileName().compare(fileName, Qt::CaseInsensitive) == 0)
                return info.absoluteFilePath();
        }
    }
    return {};
}

WaterLevelCurve waterLevelCurve(const QString& workspace, const QString& fileName)
{
    QFile file(workspaceInputFile(workspace, fileName));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    WaterLevelCurve curve;
    QTextStream in(&file);
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    while (!in.atEnd())
    {
        const QStringList values =
            in.readLine().trimmed().split(whitespace, Qt::SkipEmptyParts);
        if (values.size() < 2)
            continue;
        bool timeOk = false;
        bool levelOk = false;
        const double time = values.at(0).toDouble(&timeOk);
        const double level = values.at(1).toDouble(&levelOk);
        if (timeOk && levelOk && std::isfinite(time) && std::isfinite(level))
            curve.samples.push_back(qMakePair(time, level));
    }
    std::sort(curve.samples.begin(), curve.samples.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    return curve;
}

bool waterLevelAtTime(const WaterLevelCurve& curve, double time, double* level)
{
    if (!level || curve.samples.isEmpty() || !std::isfinite(time))
        return false;
    constexpr double tolerance = 1.0e-9;
    if (time < curve.samples.first().first - tolerance ||
        time > curve.samples.last().first + tolerance)
        return false;
    if (time <= curve.samples.first().first + tolerance)
    {
        *level = curve.samples.first().second;
        return true;
    }
    for (int i = 1; i < curve.samples.size(); ++i)
    {
        const auto& right = curve.samples.at(i);
        if (time > right.first + tolerance)
            continue;
        const auto& left = curve.samples.at(i - 1);
        const double duration = right.first - left.first;
        *level = duration <= tolerance
                     ? right.second
                     : left.second + (right.second - left.second) *
                                           ((time - left.first) / duration);
        return std::isfinite(*level);
    }
    return false;
}

QMap<int, QString> transientResultLabels(const QString& workspace,
                                         const QString& resultDir,
                                         const QVector<int>& steps)
{
    QMap<int, QString> labels;
    const TransientTimeInfo timeInfo = transientTimeInfo(resultDir);
    const WaterLevelCurve upstream =
        waterLevelCurve(workspace, QStringLiteral("HWDROP_UP.DAT"));
    const WaterLevelCurve downstream =
        waterLevelCurve(workspace, QStringLiteral("HWDROP_DW.DAT"));
    for (int step : steps)
    {
        if (step < 0)
            continue;
        if (!timeInfo.valid)
        {
            labels.insert(step, QStringLiteral("计算时刻索引 %1").arg(step));
            continue;
        }
        const double time = timeInfo.start + timeInfo.increment * step;
        QString label = QStringLiteral("计算时刻 %1 %2")
                            .arg(resultDisplayNumber(time), timeInfo.unit);
        QStringList levels;
        double level = 0.0;
        if (waterLevelAtTime(upstream, time, &level))
            levels.push_back(QStringLiteral("上游水位 %1 m").arg(resultDisplayNumber(level)));
        if (waterLevelAtTime(downstream, time, &level))
            levels.push_back(QStringLiteral("下游水位 %1 m").arg(resultDisplayNumber(level)));
        if (!levels.isEmpty())
            label += QStringLiteral("（%1）").arg(levels.join(QStringLiteral("，")));
        labels.insert(step, label);
    }
    return labels;
}

struct SolverRuntimeOptions
{
    bool useGpu = false;
    int deviceCode = 0;
    QString executableRelativePath;
};

SolverRuntimeOptions solverRuntimeOptions()
{
    QSettings settings;
    SolverRuntimeOptions options;
    options.useGpu = settings.value(QStringLiteral("Solver/ComputeDevice"),
                                    QStringLiteral("cpu")).toString().trimmed().toLower()
                     == QStringLiteral("gpu");
    const int gpuDeviceMode = qBound(
        1, settings.value(QStringLiteral("Solver/GpuDeviceMode"), 2).toInt(), 2);
    options.deviceCode = options.useGpu ? gpuDeviceMode : 0;
    options.executableRelativePath = options.useGpu
        ? QStringLiteral("solver/PCCAE_GPU/PCCAE_GPU.exe")
        : QStringLiteral("solver/PCCAE_CPU/PCCAE_CPU.exe");
    return options;
}

QString deployedSolverExecutable(const SolverRuntimeOptions& options)
{
    return QDir::toNativeSeparators(
        QDir(QCoreApplication::applicationDirPath())
            .filePath(options.executableRelativePath));
}

void terminateDeployedSolvers()
{
    QProcess::execute("taskkill", {"-f", "-im", "PCCAE_CPU.exe"});
    QProcess::execute("taskkill", {"-f", "-im", "PCCAE_GPU.exe"});
}

QString solverErrorFromText(const QString& text)
{
    static const QRegularExpression errorLine(
        QStringLiteral(
            R"((?:^|[\r\n])\s*((?:(?:ERROR\b|forrtl:\s*(?:severe|error)\b)[^\r\n]*|Failed to create subdirectory)))"),
        QRegularExpression::CaseInsensitiveOption);
    QString lastError;
    QRegularExpressionMatchIterator matches = errorLine.globalMatch(text);
    while (matches.hasNext())
        lastError = matches.next().captured(1).trimmed();
    return lastError;
}

QString solverErrorFromRunLogs(const QString& workspace, qint64 startedAtMs)
{
    if (workspace.isEmpty())
        return {};

    QString newestError;
    qint64 newestModifiedMs = 0;
    QDirIterator logs(workspace,
                      {QStringLiteral("III110.DAT"), QStringLiteral("fort.110")},
                      QDir::Files, QDirIterator::Subdirectories);
    while (logs.hasNext())
    {
        const QString path = logs.next();
        const QFileInfo info(path);
        const qint64 modifiedMs = info.lastModified().toMSecsSinceEpoch();
        if (startedAtMs > 0 && modifiedMs < startedAtMs)
            continue;

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        constexpr qint64 kLogTailBytes = 256 * 1024;
        if (file.size() > kLogTailBytes)
            file.seek(file.size() - kLogTailBytes);
        const QString error = solverErrorFromText(QString::fromLocal8Bit(file.readAll()));
        if (!error.isEmpty() && modifiedMs >= newestModifiedMs)
        {
            newestModifiedMs = modifiedMs;
            newestError = error;
        }
    }
    return newestError;
}

QString customerFacingSolverFailure(const QString& technicalReason)
{
    const QString reason = technicalReason.trimmed();
    if (reason.contains(QStringLiteral("FILE DOES NOT EXIST"), Qt::CaseInsensitive)
        || reason.contains(QStringLiteral("does not exist"), Qt::CaseInsensitive))
    {
        return QStringLiteral(
            "计算所需数据不完整。请检查当前工况的边界、材料和过程设置后重试。");
    }
    if (reason.contains(QStringLiteral("Failed to create subdirectory"),
                        Qt::CaseInsensitive)
        || reason.contains(QStringLiteral("permission"), Qt::CaseInsensitive)
        || reason.contains(QStringLiteral("access is denied"), Qt::CaseInsensitive))
    {
        return QStringLiteral(
            "无法创建计算数据。请先保存工程，并确认工程所在位置可以正常写入。");
    }
    if (reason.contains(QStringLiteral("out of memory"), Qt::CaseInsensitive)
        || reason.contains(QStringLiteral("insufficient memory"), Qt::CaseInsensitive))
    {
        return QStringLiteral(
            "计算所需内存不足。请关闭其他大型程序，或适当减小模型规模后重试。");
    }
    if (reason.contains(QStringLiteral("singular"), Qt::CaseInsensitive))
    {
        return QStringLiteral(
            "当前模型的约束或材料设置可能不完整，计算无法继续。请检查后重试。");
    }
    return QStringLiteral("计算未完成，系统未能识别具体原因。");
}

bool copyFileReplacing(const QString& sourcePath, const QString& targetPath)
{
    const QFileInfo targetInfo(targetPath);
    if (!QDir().mkpath(targetInfo.absolutePath()))
        return false;
    if (QFileInfo::exists(targetPath) && !QFile::remove(targetPath))
        return false;
    return QFile::copy(sourcePath, targetPath);
}

bool copyDirectoryTree(const QString& sourcePath, const QString& targetPath)
{
    const QDir source(sourcePath);
    if (!source.exists() || !QDir().mkpath(targetPath))
        return false;

    const QFileInfoList entries = source.entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& entry : entries)
    {
        const QString target = QDir(targetPath).filePath(entry.fileName());
        if (entry.isDir())
        {
            if (!copyDirectoryTree(entry.absoluteFilePath(), target))
                return false;
        }
        else if (!copyFileReplacing(entry.absoluteFilePath(), target))
        {
            return false;
        }
    }
    return true;
}

bool replaceDirectoryTree(const QString& sourcePath, const QString& targetPath)
{
    QDir target(targetPath);
    if (target.exists() && !target.removeRecursively())
        return false;
    return copyDirectoryTree(sourcePath, targetPath);
}

bool patchIiinElementLayers(const QString& filePath,
                            const QHash<int, int>& layerByElementId)
{
    if (layerByElementId.isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QStringList lines =
        QString::fromLocal8Bit(file.readAll())
            .split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::KeepEmptyParts);
    file.close();

    if (lines.size() < 2)
        return false;

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto integerMatches = [](const QString& line)
    {
        QVector<QRegularExpressionMatch> matches;
        auto it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());
        return matches;
    };

    const QVector<QRegularExpressionMatch> headerMatches = integerMatches(lines.at(0));
    if (headerMatches.size() < 4)
        return false;

    const int numnp = headerMatches.at(0).captured().toInt();
    const int nelem = headerMatches.at(2).captured().toInt();
    const int manod = headerMatches.at(3).captured().toInt();
    const int firstElemLine = 2 + numnp;
    const int layerTokenIndex = manod + 3;
    if (numnp < 0 || nelem <= 0 || manod <= 0 ||
        lines.size() < firstElemLine + nelem)
        return false;

    int changed = 0;
    for (int i = 0; i < nelem; ++i)
    {
        QString& line = lines[firstElemLine + i];
        const QVector<QRegularExpressionMatch> matches = integerMatches(line);
        if (matches.size() <= layerTokenIndex)
            return false;

        const int elementId = matches.at(0).captured().toInt();
        const auto layerIt = layerByElementId.constFind(elementId);
        if (layerIt == layerByElementId.constEnd())
            continue;

        const QRegularExpressionMatch& layerMatch = matches.at(layerTokenIndex);
        QString replacement = QString::number(layerIt.value());
        if (layerMatch.capturedLength() > replacement.size())
        {
            replacement = replacement.rightJustified(
                layerMatch.capturedLength(), QLatin1Char(' '));
        }
        line.replace(layerMatch.capturedStart(), layerMatch.capturedLength(), replacement);
        ++changed;
    }

    if (changed != layerByElementId.size() ||
        !file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    file.write(lines.join(QStringLiteral("\n")).toLocal8Bit());
    return true;
}

struct DamLayerSummary
{
    int minimumDamLayer = std::numeric_limits<int>::max();
    int maximumDamLayer = std::numeric_limits<int>::min();
    QSet<int> foundationMaterials;
    QSet<int> damMaterials;
    QSet<int> damLayers;
};

bool readDamLayerSummary(const QString& filePath, DamLayerSummary* summary,
                         QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!summary)
        return fail(QStringLiteral("无法检查当前网格的施工分层。"));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法读取当前网格。请重新导入或重新生成网格后重试。"));

    const QStringList lines =
        QString::fromLocal8Bit(file.readAll())
            .split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return fail(QStringLiteral("当前网格数据不完整。请重新导入或重新生成网格后重试。"));

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto integerMatches = [](const QString& line)
    {
        QVector<QRegularExpressionMatch> matches;
        auto it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());
        return matches;
    };

    const QVector<QRegularExpressionMatch> header = integerMatches(lines.at(0));
    if (header.size() < 4)
        return fail(QStringLiteral("当前网格数据无法识别。请重新导入或重新生成网格后重试。"));

    const int nodeCount = header.at(0).captured().toInt();
    const int elementCount = header.at(2).captured().toInt();
    const int maximumNodeCount = header.at(3).captured().toInt();
    const int firstElementLine = 2 + nodeCount;
    const int materialTokenIndex = maximumNodeCount + 2;
    const int layerTokenIndex = maximumNodeCount + 3;
    if (nodeCount < 0 || elementCount <= 0 || maximumNodeCount <= 0 ||
        lines.size() < firstElementLine + elementCount)
    {
        return fail(QStringLiteral("当前网格数据不完整。请重新导入或重新生成网格后重试。"));
    }

    DamLayerSummary parsed;
    for (int i = 0; i < elementCount; ++i)
    {
        const QVector<QRegularExpressionMatch> matches =
            integerMatches(lines.at(firstElementLine + i));
        if (matches.size() <= layerTokenIndex)
        {
            return fail(QStringLiteral("部分网格区域缺少材料或施工层信息。请重新设置网格分区和施工分层。"));
        }

        const int material = matches.at(materialTokenIndex).captured().toInt();
        const int layer = matches.at(layerTokenIndex).captured().toInt();
        if (layer <= 0)
        {
            return fail(QStringLiteral("部分网格区域没有有效的施工层。土石坝施工层必须使用正整数。"));
        }

        // 求解器保留材料号 1 作为内部材料，不用它派生坝基和坝体材料集合。
        if (material == 1)
            continue;
        if (layer == 1)
            parsed.foundationMaterials.insert(material);
        else
        {
            parsed.damMaterials.insert(material);
            parsed.damLayers.insert(layer);
            parsed.minimumDamLayer = qMin(parsed.minimumDamLayer, layer);
            parsed.maximumDamLayer = qMax(parsed.maximumDamLayer, layer);
        }
    }

    if (parsed.foundationMaterials.isEmpty())
        return fail(QStringLiteral("网格中没有第 1 层坝基单元。"));
    if (parsed.damMaterials.isEmpty())
        return fail(QStringLiteral("网格中没有层号大于 1 的坝体单元。"));
    if (parsed.minimumDamLayer != 2)
    {
        return fail(QStringLiteral("坝体填筑层必须从第 2 层开始，当前最小坝体层为 %1。")
                        .arg(parsed.minimumDamLayer));
    }
    for (int layer = 2; layer <= parsed.maximumDamLayer; ++layer)
    {
        if (!parsed.damLayers.contains(layer))
        {
            return fail(QStringLiteral("坝体填筑层缺少第 %1 层，请重新设置连续的施工分层。")
                            .arg(layer));
        }
    }

    QList<int> conflictingMaterials;
    for (int material : std::as_const(parsed.foundationMaterials))
    {
        if (parsed.damMaterials.contains(material))
            conflictingMaterials.push_back(material);
    }
    if (!conflictingMaterials.isEmpty())
    {
        std::sort(conflictingMaterials.begin(), conflictingMaterials.end());
        QStringList values;
        for (int material : std::as_const(conflictingMaterials))
            values << QString::number(material);
        return fail(QStringLiteral("材料分区 %1 同时用于坝基和坝体。请为坝基与坝体设置不同的材料分区。")
                        .arg(values.join(QStringLiteral("、"))));
    }

    *summary = parsed;
    return true;
}

bool validateDamFillRanges(const QString& filePath,
                           const QVector<QPair<int, int>>& ranges,
                           QString* errorMessage)
{
    DamLayerSummary summary;
    if (!readDamLayerSummary(filePath, &summary, errorMessage))
        return false;

    for (int i = 0; i < ranges.size(); ++i)
    {
        const int startLayer = ranges.at(i).first;
        const int endLayer = ranges.at(i).second;
        if (startLayer > summary.maximumDamLayer || endLayer > summary.maximumDamLayer)
        {
            if (errorMessage)
            {
                *errorMessage =
                    QStringLiteral("第 %1 个计算工况的施工阶段设置为第 %2 至 %3 层，但网格坝体最大层号为 %4。")
                        .arg(i + 1)
                        .arg(startLayer)
                        .arg(endLayer)
                        .arg(summary.maximumDamLayer);
            }
            return false;
        }
    }
    return true;
}

bool readIiinMaterialNumbers(const QString& filePath, QSet<int>* materials,
                             QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!materials)
        return fail(QStringLiteral("无法检查当前网格的材料分区。"));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法读取当前网格。请重新导入或重新生成网格后重试。"));

    const QStringList lines =
        QString::fromLocal8Bit(file.readAll())
            .split(QRegularExpression(QStringLiteral("\\r?\\n")), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return fail(QStringLiteral("当前网格数据不完整。请重新导入或重新生成网格后重试。"));

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto integerMatches = [](const QString& line)
    {
        QVector<QRegularExpressionMatch> matches;
        auto it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());
        return matches;
    };

    const QVector<QRegularExpressionMatch> header = integerMatches(lines.at(0));
    if (header.size() < 4)
        return fail(QStringLiteral("当前网格数据无法识别。请重新导入或重新生成网格后重试。"));

    const int nodeCount = header.at(0).captured().toInt();
    const int elementCount = header.at(2).captured().toInt();
    const int maximumNodeCount = header.at(3).captured().toInt();
    const int firstElementLine = 2 + nodeCount;
    const int materialTokenIndex = maximumNodeCount + 2;
    if (nodeCount < 0 || elementCount <= 0 || maximumNodeCount <= 0 ||
        lines.size() < firstElementLine + elementCount)
    {
        return fail(QStringLiteral("当前网格数据不完整。请重新导入或重新生成网格后重试。"));
    }

    QSet<int> parsed;
    for (int i = 0; i < elementCount; ++i)
    {
        const QVector<QRegularExpressionMatch> matches =
            integerMatches(lines.at(firstElementLine + i));
        if (matches.size() <= materialTokenIndex)
        {
            return fail(QStringLiteral("部分网格区域缺少材料分区信息。请重新设置网格材料分区。"));
        }

        const int material = matches.at(materialTokenIndex).captured().toInt();
        if (material < 1 || material > 300)
        {
            return fail(QStringLiteral("部分网格区域使用了无效的材料分区。请重新设置材料分区。"));
        }
        parsed.insert(material);
    }

    *materials = parsed;
    return true;
}

bool readImatValues(const QString& filePath, QVector<QVector<double>>* values,
                    QString* errorMessage)
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    if (!values)
        return fail(QStringLiteral("无法检查当前材料参数。"));

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return fail(QStringLiteral("无法读取当前材料参数。请检查材料设置后重试。"));

    QTextStream stream(&file);
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList header = stream.readLine().split(whitespace, Qt::SkipEmptyParts);
    bool nmatOk = false;
    bool nconOk = false;
    const int materialCount = header.value(0).toInt(&nmatOk);
    const int parameterCount = header.value(1).toInt(&nconOk);
    if (!nmatOk || !nconOk || materialCount <= 0 || materialCount > 300 ||
        materialCount % 10 != 0 || parameterCount <= 0)
    {
        return fail(QStringLiteral("当前材料参数不完整。请检查材料设置后重试。"));
    }

    QVector<QVector<double>> parsed(materialCount,
                                    QVector<double>(parameterCount, 0.0));
    auto nextNonEmptyLine = [&stream]()
    {
        QString line;
        while (!stream.atEnd())
        {
            line = stream.readLine();
            if (!line.trimmed().isEmpty())
                return line;
        }
        return QString();
    };

    const int groupCount = materialCount / 10;
    for (int group = 0; group < groupCount; ++group)
    {
        const QString groupLine = nextNonEmptyLine();
        const QString materialHeaderLine = nextNonEmptyLine();
        if (!groupLine.contains(QStringLiteral("CONMAT"), Qt::CaseInsensitive) ||
            materialHeaderLine.isEmpty())
        {
            return fail(QStringLiteral("当前材料参数不完整。请检查材料设置后重试。"));
        }

        for (int parameter = 0; parameter < parameterCount; ++parameter)
        {
            const QString dataLine = nextNonEmptyLine();
            const QStringList tokens = dataLine.split(whitespace, Qt::SkipEmptyParts);
            if (tokens.size() < 11)
            {
                return fail(QStringLiteral("当前材料参数不完整。请检查材料设置后重试。"));
            }

            bool rowOk = false;
            const int rowNumber = tokens.at(10).toInt(&rowOk);
            if (!rowOk || rowNumber != parameter + 1)
            {
                return fail(QStringLiteral("当前材料参数无法识别。请检查材料设置后重试。"));
            }

            for (int column = 0; column < 10; ++column)
            {
                bool valueOk = false;
                const double value = tokens.at(column).toDouble(&valueOk);
                if (!valueOk)
                {
                    return fail(QStringLiteral("当前材料参数包含无效数值。请检查材料设置后重试。"));
                }
                parsed[group * 10 + column][parameter] = value;
            }
        }
    }

    *values = parsed;
    return true;
}

bool validateSolverMaterials(const QString& iiinPath, const QString& imatPath,
                             QString* errorMessage)
{
    QSet<int> usedMaterials;
    if (!readIiinMaterialNumbers(iiinPath, &usedMaterials, errorMessage))
        return false;

    QVector<QVector<double>> materialValues;
    if (!readImatValues(imatPath, &materialValues, errorMessage))
        return false;

    constexpr double tolerance = 1.0e-4;
    QList<int> sortedMaterials = usedMaterials.values();
    std::sort(sortedMaterials.begin(), sortedMaterials.end());
    QStringList issues;
    for (int material : std::as_const(sortedMaterials))
    {
        if (material > materialValues.size())
        {
            issues << QStringLiteral("材料分区 %1 尚未设置材料参数。")
                          .arg(material);
            continue;
        }

        const QVector<double>& parameters = materialValues.at(material - 1);
        const int configuredModel = qRound(parameters.value(0));
        if (material == 2 && configuredModel != 0)
        {
            issues << QStringLiteral(
                          "材料分区 2 为系统保留分区，不能用于当前材料模型。请将相关单元调整到其他材料分区后重试。");
            continue;
        }

        const int solverModel = material <= 2 ? 0 : configuredModel;
        QString missingParameter;
        if (solverModel == -4 && parameters.value(79) <= tolerance)
            missingParameter = QStringLiteral("动力模型参数 K1");
        else if ((solverModel == -6 || solverModel == -5 || solverModel == -3 ||
                  solverModel == -2 || solverModel == -1) &&
                 parameters.value(68) <= tolerance)
            missingParameter = QStringLiteral("非线性模型参数 K");
        else if (solverModel >= 0 && parameters.value(1) <= tolerance)
            missingParameter = QStringLiteral("弹性模量");

        if (missingParameter.isEmpty())
            continue;

        issues << QStringLiteral("材料分区 %1 缺少%2。")
                      .arg(material)
                      .arg(missingParameter);
    }

    if (issues.isEmpty())
        return true;

    if (errorMessage)
        *errorMessage = issues.join(QLatin1Char('\n')).trimmed();
    return false;
}

QString workspaceKey(const QString& uuid)
{
    const QUuid parsed(uuid);
    if (!parsed.isNull())
        return parsed.toString(QUuid::WithoutBraces);

    QString key = uuid.trimmed();
    key.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    return key;
}

QString safeWorkspaceName(QString name)
{
    name = name.trimmed();
    name.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F]")),
                 QStringLiteral("_"));
    while (name.endsWith(QLatin1Char('.')) || name.endsWith(QLatin1Char(' ')))
        name.chop(1);
    if (name.isEmpty())
        name = QStringLiteral("分析");
    if (name.size() > 80)
        name = name.left(80).trimmed();

    static const QRegularExpression reservedName(
        QStringLiteral("^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\..*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedName.match(name).hasMatch())
        name.prepend(QLatin1Char('_'));
    return name;
}

QString workspaceManifestUuid(const QString& workspacePath)
{
    const QDir workspace(workspacePath);
    const QStringList manifestNames = {
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName),
        QString::fromLatin1(kLegacyAnalysisManifestFileName)};
    for (const QString& manifestName : manifestNames)
    {
        QFile file(workspace.filePath(manifestName));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
        if (document.isObject())
            return document.object().value(QStringLiteral("analysisUuid")).toString().trimmed();
    }
    return {};
}

void migrateWorkspaceManifestName(const QString& workspacePath)
{
    const QDir workspace(workspacePath);
    const QString currentPath = workspace.filePath(
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName));
    const QString legacyPath = workspace.filePath(
        QString::fromLatin1(kLegacyAnalysisManifestFileName));
    if (!QFileInfo::exists(currentPath) && QFileInfo::exists(legacyPath))
        QFile::rename(legacyPath, currentPath);
}

void removeCopiedProjectMetadata(const QString& workspacePath)
{
    const QString manifestName = QString::fromUtf8(
        PublicConstants::kSolverAnalysisManifestFileName);
    const QString legacyManifestName = QString::fromLatin1(
        kLegacyAnalysisManifestFileName);
    const QStringList projectDataPaths = {
        QString::fromUtf8(PublicConstants::kPostProcessingProjectFileName),
        QString::fromUtf8(PublicConstants::kPreProcessingProjectFileName),
        QString::fromUtf8(PublicConstants::kMeshProjectFileName),
        QString::fromUtf8(PublicConstants::kMaterialProjectFileName),
        QString::fromUtf8(PublicConstants::kPhysicalFieldProjectFileName),
        QString::fromUtf8(PublicConstants::kMeshSnapshotFileName),
        QString::fromUtf8(PublicConstants::kMeshMapFileName),
        QString::fromUtf8(PublicConstants::kSolverProjectFileName),
        QString::fromUtf8(PublicConstants::kSurrogateProjectFileName)};
    QSet<QString> projectDataNames;
    for (const QString& path : projectDataPaths)
        projectDataNames.insert(QFileInfo(path).fileName().toCaseFolded());

    const QFileInfoList files = QDir(workspacePath).entryInfoList(QDir::Files);
    for (const QFileInfo& file : files)
    {
        if (file.fileName().compare(manifestName, Qt::CaseInsensitive) == 0 ||
            file.fileName().compare(legacyManifestName, Qt::CaseInsensitive) == 0)
            continue;
        if (file.suffix().compare(QStringLiteral("xml"), Qt::CaseInsensitive) == 0 ||
            projectDataNames.contains(file.fileName().toCaseFolded()))
            QFile::remove(file.absoluteFilePath());
    }
}

QString solverNodeBaseTitle(const QString& kind)
{
    static const QHash<QString, QString> titles = {
        {QStringLiteral("SeepageAnalysis"), QStringLiteral("渗流分析")},
        {QStringLiteral("DamAnalysis"), QStringLiteral("土石坝分析")},
        {QStringLiteral("SlopeStabilityAnalysis"), QStringLiteral("边坡分析")},
        {QStringLiteral("CaveExcavationAnalysis"), QStringLiteral("洞室开挖工程")},
        {QStringLiteral("CaveBlockStabilityAnalysis"), QStringLiteral("块体稳定分析")},
        {QStringLiteral("FillStorageCycle"), QStringLiteral("填筑蓄水")},
        {QStringLiteral("DamDynamicAnalysis"), QStringLiteral("动力分析")},
        {QStringLiteral("GeoStressRegression"), QStringLiteral("地应力回归分析")},
        {QStringLiteral("GeoStressRegressionSideFace"), QStringLiteral("侧立面")},
        {QStringLiteral("GeoStressRegressionMeasurePoint"), QStringLiteral("实测地应力点")},
        {QStringLiteral("GeoStressRegressionOuterForce"), QStringLiteral("外面力")},
        {QStringLiteral("CaveSupportComputation"), QStringLiteral("开挖与加固分析")},
        {QStringLiteral("CaveLiningInnerBoundary"), QStringLiteral("衬砌内边界")},
        {QStringLiteral("CaveLiningOuterBoundary"), QStringLiteral("衬砌外边界")},
        {QStringLiteral("CaveCraneBeamArea"), QStringLiteral("吊车梁区域")},
        {QStringLiteral("CaveExcavationBody"), QStringLiteral("开挖体")},
        {QStringLiteral("CaveBlockPlane"), QStringLiteral("滑块面")},
        {QStringLiteral("SeepageNaturalBoundary"), QStringLiteral("天然渗流初始面")}};
    return titles.value(kind);
}

QString solverNodeRelationshipHint(ProjectNode* node)
{
    if (!node)
        return {};

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    // 只保留对客户真正有帮助、且仅凭节点名称不容易判断的说明。
    static const QHash<QString, QString> hints = {
        {QStringLiteral("FillStorageCycle"), QStringLiteral("设置本工况的填筑、蓄水、渗流关联、湿化和水位骤降")},
        {QStringLiteral("DamDynamicAnalysis"), QStringLiteral("设置本工况的模态、地震时程、永久沉降及静动材料转换")},
        {QStringLiteral("CaveLiningInnerBoundary"),
         QStringLiteral("衬砌与洞腔接触的内表面")},
        {QStringLiteral("CaveLiningOuterBoundary"),
         QStringLiteral("衬砌与围岩接触的外表面")},
        {QStringLiteral("CaveCraneBeamArea"), QStringLiteral("吊车梁所在实体区域")},
        {QStringLiteral("SeepageNaturalBoundary"), QStringLiteral("初始地下水自由面")},
        {QStringLiteral("DamIceRegion"), QStringLiteral("结冰作用节点")}};
    return hints.value(kind);
}

bool solverNodeUsesAutomaticIndex(const QString& kind)
{
    static const QSet<QString> repeatableKinds = {
        QStringLiteral("SeepageAnalysis"), QStringLiteral("DamAnalysis"),
        QStringLiteral("SlopeStabilityAnalysis"), QStringLiteral("CaveExcavationAnalysis"),
        QStringLiteral("CaveBlockStabilityAnalysis"),
        QStringLiteral("GeoStressRegressionSideFace"), QStringLiteral("CaveExcavationBody"),
        QStringLiteral("CaveBlockPlane"), QStringLiteral("SeepageNaturalBoundary")};
    return repeatableKinds.contains(kind);
}

bool solverNodeCanRename(const QString& kind)
{
    static const QSet<QString> renamableKinds = {
        QStringLiteral("SeepageAnalysis"), QStringLiteral("DamAnalysis"),
        QStringLiteral("SlopeStabilityAnalysis"), QStringLiteral("CaveExcavationAnalysis"),
        QStringLiteral("CaveBlockStabilityAnalysis"),
        QStringLiteral("GeoStressRegressionSideFace"), QStringLiteral("CaveExcavationBody"),
        QStringLiteral("CaveBlockPlane"), QStringLiteral("SeepageNaturalBoundary")};
    return renamableKinds.contains(kind);
}

bool solverNodeCanMove(const QString& kind)
{
    static const QSet<QString> movableKinds = {
        QStringLiteral("GeoStressRegressionSideFace"),
        QStringLiteral("CaveBlockPlane"), QStringLiteral("SeepageNaturalBoundary")};
    return movableKinds.contains(kind);
}

int solverChildOrderRank(ProjectNode* parent, ProjectNode* child)
{
    if (!parent || !child)
        return 1000;

    const QString parentKind = parent->data(0, ProjectNode::RoleKind).toString();
    const QString childKind = child->data(0, ProjectNode::RoleKind).toString();
    const QString pageType = child->getPage() ? child->getPage()->getPageType() : QString();

    if (parentKind == QStringLiteral("DamAnalysis"))
    {
        if (childKind == QStringLiteral("FillStorageCycle")) return 0;
        if (childKind == QStringLiteral("DamDynamicAnalysis")) return 1;
        if (pageType == QStringLiteral("StressBoundaryDamUpstream")) return 10;
        if (childKind == QStringLiteral("DamIceRegion")) return 11;
    }
    else if (parentKind == QStringLiteral("SeepageAnalysis"))
    {
        if (pageType == QStringLiteral("SeepageBoundaryUpstreamWater")) return 0;
        if (pageType == QStringLiteral("SeepageBoundaryDownstreamWater")) return 1;
        if (pageType == QStringLiteral("SeepageBoundaryNaturalSeepage")) return 2;
        if (pageType == QStringLiteral("SeepageBoundaryDrainWell")) return 3;
    }
    else if (parentKind == QStringLiteral("CaveExcavationAnalysis"))
    {
        if (childKind == QStringLiteral("GeoStressRegression")) return 0;
        if (childKind == QStringLiteral("CaveSupportComputation")) return 1;
        if (childKind == QStringLiteral("CaveBlockStabilityAnalysis")) return 2; // 旧工程兼容
    }
    else if (parentKind == QStringLiteral("GeoStressRegression"))
    {
        if (childKind == QStringLiteral("GeoStressRegressionSideFace")) return 0;
        if (childKind == QStringLiteral("GeoStressRegressionMeasurePoint")) return 10;
        if (childKind == QStringLiteral("GeoStressRegressionOuterForce")) return 11;
    }
    else if (parentKind == QStringLiteral("CaveSupportComputation"))
    {
        if (childKind == QStringLiteral("CaveLiningInnerBoundary")) return 0;
        if (childKind == QStringLiteral("CaveLiningOuterBoundary")) return 1;
        if (childKind == QStringLiteral("CaveCraneBeamArea")) return 2;
        if (childKind == QStringLiteral("CaveExcavationBody")) return 10;
    }
    return 100;
}

bool solverNodeHasCustomTitle(ProjectNode* node)
{
    return node && !node->data(0, kSolverCustomTitleRole).toString().trimmed().isEmpty();
}

QString solverNodeConfiguredTitle(ProjectNode* node, const QString& kind)
{
    if (!node)
        return {};
    const QString customTitle = node->data(0, kSolverCustomTitleRole).toString().trimmed();
    return customTitle.isEmpty() ? solverNodeBaseTitle(kind) : customTitle;
}

QString solverNodeDisplayTitle(const QString& title, const QString& kind, int defaultIndex,
                               bool customTitle)
{
    if (customTitle || !solverNodeUsesAutomaticIndex(kind) || defaultIndex <= 1)
        return title;
    return QStringLiteral("%1(%2)").arg(title).arg(defaultIndex);
}

void restoreSolverCustomTitle(ProjectNode* node, const QJsonObject& obj)
{
    if (!node)
        return;
    const QString customTitle = obj.value(QStringLiteral("custom_title")).toString().trimmed();
    if (!customTitle.isEmpty())
        node->setData(0, kSolverCustomTitleRole, customTitle);
}

void saveSolverCustomTitle(QJsonObject& obj, ProjectNode* node)
{
    if (!node)
        return;
    const QString customTitle = node->data(0, kSolverCustomTitleRole).toString().trimmed();
    if (!customTitle.isEmpty())
        obj[QStringLiteral("custom_title")] = customTitle;
}

void setSolverNodeTitle(ProjectNode* node, const QString& title)
{
    if (!node || title.isEmpty())
        return;
    node->setName(title);
    node->setText(0, title);
    if (node->getPage())
        node->getPage()->setDisplayName(title);
}

QStringList caveIcaveLineKeys()
{
    return {"line_3", "line_5", "line_7", "line_10", "line_11", "line_18", "line_19",
            "line_20", "line_21", "line_22", "line_23", "line_24", "line_25"};
}

bool hasAnyIcaveLineValue(const QJsonObject& obj)
{
    for (const QString& key : caveIcaveLineKeys())
    {
        if (!obj.value(key).toString().trimmed().isEmpty())
            return true;
    }
    return false;
}

bool hasAnyIcaveLineKey(const QJsonObject& obj)
{
    for (const QString& key : caveIcaveLineKeys())
    {
        if (obj.contains(key))
            return true;
    }
    return false;
}
}

Solver::Solver()
{
    ServiceRegistry::instance().registerService<SolverService>(this);
    initAction();
    initTreeMenu();
}

void Solver::newProject()
{
    if (m_root)
        closeProject();
    auto* rootPage = new SolverSettingWidget;
    connect(rootPage, &SolverSettingWidget::createSeepageRequested,
            this, &Solver::onSeepageAction);
    connect(rootPage, &SolverSettingWidget::createDamRequested,
            this, &Solver::onStressStrainAction);
    connect(rootPage, &SolverSettingWidget::createSlopeRequested,
            this, &Solver::onSlopeStabilityAction);
    connect(rootPage, &SolverSettingWidget::createCaveRequested,
            this, &Solver::onCaveExcavationAction);
    connect(rootPage, &SolverSettingWidget::createBlockRequested,
            this, &Solver::onCaveBlockStabilityAction);
    connect(rootPage, &SolverSettingWidget::batchComputeRequested,
            this, &Solver::onBatchComputeRequested);
    connect(rootPage, &SolverSettingWidget::batchStopRequested,
            this, &Solver::onBatchStopRequested);
    connect(rootPage, &SolverSettingWidget::rootActivated, this,
            [this]()
            {
                if (!m_root)
                    return;
                m_root->setExpanded(true);
                std::function<void(ProjectNode*)> collapseTree =
                    [&collapseTree](ProjectNode* node)
                    {
                        if (!node)
                            return;
                        node->setExpanded(false);
                        for (ProjectNode* child : node->getChildren())
                            collapseTree(child);
                    };
                for (ProjectNode* analysisNode : m_root->getChildren())
                    collapseTree(analysisNode);
            });
    m_root = new ProjectNode("计算分析", QUuid::createUuid().toString(), rootPage);
    setupNodeRole(m_root, "SolverRoot");
    ProjectManagerService* projectManagerService =
        ServiceRegistry::instance().getService<ProjectManagerService>();
    projectManagerService->addTreeItem(m_root);
    refreshRootOverview();
}

bool Solver::saveProject()
{
    if (!m_root)
        return true;

    QJsonObject root;
    root["module"] = "Solver";
    root["root_title"] = "计算分析";
    if (auto* rootPage = m_root->getPage())
        root["state"] = rootPage->saveJson();

    QJsonArray analysisItems;
    for (ProjectNode* analysisNode : m_root->getChildren())
    {
        if (!analysisNode)
            continue;

        QJsonObject analysisObj;
        analysisObj["uuid"] = analysisNode->getUuid();
        analysisObj["title"] = analysisNode->text(0);
        saveSolverCustomTitle(analysisObj, analysisNode);

        if (auto* analysisPage = analysisNode->getPage())
        {
            analysisObj["type"] = analysisPage->getPageType();
            analysisObj["state"] = analysisPage->saveJson();
        }

        QJsonArray boundaryItems;
        for (ProjectNode* child : analysisNode->getChildren())
        {
            if (!child || !child->getPage())
                continue;

            // 保存工程只序列化设置；求解器输入文件统一在点击计算后生成，
            // 避免普通保存操作改写或残留计算输入文件。
            QJsonObject childObj;
            childObj["uuid"] = child->getUuid();
            childObj["title"] = child->text(0);
            saveSolverCustomTitle(childObj, child);
            childObj["type"] = child->getPage()->getPageType();
            childObj["state"] = child->getPage()->saveJson();
            QJsonArray childItems;
            for (ProjectNode* gc : child->getChildren())
            {
                if (!gc || !gc->getPage())
                    continue;
                QJsonObject gcObj;
                gcObj["uuid"] = gc->getUuid();
                gcObj["title"] = gc->text(0);
                saveSolverCustomTitle(gcObj, gc);
                gcObj["type"] = gc->getPage()->getPageType();
                gcObj["state"] = gc->getPage()->saveJson();
                childItems.append(gcObj);
            }
            childObj["children"] = childItems;
            boundaryItems.append(childObj);
        }
        analysisObj["children"] = boundaryItems;
        analysisItems.append(analysisObj);
    }
    root["items"] = analysisItems;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));

    return true;
}

bool Solver::loadProject()
{
    newProject();
    if (!m_root)
        return false;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QFile file(QDir(projectDir).filePath(PublicConstants::kSolverProjectFileName));
    if (!file.open(QIODevice::ReadOnly))
        return true;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonObject rootState = root.value(QStringLiteral("state")).toObject();
    auto* rootSettingsPage = dynamic_cast<SolverSettingWidget*>(m_root->getPage());
    if (rootSettingsPage)
        rootSettingsPage->loadJson(rootState);
    for (const auto& item : root.value("items").toArray())
    {
        const QJsonObject analysisObj = item.toObject();
        const QJsonObject analysisState = analysisObj.value("state").toObject();
        const QString analysisType = analysisObj.value("type").toString();

        BasePage* analysisPage = nullptr;
        ProjectNode* analysisNode = nullptr;

        if (analysisType == "SeepageAnalysis")
        {
            auto* seepagePage = new SeepageWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("渗流分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), seepagePage);
            seepagePage->loadJson(analysisState);
            connect(seepagePage, &SeepageWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSeepage(analysisNode);
                    });
            connect(seepagePage, &SeepageWidget::requirementsChanged, this,
                    [this, analysisNode]() { syncSeepageConditionalNodes(analysisNode); });
            connectStopRequest(seepagePage);
            analysisPage = seepagePage;
        }
        else if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
        {
            auto* stressPage = new DamAnalysisWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("土石坝分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), stressPage);
            stressPage->loadJson(analysisState);
            connect(stressPage, &DamAnalysisWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeStressStrain(analysisNode);
                    });
            connect(stressPage, &DamAnalysisWidget::requirementsChanged, this,
                    [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
            connectDamLayerUpdates(stressPage, analysisNode);
            connectStopRequest(stressPage);
            analysisPage = stressPage;
        }
        else if (analysisType == "SlopeStabilityAnalysis")
        {
            auto* slopePage = new SlopeStabilityWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("边坡分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), slopePage);
            slopePage->loadJson(analysisState);
            connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeSlopeStability(analysisNode);
                    });
            connectStopRequest(slopePage);
            analysisPage = slopePage;
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            auto* cavePage = new CaveExcavationWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("洞室开挖工程"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), cavePage);
            cavePage->loadJson(analysisState);
            connectCaveOverview(cavePage, analysisNode);
            analysisPage = cavePage;
        }
        else if (analysisType == "CaveBlockStabilityAnalysis")
        {
            auto* blockPage = new CaveBlockStabilityWidget();
            analysisNode = new ProjectNode(
                analysisObj.value("title").toString("块体稳定分析"),
                analysisObj.value("uuid").toString(QUuid::createUuid().toString()), blockPage);
            blockPage->loadJson(analysisState);
            connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
                    [this, analysisNode]()
                    {
                        onComputeCaveBlockStability(analysisNode);
                    });
            connectStopRequest(blockPage);
            analysisPage = blockPage;
        }

        if (!analysisNode || !analysisPage)
            continue;

        bool hasFillStorageCycleNode = false;
        bool hasDamDynamicAnalysisNode = false;
        QJsonObject legacyDamDynamicState;
        bool hasSeepageUpstreamBoundaryNode = false;
        bool hasSeepageDownstreamBoundaryNode = false;
        bool hasSeepageNaturalBoundaryNode = false;
        bool hasSeepageDrainWellNode = false;
        bool hasDamUpstreamBoundaryNode = false;
        bool hasDamIceRegionNode = false;
        bool hasGeoStressRegressionNode = false;
        bool hasCaveSupportComputationNode = false;
        QJsonObject legacyCaveReinforcementState;
        for (const auto& childV : analysisObj.value("children").toArray())
        {
            const QJsonObject childObj = childV.toObject();
            const QString type = childObj.value("type").toString();

            // 旧工程可能把“围岩等效加固”保存为独立树节点。新版将它并入
            // “开挖与加固分析”页面，只迁移状态，不再恢复独立节点。
            if (type == QStringLiteral("CaveReinforcementField"))
            {
                legacyCaveReinforcementState = childObj.value("state").toObject();
                continue;
            }

            BasePage* page = nullptr;
            if (type == "SeepageBoundaryUpstreamWater")
            {
                page = new SeepageBoundaryPage(type, "上游渗流边界", "JD_UPSEEP.DAT");
                hasSeepageUpstreamBoundaryNode = true;
            }
            else if (type == "SeepageBoundaryDownstreamWater")
            {
                page = new SeepageBoundaryPage(type, "下游渗流边界", "JD_DWSEEP.DAT");
                hasSeepageDownstreamBoundaryNode = true;
            }
            else if (type == "SeepageBoundaryNaturalSeepage")
            {
                page = new SeepageBoundaryPage(type, "天然渗流初始面", "JDFREE.DAT");
                hasSeepageNaturalBoundaryNode = true;
            }
            else if (type == "SeepageBoundaryDrainWell")
            {
                page = new SeepageBoundaryPage(type, "排水井区域", "JDJING.DAT", nullptr, true);
                hasSeepageDrainWellNode = true;
            }
            else if (type == "StressBoundaryDamUpstream")
            {
                page = new StressBoundaryPage(type, "上游水压力作用面", "FA_DAMUP.DAT");
                hasDamUpstreamBoundaryNode = true;
            }
            else if (type == "StressBoundaryDamBase")
            {
                // 旧工程兼容：当前只提供均质坝，求解器固定忽略 IFIIWL_SEEP，
                // 因此不再恢复无效的坝基扬压力边界节点。
                continue;
            }
            else if (type == "DamIceRegion")
            {
                page = new SeepageBoundaryPage(type, "结冰区域", "JMFW.DAT", nullptr, true);
                hasDamIceRegionNode = true;
            }
            else if (type == "FillStorageCycle")
            {
                page = new FillStorageCyclePage();
                hasFillStorageCycleNode = true;
            }
            else if (type == "DamDynamicAnalysis")
            {
                page = new DamDynamicAnalysisPage();
                hasDamDynamicAnalysisNode = true;
            }
            else if (type == "GeoStressRegressionAnalysis")
            {
                page = new GeoStressRegressionWidget();
                hasGeoStressRegressionNode = true;
            }
            else if (type == "GeoStressRegressionSideFace")
            {
                page = new GeoStressSideFacePage();
            }
            else if (type == "GeoStressRegressionMeasurePoint")
            {
                page = new GeoStressMeasurePointPage();
            }
            else if (type == "GeoStressRegressionOuterForce")
            {
                page = new GeoStressOuterForcePage();
            }
            else if (type == "CaveSupportComputation")
            {
                page = new CaveSupportComputationPage();
                hasCaveSupportComputationNode = true;
            }
            else if (type == "CaveCraneBeamArea")
            {
                page = new CaveCraneBeamAreaPage();
            }
            else if (type == "CaveExcavationBody")
            {
                auto* bodyPage = new CaveExcavationBodyPage();
                bodyPage->setExcavationLayer(
                    -(countGeoStressChildren(analysisNode, QStringLiteral("CaveExcavationBody"))
                      + 1));
                page = bodyPage;
            }
            else if (type == "CaveBlockStabilityAnalysis")
            {
                page = new CaveBlockStabilityWidget();
            }
            else if (type == "CaveBlockPlane" || type == "CaveBlockPlanePage")
            {
                page = new CaveBlockPlanePage();
            }

            if (!page)
                continue;

            // 边界节点名称描述的是长期稳定的业务对象，而不是当前边界类型。
            // 旧工程中可能保存为“上游水头边界”或“上游定通量边界”，
            // 加载时统一迁移，切换水位/通量后树节点名称不再变化。
            QString childTitle = childObj.value("title").toString("边界");
            if (type == QStringLiteral("SeepageBoundaryUpstreamWater"))
                childTitle = QStringLiteral("上游渗流边界");
            else if (type == QStringLiteral("SeepageBoundaryDownstreamWater"))
                childTitle = QStringLiteral("下游渗流边界");
            else if (type == QStringLiteral("SeepageBoundaryNaturalSeepage"))
                childTitle = QStringLiteral("天然渗流初始面");
            else if (type == QStringLiteral("StressBoundaryDamUpstream"))
                childTitle = QStringLiteral("上游水压力作用面");
            else if (type == QStringLiteral("FillStorageCycle"))
                childTitle = QStringLiteral("填筑蓄水");
            else if (type == QStringLiteral("DamDynamicAnalysis"))
                childTitle = QStringLiteral("动力分析");

            auto* node = new ProjectNode(
                childTitle,
                childObj.value("uuid").toString(QUuid::createUuid().toString()), page);
            if (type == "FillStorageCycle")
                setupNodeRole(node, "FillStorageCycle");
            else if (type == "DamDynamicAnalysis")
                setupNodeRole(node, "DamDynamicAnalysis");
            else if (type == "GeoStressRegressionAnalysis")
                setupNodeRole(node, "GeoStressRegression");
            else if (type == "GeoStressRegressionSideFace")
                setupNodeRole(node, "GeoStressRegressionSideFace");
            else if (type == "GeoStressRegressionMeasurePoint")
                setupNodeRole(node, "GeoStressRegressionMeasurePoint");
            else if (type == "GeoStressRegressionOuterForce")
                setupNodeRole(node, "GeoStressRegressionOuterForce");
            else if (type == "CaveSupportComputation")
                setupNodeRole(node, "CaveSupportComputation");
            else if (type == "CaveCraneBeamArea")
                setupNodeRole(node, "CaveCraneBeamArea");
            else if (type == "CaveExcavationBody")
                setupNodeRole(node, "CaveExcavationBody");
            else if (type == "CaveBlockStabilityAnalysis")
                setupNodeRole(node, "CaveBlockStabilityAnalysis");
            else if (type == "CaveBlockPlane" || type == "CaveBlockPlanePage")
                setupNodeRole(node, "CaveBlockPlane");
            else if (type.startsWith("StressBoundary"))
                setupNodeRole(node, "StressBoundary");
            else if (type == QStringLiteral("SeepageBoundaryNaturalSeepage"))
                setupNodeRole(node, "SeepageNaturalBoundary");
            else if (type.startsWith("SeepageBoundary"))
                setupNodeRole(node, "SeepageBoundary");
            else if (type == "DamIceRegion")
                setupNodeRole(node, "DamIceRegion");
            restoreSolverCustomTitle(node, childObj);

            if (auto* geoPage = dynamic_cast<GeoStressRegressionWidget*>(page))
            {
                connect(geoPage, &GeoStressRegressionWidget::computeRequested, this,
                        [this, node]()
                        {
                            onComputeGeoStressRegression(node);
                        });
            connectStopRequest(geoPage);
            }
            else if (auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(page))
            {
                connect(supportPage, &CaveSupportComputationPage::computeRequested, this,
                        [this, analysisNode]()
                        {
                            onComputeCaveExcavation(analysisNode);
                        });
                connect(supportPage, &CaveSupportComputationPage::liningEnabledChanged, this,
                        [this, node](bool)
                        {
                            syncCaveSupportConditionalNodes(node);
                        });
                connect(supportPage, &CaveSupportComputationPage::craneBeamEnabledChanged, this,
                        [this, node](bool)
                        {
                            syncCaveSupportConditionalNodes(node);
                        });
                connectStopRequest(supportPage);
            }
            else if (auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(page))
            {
                connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
                        [this, node]()
                        {
                            onComputeCaveBlockStability(node);
                        });
            connectStopRequest(blockPage);
            }
            else if (auto* cyclePage = dynamic_cast<FillStorageCyclePage*>(page))
            {
                connect(cyclePage, &FillStorageCyclePage::requirementsChanged, this,
                        [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
            }
            else if (auto* dynamicPage = dynamic_cast<DamDynamicAnalysisPage*>(page))
            {
                connect(dynamicPage, &DamDynamicAnalysisPage::requirementsChanged, this,
                        [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
                connect(dynamicPage, &DamDynamicAnalysisPage::modalExtractionOrderChanged, this,
                        [this, analysisNode](int order)
                        {
                            if (auto* damPage =
                                    dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage()))
                                damPage->setModalExtractionOrder(order);
                        });
            }
            QJsonObject childState = childObj.value("state").toObject();
            if (dynamic_cast<FillStorageCyclePage*>(page))
            {
                // 旧版把动力任务和静动材料转换存放在“填筑蓄水/计算工况”页面。
                // 先记录下来，加载结束后迁移到新的“动力分析”兄弟节点。
                QJsonObject legacyState = childState;
                const QJsonArray rootMappings =
                    analysisState.value(QStringLiteral("dynamic_material_table")).toArray();
                if (legacyState.value(QStringLiteral("dynamic_material_table")).toArray().isEmpty()
                    && !rootMappings.isEmpty())
                    legacyState[QStringLiteral("dynamic_material_table")] = rootMappings;
                if (legacyDamDynamicState.isEmpty())
                    legacyDamDynamicState = legacyState;
            }
            page->loadJson(childState);
            if (analysisType == "CaveExcavationAnalysis" && type == "CaveSupportComputation"
                && !hasAnyIcaveLineKey(childState) && hasAnyIcaveLineValue(analysisState))
            {
                if (auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(page))
                    supportPage->loadLegacyCaveJson(analysisState);
            }
            analysisNode->addChild(node);
            if (dynamic_cast<SeepageBoundaryPage*>(page)
                || dynamic_cast<StressBoundaryPage*>(page))
            {
                connectBoundaryStatus(node);
            }
            if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(page))
            {
                connect(bodyPage, &CaveExcavationBodyPage::excavationLayerChanged, this,
                        [this, analysisNode](int) { syncChildNodeNames(analysisNode); });
            }

            if (type == "GeoStressRegressionAnalysis" || type == "CaveSupportComputation"
                || type == "CaveBlockStabilityAnalysis")
            {
                for (const auto& gcV : childObj.value("children").toArray())
                {
                    const QJsonObject gcObj = gcV.toObject();
                    const QString gcType = gcObj.value("type").toString();
                    if (gcType == QStringLiteral("CaveReinforcementField"))
                    {
                        if (auto* supportPage = dynamic_cast<CaveSupportComputationPage*>(page))
                            supportPage->loadLegacyReinforcementFieldJson(
                                gcObj.value("state").toObject());
                        continue;
                    }
                    BasePage* gcPage = nullptr;
                    if (gcType == "GeoStressRegressionSideFace")
                        gcPage = new GeoStressSideFacePage();
                    else if (gcType == "GeoStressRegressionMeasurePoint")
                        gcPage = new GeoStressMeasurePointPage();
                    else if (gcType == "GeoStressRegressionOuterForce")
                        gcPage = new GeoStressOuterForcePage();
                    else if (gcType == "CaveLiningInnerBoundary")
                    {
                        gcPage = new StressBoundaryPage(
                            gcType, QStringLiteral("衬砌内边界"),
                            QStringLiteral("IIJX_CHEN.DAT"));
                    }
                    else if (gcType == "CaveLiningOuterBoundary")
                    {
                        gcPage = new StressBoundaryPage(
                            gcType, QStringLiteral("衬砌外边界"),
                            QStringLiteral("IIJX_WAI.DAT"));
                    }
                    else if (gcType == "CaveCraneBeamArea")
                    {
                        gcPage = new CaveCraneBeamAreaPage();
                    }
                    else if (gcType == "CaveExcavationBody")
                    {
                        auto* bodyPage = new CaveExcavationBodyPage();
                        // 旧工程没有显式层号时，沿用原有的树顺序生成 -1、-2……；
                        // 新工程的 loadJson 会用保存的 excavation_layer 覆盖该兼容值。
                        bodyPage->setExcavationLayer(
                            -(countGeoStressChildren(node, QStringLiteral("CaveExcavationBody"))
                              + 1));
                        gcPage = bodyPage;
                    }
                    else if (gcType == "CaveBlockPlane" || gcType == "CaveBlockPlanePage")
                        gcPage = new CaveBlockPlanePage();
                    if (!gcPage)
                        continue;

                    auto* gcNode = new ProjectNode(
                        gcObj.value("title").toString("子项"),
                        gcObj.value("uuid").toString(QUuid::createUuid().toString()), gcPage);
                    if (gcType == "GeoStressRegressionSideFace")
                        setupNodeRole(gcNode, "GeoStressRegressionSideFace");
                    else if (gcType == "GeoStressRegressionMeasurePoint")
                        setupNodeRole(gcNode, "GeoStressRegressionMeasurePoint");
                    else if (gcType == "GeoStressRegressionOuterForce")
                        setupNodeRole(gcNode, "GeoStressRegressionOuterForce");
                    else if (gcType == "CaveLiningInnerBoundary")
                        setupNodeRole(gcNode, "CaveLiningInnerBoundary");
                    else if (gcType == "CaveLiningOuterBoundary")
                        setupNodeRole(gcNode, "CaveLiningOuterBoundary");
                    else if (gcType == "CaveCraneBeamArea")
                        setupNodeRole(gcNode, "CaveCraneBeamArea");
                    else if (gcType == "CaveExcavationBody")
                        setupNodeRole(gcNode, "CaveExcavationBody");
                    else if (gcType == "CaveBlockPlane" || gcType == "CaveBlockPlanePage")
                        setupNodeRole(gcNode, "CaveBlockPlane");
                    restoreSolverCustomTitle(gcNode, gcObj);

                    gcPage->loadJson(gcObj.value("state").toObject());
                    node->addChild(gcNode);
                    if (dynamic_cast<StressBoundaryPage*>(gcPage))
                        connectBoundaryStatus(gcNode);
                    if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(gcPage))
                    {
                        connect(bodyPage, &CaveExcavationBodyPage::excavationLayerChanged,
                                this, [this, node](int) { syncChildNodeNames(node); });
                    }
                    if (auto* cranePage = dynamic_cast<CaveCraneBeamAreaPage*>(gcPage))
                    {
                        connect(cranePage, &CaveCraneBeamAreaPage::selectionChanged, this,
                                [this, node](int) { syncCaveSupportConditionalNodes(node); });
                    }
                }
            }
        }

        if (analysisType == QStringLiteral("CaveExcavationAnalysis")
            && !legacyCaveReinforcementState.isEmpty())
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (auto* supportPage = child
                        ? dynamic_cast<CaveSupportComputationPage*>(child->getPage()) : nullptr)
                {
                    supportPage->loadLegacyReinforcementFieldJson(
                        legacyCaveReinforcementState);
                    break;
                }
            }
        }

        if (analysisType == "SeepageAnalysis")
        {
            auto addMissingBoundary = [this, analysisNode](const QString& title,
                                                           const QString& type,
                                                           const QString& fileName,
                                                           bool removeWhenEmpty)
            {
                auto* page = new SeepageBoundaryPage(type, title, fileName, nullptr,
                                                     removeWhenEmpty);
                auto* node = new ProjectNode(title, QUuid::createUuid().toString(), page);
                setupNodeRole(node,
                              type == QStringLiteral("SeepageBoundaryNaturalSeepage")
                                  ? QStringLiteral("SeepageNaturalBoundary")
                                  : QStringLiteral("SeepageBoundary"));
                analysisNode->addChild(node);
                connectBoundaryStatus(node);
            };
            if (!hasSeepageUpstreamBoundaryNode)
                addMissingBoundary(QStringLiteral("上游渗流边界"),
                                   QStringLiteral("SeepageBoundaryUpstreamWater"),
                                   QStringLiteral("JD_UPSEEP.DAT"), false);
            if (!hasSeepageDownstreamBoundaryNode)
                addMissingBoundary(QStringLiteral("下游渗流边界"),
                                   QStringLiteral("SeepageBoundaryDownstreamWater"),
                                   QStringLiteral("JD_DWSEEP.DAT"), false);
            if (!hasSeepageNaturalBoundaryNode)
                addMissingBoundary(QStringLiteral("天然渗流初始面"),
                                   QStringLiteral("SeepageBoundaryNaturalSeepage"),
                                   QStringLiteral("JDFREE.DAT"), false);
            if (!hasSeepageDrainWellNode)
                addMissingBoundary(QStringLiteral("排水井区域"),
                                   QStringLiteral("SeepageBoundaryDrainWell"),
                                   QStringLiteral("JDJING.DAT"), true);

            auto* seepagePage = dynamic_cast<SeepageWidget*>(analysisNode->getPage());
            SeepageBoundaryPage* upstreamPage = nullptr;
            SeepageBoundaryPage* downstreamPage = nullptr;
            for (ProjectNode* child : analysisNode->getChildren())
            {
                auto* boundary = child
                    ? dynamic_cast<SeepageBoundaryPage*>(child->getPage()) : nullptr;
                if (!boundary)
                    continue;
                if (boundary->getPageType()
                    == QStringLiteral("SeepageBoundaryUpstreamWater"))
                    upstreamPage = boundary;
                else if (boundary->getPageType()
                         == QStringLiteral("SeepageBoundaryDownstreamWater"))
                    downstreamPage = boundary;
            }
            if (seepagePage)
                seepagePage->setBoundaryPages(upstreamPage, downstreamPage);
        }

        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis") && !hasFillStorageCycleNode)
        {
            addFillStorageCycleNode(analysisNode, false);
        }
        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            && !hasDamDynamicAnalysisNode)
        {
            addDamDynamicAnalysisNode(analysisNode, false, legacyDamDynamicState);
        }
        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            && !hasDamUpstreamBoundaryNode)
        {
            auto* page = new StressBoundaryPage("StressBoundaryDamUpstream", "上游水压力作用面",
                                                "FA_DAMUP.DAT");
            auto* node = new ProjectNode("上游水压力作用面", QUuid::createUuid().toString(), page);
            setupNodeRole(node, "StressBoundary");
            analysisNode->addChild(node);
            connectBoundaryStatus(node);
        }
        if ((analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            && !hasDamIceRegionNode)
        {
            auto* page =
                new SeepageBoundaryPage("DamIceRegion", "结冰区域", "JMFW.DAT", nullptr, true);
            auto* node = new ProjectNode("结冰区域", QUuid::createUuid().toString(), page);
            setupNodeRole(node, "DamIceRegion");
            analysisNode->addChild(node);
            connectBoundaryStatus(node);
        }
        if ((analysisType == "CaveExcavationAnalysis")
            && !hasGeoStressRegressionNode)
        {
            addGeoStressRegressionNode(analysisNode, false);
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (!child || !child->getPage()
                    || child->getPage()->getPageType() != "GeoStressRegressionAnalysis")
                    continue;
                while (countGeoStressChildren(child, "GeoStressRegressionSideFace")
                       < minimumGeoStressSideFaceCount())
                    addGeoStressSideFaceNode(child, false);
                if (countGeoStressChildren(child, "GeoStressRegressionMeasurePoint") == 0)
                    addGeoStressMeasurePointNode(child, false);
                if (countGeoStressChildren(child, "GeoStressRegressionOuterForce") == 0)
                    addGeoStressOuterForceNode(child, false);
            }
        }


        if (analysisType == "CaveExcavationAnalysis" && !hasCaveSupportComputationNode)
        {
            addCaveSupportComputationNode(analysisNode, false);
        }
        else if (analysisType == "CaveExcavationAnalysis")
        {
            for (ProjectNode* child : analysisNode->getChildren())
            {
                if (!child || !child->getPage()
                    || child->getPage()->getPageType() != "CaveSupportComputation")
                    continue;
                if (countGeoStressChildren(child, "CaveLiningInnerBoundary") == 0)
                    addCaveLiningBoundaryNode(child, true, false);
                if (countGeoStressChildren(child, "CaveLiningOuterBoundary") == 0)
                    addCaveLiningBoundaryNode(child, false, false);
                if (countGeoStressChildren(child, "CaveCraneBeamArea") == 0)
                    addCaveCraneBeamAreaNode(child, false);
                if (countGeoStressChildren(child, "CaveExcavationBody") == 0)
                    addCaveExcavationBodyNode(child, false);

                auto* supportPage =
                    dynamic_cast<CaveSupportComputationPage*>(child->getPage());
                auto* craneNode = findChildByPageType(child, QStringLiteral("CaveCraneBeamArea"));
                auto* cranePage = craneNode
                    ? dynamic_cast<CaveCraneBeamAreaPage*>(craneNode->getPage()) : nullptr;
                if (supportPage && cranePage && cranePage->selectionCount() == 0)
                {
                    const QJsonObject legacyState = supportPage->legacyCraneBeamAreaState();
                    if (!legacyState.isEmpty())
                        cranePage->loadJson(legacyState);
                }
                syncCaveSupportConditionalNodes(child);
            }
        }
        if (analysisType == "CaveBlockStabilityAnalysis"
            && countGeoStressChildren(analysisNode, "CaveBlockPlane") == 0)
            addCaveBlockPlaneNode(analysisNode, false);

        if (analysisType == "StressStrainAnalysis" || analysisType == "DamAnalysis")
            setupNodeRole(analysisNode, "DamAnalysis");
        else if (analysisType == "SeepageAnalysis")
            setupNodeRole(analysisNode, "SeepageAnalysis");
        else if (analysisType == "SlopeStabilityAnalysis")
            setupNodeRole(analysisNode, "SlopeStabilityAnalysis");
        else if (analysisType == "CaveExcavationAnalysis")
            setupNodeRole(analysisNode, "CaveExcavationAnalysis");
        else if (analysisType == "CaveBlockStabilityAnalysis")
            setupNodeRole(analysisNode, "CaveBlockStabilityAnalysis");
        restoreSolverCustomTitle(analysisNode, analysisObj);

        normalizeChildNodeOrder(analysisNode);
        syncDamAnalysisMCase(analysisNode);
        syncChildNodeNames(analysisNode);
        for (ProjectNode* child : analysisNode->getChildren())
            syncChildNodeNames(child);
        m_root->addChild(analysisNode);
        // 工程打开时只展示计算分析的直接子节点，不递归展开每个分析。
        analysisNode->setExpanded(false);
        syncSeepageConditionalNodes(analysisNode);
        syncDamConditionalNodes(analysisNode);
    }

    syncAnalysisNodeNames();

    return true;
}

void Solver::closeProject()
{
    m_batchActive = false;
    m_batchDispatching = false;
    m_batchQueue.clear();
    m_batchCurrentUuid.clear();
    if (m_solverProcess)
        finishSolverProcess(true);
    clearSolverRunDirectory();
    if (!m_root)
        return;

    ProjectNode* root = m_root;
    m_root = nullptr;

    if (auto* parent = dynamic_cast<ProjectNode*>(root->parent()))
        parent->removeChild(root);
    else if (QTreeWidget* tree = root->treeWidget())
    {
        const int index = tree->indexOfTopLevelItem(root);
        if (index >= 0)
            tree->takeTopLevelItem(index);
    }

    deleteNodeRecursively(root);
}

void Solver::initAction()
{
    auto& em = EventManager::instance();

    em.registerHandler(PublicConstants::kActionSeepageKey,
                       [this](const QVariantMap&)
                       {
                           onSeepageAction();
                       });
    em.registerHandler(PublicConstants::kActionStressStrainKey,
                       [this](const QVariantMap&)
                       {
                           onStressStrainAction();
                       });
    em.registerHandler(PublicConstants::kActionCaveExcavationKey,
                       [this](const QVariantMap&)
                       {
                           onCaveExcavationAction();
                       });
    em.registerHandler(PublicConstants::kActionCaveBlockStabilityKey,
                       [this](const QVariantMap&)
                       {
                           onCaveBlockStabilityAction();
                       });
    em.registerHandler(PublicConstants::kActionSlopeStabilityKey,
                       [this](const QVariantMap&)
                       {
                           onSlopeStabilityAction();
                       });
    em.registerHandler(PublicConstants::kActionSpecificationCheckKey,
                       [this](const QVariantMap&)
                       {
                           onSpecificationCheckAction();
                       });
}

void Solver::initTreeMenu()
{
    ContextMenuHub::instance().registerProvider(
        "project_tree.node", this,
        [this](QMenu* menu, const QVariantMap& ctx)
        {
            const QString uuid = ctx.value("uuid").toString();
            const QString kind = ctx.value("kind").toString();
            if (uuid.isEmpty())
                return;

            auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
            auto* menuNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
            if (!menuNode)
                return;

            if (kind == QStringLiteral("SolverRoot"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "新增渗流分析",
                                          [this]() { onSeepageAction(); });
                ContextMenuHub::addAction(menu, this, "新增土石坝分析",
                                          [this]() { onStressStrainAction(); });
                ContextMenuHub::addAction(menu, this, "新增边坡分析",
                                          [this]() { onSlopeStabilityAction(); });
                ContextMenuHub::addAction(menu, this, "新增洞室开挖工程",
                                          [this]() { onCaveExcavationAction(); });
                ContextMenuHub::addAction(menu, this, "新增块体稳定分析",
                                          [this]() { onCaveBlockStabilityAction(); });
                return;
            }

            if (kind == QStringLiteral("SeepageAnalysis"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "添加天然渗流初始面",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (analysisNode)
                            addSeepageNaturalBoundaryNode(analysisNode, true);
                    });
            }
            else if (kind == QStringLiteral("CaveExcavationAnalysis"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "定位地应力回归分析",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (analysisNode)
                            addGeoStressRegressionNode(analysisNode, true);
                    });
                ContextMenuHub::addAction(
                    menu, this, "定位开挖与加固分析",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* analysisNode = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (analysisNode)
                            addCaveSupportComputationNode(analysisNode, true);
                    });
            }
            else if (kind == QStringLiteral("CaveSupportComputation"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增开挖体",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (node)
                            addCaveExcavationBodyNode(node, true);
                    });
            }
            else if (kind == QStringLiteral("CaveBlockStabilityAnalysis"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增滑块面",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (node)
                            addCaveBlockPlaneNode(node, true);
                    });
            }
            else if (kind == QStringLiteral("GeoStressRegression"))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(
                    menu, this, "新增侧立面",
                    [this, uuid]()
                    {
                        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
                        auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
                        if (node)
                            addGeoStressSideFaceNode(node, true);
                    });
            }

            if (solverNodeCanMove(kind))
            {
                auto* parent = dynamic_cast<ProjectNode*>(menuNode->parent());
                bool hasPrevious = false;
                bool hasNext = false;
                if (parent)
                {
                    const auto siblings = parent->getChildren();
                    int current = -1;
                    for (int i = 0; i < siblings.size(); ++i)
                    {
                        if (siblings.at(i) == menuNode)
                        {
                            current = i;
                            break;
                        }
                    }
                    for (int i = current - 1; i >= 0; --i)
                    {
                        if (siblings.at(i)->data(0, ProjectNode::RoleKind).toString() == kind)
                        {
                            hasPrevious = true;
                            break;
                        }
                    }
                    for (int i = current + 1; i < siblings.size(); ++i)
                    {
                        if (siblings.at(i)->data(0, ProjectNode::RoleKind).toString() == kind)
                        {
                            hasNext = true;
                            break;
                        }
                    }
                }
                if (hasPrevious || hasNext)
                    ContextMenuHub::addSeparator(menu);
                if (hasPrevious)
                    ContextMenuHub::addAction(menu, this, "上移",
                                              [this, uuid]() { moveNodeByUuid(uuid, -1); });
                if (hasNext)
                    ContextMenuHub::addAction(menu, this, "下移",
                                              [this, uuid]() { moveNodeByUuid(uuid, 1); });
            }

            if (solverNodeCanRename(kind))
            {
                ContextMenuHub::addSeparator(menu);
                ContextMenuHub::addAction(menu, this, "重命名",
                                          [this, uuid]() { renameNodeByUuid(uuid); });
            }
            if (canDeleteNode(menuNode))
            {
                ContextMenuHub::addAction(menu, this, "删除",
                                          [this, uuid]() { removeNodeByUuid(uuid); });
            }
        },
        0,
        [](const QVariantMap& ctx) -> bool
        {
            return ctx.value("module").toString() == "Solver";
        });
}

void Solver::deleteNodeRecursively(ProjectNode* node) const
{
    if (!node)
        return;

    const auto children = node->getChildren();
    for (ProjectNode* child : children)
    {
        // QTreeWidgetItem owns its attached children.  Detach each child
        // before deleting it recursively; otherwise deleting the parent below
        // asks Qt to delete the same child a second time and corrupts the heap
        // during project switching.
        node->removeChild(child);
        deleteNodeRecursively(child);
    }

    if (BasePage* page = node->getPage())
    {
        node->setPage(nullptr);
        delete page;
    }

    delete node;
}

void Solver::removeNodeByUuid(const QString& uuid)
{
    if (!m_root || uuid.isEmpty())
        return;
    if (m_batchActive)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止批量计算，再删除计算节点。"));
        return;
    }

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node || node == m_root)
        return;

    if (node->data(0, ProjectNode::RoleModule).toString() != "Solver")
        return;
    if (!canDeleteNode(node))
        return;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (!parent)
        return;

    const bool parentWasExpanded = parent->isExpanded();
    const int removedIndex = parent->indexOfChild(node);
    ProjectNode* fallbackSelection = nullptr;
    if (removedIndex >= 0 && removedIndex + 1 < parent->childCount())
        fallbackSelection = dynamic_cast<ProjectNode*>(parent->child(removedIndex + 1));
    else if (removedIndex > 0)
        fallbackSelection = dynamic_cast<ProjectNode*>(parent->child(removedIndex - 1));
    if (!fallbackSelection)
        fallbackSelection = parent;

    const QString removedKind =
        node->data(0, ProjectNode::RoleKind).toString();
    const bool removesAnalysis = parent == m_root;
    const QString workspaceToRemove = removesAnalysis ? analysisWorkspace(node) : QString();

    if (QMessageBox::question(nullptr, QStringLiteral("删除计算项"),
                              removesAnalysis
                                  ? QStringLiteral("确定删除“%1”及其计算结果吗？")
                                        .arg(node->getName())
                                  : QStringLiteral("确定删除“%1”吗？").arg(node->getName()),
                              QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    if (removesAnalysis && m_activeAnalysisUuid == node->getUuid())
        finishSolverProcess(true);

    parent->removeChild(node);
    deleteNodeRecursively(node);
    if (!workspaceToRemove.isEmpty())
        QDir(workspaceToRemove).removeRecursively();
    syncChildNodeNames(parent);

    if (parent->data(0, ProjectNode::RoleKind).toString() == "DamAnalysis")
    {
        syncDamAnalysisMCase(parent);
        syncDamConditionalNodes(parent);
    }
    if (parent == m_root)
        syncAnalysisNodeNames();

    parent->setExpanded(parentWasExpanded);
    if (pm && fallbackSelection)
        pm->setCurrentItem(fallbackSelection);

    // 删除渗流工况后，立即刷新土石坝页面中的关联状态。
    if (removesAnalysis && removedKind == QStringLiteral("SeepageAnalysis"))
    {
        for (ProjectNode* analysis : m_root->getChildren())
        {
            if (analysis &&
                analysis->data(0, ProjectNode::RoleKind).toString()
                    == QStringLiteral("DamAnalysis"))
                syncDamConditionalNodes(analysis);
        }
    }
}

void Solver::renameNodeByUuid(const QString& uuid)
{
    if (!m_root || uuid.isEmpty())
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    ProjectNode* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node || node == m_root
        || node->data(0, ProjectNode::RoleModule).toString() != QStringLiteral("Solver"))
        return;

    ProjectNode* analysisOwner = topLevelAnalysisNode(node);
    if (m_solverProcess && analysisOwner == node &&
        m_activeAnalysisUuid == analysisOwner->getUuid())
    {
        QMessageBox::information(nullptr, QStringLiteral("计算正在进行"),
                                 QStringLiteral("请在当前计算结束后重命名该分析。"));
        return;
    }

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    if (!solverNodeCanRename(kind))
        return;
    const bool automaticIndex = solverNodeUsesAutomaticIndex(kind);
    QString currentTitle = node->data(0, kSolverCustomTitleRole).toString().trimmed();
    if (currentTitle.isEmpty())
        currentTitle = automaticIndex ? solverNodeBaseTitle(kind) : node->text(0);

    bool ok = false;
    QString newTitle = QInputDialog::getText(
        nullptr, QStringLiteral("重命名计算项"), QStringLiteral("请输入新名称："),
        QLineEdit::Normal, currentTitle, &ok);
    if (!ok)
        return;

    newTitle = newTitle.trimmed();
    if (automaticIndex)
    {
        static const QRegularExpression suffixPattern(QStringLiteral("\\s*\\(\\d+\\)\\s*$"));
        newTitle.remove(suffixPattern);
        newTitle = newTitle.trimmed();
    }
    if (newTitle.isEmpty())
    {
        QMessageBox::warning(nullptr, QStringLiteral("名称无效"),
                             QStringLiteral("名称不能为空。"));
        return;
    }
    if (newTitle == currentTitle && solverNodeHasCustomTitle(node))
        return;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    const QString displayTitle = newTitle;
    if (parent)
    {
        for (ProjectNode* sibling : parent->getChildren())
        {
            if (sibling != node
                && sibling->text(0).compare(displayTitle, Qt::CaseInsensitive) == 0)
            {
                QMessageBox::warning(nullptr, QStringLiteral("名称重复"),
                                     QStringLiteral("同一级中已存在“%1”，请使用其他名称。")
                                         .arg(displayTitle));
                return;
            }
        }
    }

    node->setData(0, kSolverCustomTitleRole, newTitle);
    if (automaticIndex && parent == m_root)
        syncAnalysisNodeNames();
    else if (automaticIndex && parent)
        syncChildNodeNames(parent);
    else
        setSolverNodeTitle(node, newTitle);

    if (parent == m_root)
        updateAnalysisManifestName(node);
}

void Solver::moveNodeByUuid(const QString& uuid, int direction)
{
    if (!m_root || uuid.isEmpty() || (direction != -1 && direction != 1))
        return;

    auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
    auto* node = pm ? pm->findNodeByUuid(uuid) : nullptr;
    if (!node || !solverNodeCanMove(node->data(0, ProjectNode::RoleKind).toString()))
        return;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (!parent)
        return;

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    QList<QTreeWidgetItem*> items = parent->takeChildren();
    int current = -1;
    for (int i = 0; i < items.size(); ++i)
    {
        if (items.at(i) == node)
        {
            current = i;
            break;
        }
    }

    if (current < 0)
    {
        parent->addChildren(items);
        return;
    }

    int target = -1;
    for (int i = current + direction; i >= 0 && i < items.size(); i += direction)
    {
        auto* sibling = dynamic_cast<ProjectNode*>(items.at(i));
        if (sibling && sibling->data(0, ProjectNode::RoleKind).toString() == kind)
        {
            target = i;
            break;
        }
    }

    if (current >= 0 && target >= 0)
        items.swapItemsAt(current, target);
    parent->addChildren(items);

    if (target < 0)
        return;

    syncChildNodeNames(parent);
    if (parent->data(0, ProjectNode::RoleKind).toString() == QStringLiteral("DamAnalysis"))
    {
        syncDamAnalysisMCase(parent);
        syncDamConditionalNodes(parent);
    }
    parent->setExpanded(true);
    if (pm)
        pm->setCurrentItem(node);
}

void Solver::normalizeChildNodeOrder(ProjectNode* parent) const
{
    if (!parent || parent->childCount() < 2)
        return;

    QList<QTreeWidgetItem*> originalItems;
    originalItems.reserve(parent->childCount());
    for (int i = 0; i < parent->childCount(); ++i)
        originalItems.push_back(parent->child(i));

    QList<QTreeWidgetItem*> sortedItems = originalItems;
    std::stable_sort(sortedItems.begin(), sortedItems.end(),
                     [parent](QTreeWidgetItem* lhs, QTreeWidgetItem* rhs)
                     {
                         return solverChildOrderRank(parent, dynamic_cast<ProjectNode*>(lhs))
                                < solverChildOrderRank(parent, dynamic_cast<ProjectNode*>(rhs));
                     });

    // 顺序没有变化时不要重新挂接节点，否则 Qt 会重置整棵子树的展开状态。
    if (sortedItems == originalItems)
        return;

    QHash<QTreeWidgetItem*, bool> expandedStates;
    auto captureExpandedState = [&](auto&& self, QTreeWidgetItem* item) -> void
    {
        if (!item)
            return;
        expandedStates.insert(item, item->isExpanded());
        for (int i = 0; i < item->childCount(); ++i)
            self(self, item->child(i));
    };
    for (QTreeWidgetItem* item : std::as_const(originalItems))
        captureExpandedState(captureExpandedState, item);

    QTreeWidget* tree = parent->treeWidget();
    QTreeWidgetItem* currentItem = tree ? tree->currentItem() : nullptr;

    parent->takeChildren();
    parent->addChildren(sortedItems);

    for (auto it = expandedStates.cbegin(); it != expandedStates.cend(); ++it)
        it.key()->setExpanded(it.value());

    if (tree && currentItem && currentItem->treeWidget() == tree)
        tree->setCurrentItem(currentItem);
}

void Solver::refreshRootOverview() const
{
    if (!m_root)
        return;
    auto* page = dynamic_cast<SolverSettingWidget*>(m_root->getPage());
    if (!page)
        return;

    int seepage = 0;
    int dam = 0;
    int slope = 0;
    int cave = 0;
    int block = 0;
    for (ProjectNode* child : m_root->getChildren())
    {
        const QString kind = child ? child->data(0, ProjectNode::RoleKind).toString() : QString();
        if (kind == QStringLiteral("SeepageAnalysis")) ++seepage;
        else if (kind == QStringLiteral("DamAnalysis")) ++dam;
        else if (kind == QStringLiteral("SlopeStabilityAnalysis")) ++slope;
        else if (kind == QStringLiteral("CaveExcavationAnalysis")) ++cave;
        else if (kind == QStringLiteral("CaveBlockStabilityAnalysis")) ++block;
    }
    page->setAnalysisCounts(seepage, dam, slope, cave, block);

    QVector<QPair<QString, QString>> batchItems;
    for (ProjectNode* node : batchComputableNodes())
    {
        if (!node)
            continue;
        batchItems.push_back(qMakePair(node->getUuid(), batchNodeDisplayName(node)));
    }
    page->setBatchAnalyses(batchItems);
}

QVector<ProjectNode*> Solver::batchComputableNodes() const
{
    QVector<ProjectNode*> nodes;
    if (!m_root)
        return nodes;

    static const QSet<QString> computableKinds = {
        QStringLiteral("SeepageAnalysis"),
        QStringLiteral("DamAnalysis"),
        QStringLiteral("SlopeStabilityAnalysis"),
        QStringLiteral("CaveBlockStabilityAnalysis"),
        QStringLiteral("GeoStressRegression"),
        QStringLiteral("CaveSupportComputation")};

    std::function<void(ProjectNode*)> collect = [&](ProjectNode* node)
    {
        if (!node)
            return;
        const QString kind = node->data(0, ProjectNode::RoleKind).toString();
        if (computableKinds.contains(kind))
            nodes.push_back(node);
        for (ProjectNode* child : node->getChildren())
            collect(child);
    };

    for (ProjectNode* child : m_root->getChildren())
        collect(child);
    return nodes;
}

QString Solver::batchNodeDisplayName(ProjectNode* node) const
{
    if (!node)
        return {};

    QStringList names;
    QTreeWidgetItem* item = node;
    while (item && item != m_root)
    {
        const QString name = item->text(0).trimmed();
        if (!name.isEmpty())
            names.prepend(name);
        item = item->parent();
    }
    return names.join(QStringLiteral(" / "));
}

bool Solver::dispatchBatchCompute(ProjectNode* node)
{
    if (!node)
        return false;

    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    m_batchDispatching = true;
    if (kind == QStringLiteral("SeepageAnalysis"))
        onComputeSeepage(node);
    else if (kind == QStringLiteral("DamAnalysis"))
        onComputeStressStrain(node);
    else if (kind == QStringLiteral("SlopeStabilityAnalysis"))
        onComputeSlopeStability(node);
    else if (kind == QStringLiteral("CaveBlockStabilityAnalysis"))
        onComputeCaveBlockStability(node);
    else if (kind == QStringLiteral("GeoStressRegression"))
        onComputeGeoStressRegression(node);
    else if (kind == QStringLiteral("CaveSupportComputation"))
    {
        ProjectNode* owner = topLevelAnalysisNode(node);
        if (!owner)
        {
            m_batchDispatching = false;
            return false;
        }
        onComputeCaveExcavation(owner);
    }
    else
    {
        m_batchDispatching = false;
        return false;
    }
    m_batchDispatching = false;
    return true;
}

void Solver::onBatchComputeRequested(const QStringList& analysisUuids)
{
    if (!m_root || analysisUuids.isEmpty())
        return;

    if (m_batchActive)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算"),
                                 QStringLiteral("当前已有一组批量计算正在执行。"));
        return;
    }

    if (m_solverProcess)
    {
        if (QMessageBox::question(nullptr, QStringLiteral("已有计算正在进行"),
                                  QStringLiteral("是否停止当前计算并开始批量计算？"),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return;
        finishSolverProcess(true);
    }

    QSet<QString> requested;
    for (const QString& uuid : analysisUuids)
        requested.insert(uuid);
    QStringList ordered;
    for (ProjectNode* node : batchComputableNodes())
    {
        if (node && requested.contains(node->getUuid()))
            ordered.push_back(node->getUuid());
    }
    if (ordered.isEmpty())
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算"),
                                 QStringLiteral("所选节点已经不存在或不能直接计算。"));
        refreshRootOverview();
        return;
    }

    m_batchQueue = ordered;
    m_batchCurrentUuid.clear();
    m_batchTotal = ordered.size();
    m_batchFinished = 0;
    m_batchFailed = 0;
    m_batchActive = true;

    if (auto* page = dynamic_cast<SolverSettingWidget*>(m_root->getPage()))
    {
        page->setBatchRunning(true, m_batchTotal);
        page->setBatchProgress(0, m_batchTotal, 0);
        for (const QString& uuid : std::as_const(m_batchQueue))
            page->setBatchItemStatus(uuid, QStringLiteral("等待"));
    }

    QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
}

void Solver::onBatchStopRequested()
{
    if (!m_batchActive)
        return;
    stopSolverExecutable();
}

void Solver::startNextBatchCompute()
{
    if (!m_batchActive || !m_root || !m_batchCurrentUuid.isEmpty())
        return;

    if (m_batchQueue.isEmpty())
    {
        finishBatchCompute(false);
        return;
    }

    m_batchCurrentUuid = m_batchQueue.takeFirst();
    auto* page = dynamic_cast<SolverSettingWidget*>(m_root->getPage());
    if (page)
        page->setBatchItemStatus(m_batchCurrentUuid, QStringLiteral("准备中"));

    ProjectNode* node = nullptr;
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        node = pm->findNodeByUuid(m_batchCurrentUuid);
    if (!node || node->data(0, ProjectNode::RoleModule).toString() != QStringLiteral("Solver"))
    {
        completeBatchItem(false, QStringLiteral("节点不存在"));
        QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
        return;
    }

    if (!dispatchBatchCompute(node))
    {
        completeBatchItem(false, QStringLiteral("该节点不能直接计算"));
        QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
        return;
    }

    // 所有现有求解流程在准备成功后都会启动 QProcess。若没有进程，说明该节点在
    // 参数校验、输入文件生成或程序启动阶段已经失败；批量测试继续执行下一个节点。
    if (!m_solverProcess)
    {
        completeBatchItem(false, QStringLiteral("计算未启动"));
        QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
        return;
    }

    if (page)
        page->setBatchItemStatus(m_batchCurrentUuid, QStringLiteral("计算中"));
}

void Solver::completeBatchItem(bool succeeded, const QString& detail)
{
    if (!m_batchActive || m_batchCurrentUuid.isEmpty())
        return;

    if (auto* page = m_root ? dynamic_cast<SolverSettingWidget*>(m_root->getPage()) : nullptr)
    {
        QString status = succeeded ? QStringLiteral("完成") : QStringLiteral("失败");
        if (!succeeded && !detail.trimmed().isEmpty())
            status += QStringLiteral("（%1）").arg(detail.trimmed());
        page->setBatchItemStatus(m_batchCurrentUuid, status);
    }

    ++m_batchFinished;
    if (!succeeded)
        ++m_batchFailed;
    m_batchCurrentUuid.clear();

    if (auto* page = m_root ? dynamic_cast<SolverSettingWidget*>(m_root->getPage()) : nullptr)
        page->setBatchProgress(m_batchFinished, m_batchTotal, m_batchFailed);
}

void Solver::finishBatchCompute(bool cancelled)
{
    if (!m_batchActive)
        return;

    const int total = m_batchTotal;
    const int finished = m_batchFinished;
    const int failed = m_batchFailed;
    const int succeeded = qMax(0, finished - failed);

    m_batchActive = false;
    m_batchDispatching = false;
    m_batchQueue.clear();
    m_batchCurrentUuid.clear();

    if (auto* page = m_root ? dynamic_cast<SolverSettingWidget*>(m_root->getPage()) : nullptr)
    {
        page->setBatchRunning(false, total);
        if (cancelled)
        {
            page->setBatchStatusText(
                QStringLiteral("批量计算已停止：已执行 %1 / %2（成功 %3，失败 %4）")
                    .arg(finished)
                    .arg(total)
                    .arg(succeeded)
                    .arg(failed));
        }
        else
        {
            page->setBatchProgress(total, total, failed);
        }
    }

    if (!cancelled)
    {
        QMessageBox::information(
            nullptr, QStringLiteral("批量计算完成"),
            QStringLiteral("共执行 %1 个计算节点：成功 %2，失败 %3。")
                .arg(total)
                .arg(qMax(0, total - failed))
                .arg(failed));
    }
}

void Solver::setupNodeRole(ProjectNode* node, const QString& kind) const
{
    if (!node)
        return;
    node->setData(0, ProjectNode::RoleUuid, node->getUuid());
    node->setData(0, ProjectNode::RoleModule, "Solver");
    node->setData(0, ProjectNode::RoleKind, kind);
    node->setData(0, ProjectNode::RoleDimmed, false);
    node->setIcon(0, QIcon());
    QStringList tooltip;
    const QString relationship = solverNodeRelationshipHint(node);
    if (!relationship.isEmpty())
        tooltip.push_back(relationship);
    node->setToolTip(0, tooltip.join(QLatin1Char('\n')));
}

void Solver::connectCaveOverview(CaveExcavationWidget* page,
                                 ProjectNode* analysisNode) const
{
    if (!page || !analysisNode)
        return;

    auto openChild = [this, analysisNode](const QString& pageType)
    {
        ProjectNode* child = findChildByPageType(analysisNode, pageType);
        auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>();
        if (!child || !pm)
            return;
        analysisNode->setExpanded(true);
        child->setExpanded(true);
        pm->setCurrentItem(child);
    };
    connect(page, &CaveExcavationWidget::openGeoStressRequested, this,
            [openChild]() { openChild(QStringLiteral("GeoStressRegressionAnalysis")); });
    connect(page, &CaveExcavationWidget::openCaveSupportRequested, this,
            [openChild]() { openChild(QStringLiteral("CaveSupportComputation")); });
}

int Solver::countFillStorageCycles(ProjectNode* analysisNode) const
{
    if (!analysisNode)
        return 0;

    int count = 0;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (dynamic_cast<FillStorageCyclePage*>(child->getPage()))
            ++count;
    }
    return count;
}

void Solver::syncDamAnalysisMCase(ProjectNode* analysisNode) const
{
    if (!analysisNode)
        return;

    auto* page = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage());
    if (!page)
        return;

    int index = 0;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (!child || !dynamic_cast<FillStorageCyclePage*>(child->getPage()))
            continue;
        ++index;
    }
    syncChildNodeNames(analysisNode);
    page->setMCaseCount(index);
}

void Solver::connectDamLayerUpdates(DamAnalysisWidget* page, ProjectNode* analysisNode) const
{
    if (!page || !analysisNode)
        return;
    connect(page, &DamAnalysisWidget::constructionLayersChanged, this,
            [analysisNode](int firstLayer, int lastLayer)
            {
                QVector<FillStorageCyclePage*> cycles;
                for (ProjectNode* child : analysisNode->getChildren())
                {
                    if (auto* cycle = child
                                          ? dynamic_cast<FillStorageCyclePage*>(child->getPage())
                                          : nullptr)
                        cycles.push_back(cycle);
                }
                if (cycles.size() == 1)
                    cycles.first()->setFillLayerRange(firstLayer, lastLayer);
            });
}

int Solver::countGeoStressChildren(ProjectNode* geostressNode, const QString& pageType) const
{
    if (!geostressNode)
        return 0;
    int count = 0;
    for (ProjectNode* child : geostressNode->getChildren())
    {
        if (child && child->getPage() && child->getPage()->getPageType() == pageType)
            ++count;
    }
    return count;
}

int Solver::minimumGeoStressSideFaceCount() const
{
    const int dimension =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    return dimension == 2 ? 1 : 2;
}

void Solver::syncAnalysisNodeNames() const
{
    if (!m_root)
        return;

    QSet<QString> usedTitles;
    for (ProjectNode* node : m_root->getChildren())
    {
        if (solverNodeHasCustomTitle(node))
            usedTitles.insert(node->data(0, kSolverCustomTitleRole).toString().trimmed().toCaseFolded());
    }

    QHash<QString, int> defaultIndexes;
    for (ProjectNode* node : m_root->getChildren())
    {
        const QString kind = node ? node->data(0, ProjectNode::RoleKind).toString() : QString();
        const QString configuredTitle = solverNodeConfiguredTitle(node, kind);
        if (configuredTitle.isEmpty())
            continue;
        const bool customTitle = solverNodeHasCustomTitle(node);
        QString displayTitle = configuredTitle;
        if (!customTitle && solverNodeUsesAutomaticIndex(kind))
        {
            do
            {
                const int defaultIndex = ++defaultIndexes[kind];
                displayTitle = solverNodeDisplayTitle(configuredTitle, kind, defaultIndex, false);
            } while (usedTitles.contains(displayTitle.toCaseFolded()));
        }
        setSolverNodeTitle(node, displayTitle);
        updateAnalysisManifestName(node);
        usedTitles.insert(displayTitle.toCaseFolded());
    }
    refreshRootOverview();
}

void Solver::syncChildNodeNames(ProjectNode* parent) const
{
    if (!parent)
        return;

    normalizeChildNodeOrder(parent);

    QSet<QString> usedTitles;
    for (ProjectNode* node : parent->getChildren())
    {
        if (solverNodeHasCustomTitle(node))
            usedTitles.insert(node->data(0, kSolverCustomTitleRole).toString().trimmed().toCaseFolded());
    }

    QHash<QString, int> indexes;
    QHash<QString, int> defaultIndexes;
    for (ProjectNode* node : parent->getChildren())
    {
        const QString kind = node ? node->data(0, ProjectNode::RoleKind).toString() : QString();
        const QString configuredTitle = solverNodeConfiguredTitle(node, kind);
        if (configuredTitle.isEmpty())
            continue;
        const int index = ++indexes[kind];
        const bool customTitle = solverNodeHasCustomTitle(node);
        QString displayTitle = configuredTitle;
        if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(node->getPage()))
        {
            // 层号决定实际施工顺序，直接显示在树上，避免节点序号被误认为开挖级次。
            displayTitle = QStringLiteral("%1（层号 %2）")
                               .arg(configuredTitle)
                               .arg(bodyPage->excavationLayer());
        }
        else if (!customTitle && solverNodeUsesAutomaticIndex(kind))
        {
            do
            {
                const int defaultIndex = ++defaultIndexes[kind];
                displayTitle = solverNodeDisplayTitle(configuredTitle, kind, defaultIndex, false);
            } while (usedTitles.contains(displayTitle.toCaseFolded()));
        }
        setSolverNodeTitle(node, displayTitle);
        usedTitles.insert(displayTitle.toCaseFolded());
        if (auto* sidePage = dynamic_cast<GeoStressSideFacePage*>(node->getPage()))
            sidePage->setFaceIndex(index);
    }

    refreshRootOverview();
}

void Solver::addFillStorageCycleNode(ProjectNode* analysisNode, bool setCurrent) const
{
    if (!analysisNode)
        return;

    if (ProjectNode* existing = findChildByPageType(analysisNode, QStringLiteral("FillStorageCycle")))
    {
        if (setCurrent)
        {
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(existing);
        }
        return;
    }

    auto* fillStorageCyclePage = new FillStorageCyclePage();
    if (auto* damPage = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage()))
    {
        const QPair<int, int> range = damPage->constructionLayerRange();
        if (range.first == 2)
            fillStorageCyclePage->setFillLayerRange(range.first, range.second);
    }
    auto* fillStorageCycleNode = new ProjectNode(
        QStringLiteral("填筑蓄水"), QUuid::createUuid().toString(), fillStorageCyclePage);
    setupNodeRole(fillStorageCycleNode, "FillStorageCycle");
    analysisNode->addChild(fillStorageCycleNode);
    analysisNode->setExpanded(true);
    connect(fillStorageCyclePage, &FillStorageCyclePage::requirementsChanged, this,
            [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
    normalizeChildNodeOrder(analysisNode);
    syncDamAnalysisMCase(analysisNode);
    syncDamConditionalNodes(analysisNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(fillStorageCycleNode);
    }
}

void Solver::addDamDynamicAnalysisNode(ProjectNode* analysisNode, bool setCurrent,
                                       const QJsonObject& initialState) const
{
    if (!analysisNode)
        return;

    if (ProjectNode* existing = findChildByPageType(analysisNode, QStringLiteral("DamDynamicAnalysis")))
    {
        if (setCurrent)
        {
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(existing);
        }
        return;
    }

    auto* dynamicPage = new DamDynamicAnalysisPage();
    if (!initialState.isEmpty())
        dynamicPage->loadLegacyFillStorageState(initialState);
    if (auto* damPage = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage()))
        dynamicPage->setModalExtractionOrder(damPage->modalExtractionOrder());

    auto* node = new ProjectNode(QStringLiteral("动力分析"),
                                 QUuid::createUuid().toString(), dynamicPage);
    setupNodeRole(node, QStringLiteral("DamDynamicAnalysis"));
    analysisNode->addChild(node);
    analysisNode->setExpanded(true);

    connect(dynamicPage, &DamDynamicAnalysisPage::requirementsChanged, this,
            [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
    connect(dynamicPage, &DamDynamicAnalysisPage::modalExtractionOrderChanged, this,
            [this, analysisNode](int order)
            {
                if (auto* damPage = dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage()))
                    damPage->setModalExtractionOrder(order);
            });

    normalizeChildNodeOrder(analysisNode);
    syncChildNodeNames(analysisNode);
    syncDamConditionalNodes(analysisNode);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addSeepageNaturalBoundaryNode(ProjectNode* analysisNode, bool setCurrent) const
{
    if (!analysisNode)
        return;

    auto* page = new SeepageBoundaryPage(
        QStringLiteral("SeepageBoundaryNaturalSeepage"),
        QStringLiteral("天然渗流初始面"), QStringLiteral("JDFREE.DAT"));
    auto* node = new ProjectNode(QStringLiteral("天然渗流初始面"),
                                 QUuid::createUuid().toString(), page);
    setupNodeRole(node, QStringLiteral("SeepageNaturalBoundary"));
    analysisNode->addChild(node);
    analysisNode->setExpanded(true);
    connectBoundaryStatus(node);
    syncChildNodeNames(analysisNode);
    syncSeepageConditionalNodes(analysisNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressRegressionNode(ProjectNode* analysisNode, bool setCurrent)
{
    if (!analysisNode)
        return;

    if (ProjectNode* existing =
            findChildByPageType(analysisNode, QStringLiteral("GeoStressRegressionAnalysis")))
    {
        if (setCurrent)
        {
            QMessageBox::information(
                nullptr, QStringLiteral("数量限制"),
                QStringLiteral("每个洞室开挖分析只能设置一个地应力回归分析。"));
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(existing);
        }
        return;
    }

    auto* page = new GeoStressRegressionWidget();
    auto* node = new ProjectNode("地应力回归分析", QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegression");
    analysisNode->addChild(node);
    analysisNode->setExpanded(true);

    connect(page, &GeoStressRegressionWidget::computeRequested, this,
            [this, node]()
            {
                onComputeGeoStressRegression(node);
            });
            connectStopRequest(page);

    for (int i = 0; i < minimumGeoStressSideFaceCount(); ++i)
        addGeoStressSideFaceNode(node, false);
    addGeoStressMeasurePointNode(node, false);
    addGeoStressOuterForceNode(node, false);
    node->setExpanded(true);
    syncChildNodeNames(analysisNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressSideFaceNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    const int idx = countGeoStressChildren(geostressNode, "GeoStressRegressionSideFace") + 1;
    if (idx > 4)
    {
        if (setCurrent)
        {
            QMessageBox::information(nullptr, QStringLiteral("数量限制"),
                                     QStringLiteral("侧立面最多允许设置 4 个。"));
        }
        return;
    }
    auto* page = new GeoStressSideFacePage();
    page->setFaceIndex(idx);
    auto* node = new ProjectNode(QStringLiteral("侧立面"), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionSideFace");
    geostressNode->addChild(node);
    geostressNode->setExpanded(true);
    syncChildNodeNames(geostressNode);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressMeasurePointNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    if (ProjectNode* existing = findChildByPageType(
            geostressNode, QStringLiteral("GeoStressRegressionMeasurePoint")))
    {
        if (setCurrent)
        {
            QMessageBox::information(
                nullptr, QStringLiteral("数量限制"),
                QStringLiteral("每个地应力回归分析只能设置一个实测地应力数据项。"));
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(existing);
        }
        return;
    }
    auto* page = new GeoStressMeasurePointPage();
    auto* node = new ProjectNode(QStringLiteral("实测地应力点"), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionMeasurePoint");
    geostressNode->addChild(node);
    geostressNode->setExpanded(true);
    syncChildNodeNames(geostressNode);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addGeoStressOuterForceNode(ProjectNode* geostressNode, bool setCurrent) const
{
    if (!geostressNode)
        return;
    if (ProjectNode* existing = findChildByPageType(
            geostressNode, QStringLiteral("GeoStressRegressionOuterForce")))
    {
        if (setCurrent)
        {
            QMessageBox::information(
                nullptr, QStringLiteral("数量限制"),
                QStringLiteral("每个地应力回归分析只能设置一个外面力数据项。"));
            if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                pm->setCurrentItem(existing);
        }
        return;
    }
    auto* page = new GeoStressOuterForcePage();
    auto* node = new ProjectNode(QStringLiteral("外面力"), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "GeoStressRegressionOuterForce");
    geostressNode->addChild(node);
    geostressNode->setExpanded(true);
    syncChildNodeNames(geostressNode);
    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

GeoStressRegressionWidget::ExportPayload Solver::buildGeoStressPayload(ProjectNode* geostressNode) const
{
    GeoStressRegressionWidget::ExportPayload payload;
    if (!geostressNode)
        return payload;

    int measureIndex = 0;
    for (ProjectNode* child : geostressNode->getChildren())
    {
        if (!child || !child->getPage())
            continue;
        if (auto* side = dynamic_cast<GeoStressSideFacePage*>(child->getPage()))
        {
            if (!side->exportDatFile())
                continue;
            payload.sideFaceFiles.push_back(side->faceFile());
            const auto attr = side->attr();
            payload.faceRows.push_back({attr.idir, attr.height, attr.gama, attr.pgrout, attr.ii});
        }
        else if (auto* p = dynamic_cast<GeoStressMeasurePointPage*>(child->getPage()))
        {
            for (const auto& point : p->points())
            {
                ++measureIndex;
                payload.measureRows.push_back({point.x, point.y, point.z, point.sgx, point.sgy, point.sgz,
                                               point.txy, point.tyz, point.tzx, QString::number(measureIndex)});
            }
        }
        else if (auto* f = dynamic_cast<GeoStressOuterForcePage*>(child->getPage()))
        {
            // FAROOF.DAT 描述地表面，与是否施加外力无关，必须始终导出。
            payload.roofFileReady = f->exportRoofFile();
            if (!payload.roofFileReady)
                continue;

            // 不再无条件生成 FTLOAD.DAT。零外力或未选择作用边/面时，
            // IGEOS 的 ITAG_FT 必须保持 0，避免求解器读取无效或旧的 FTLOAD.DAT。
            const int enabled = f->forceEnabled();
            if (enabled == 0)
                continue;

            // 只有文件成功生成后才允许启用 FTLOAD。
            if (!f->exportForceFile())
                continue;

            if (payload.forceFile.isEmpty())
                payload.forceFile = f->forceFile();
            payload.forceEnabled = 1;
        }
    }
    return payload;
}


void Solver::addCaveSupportComputationNode(ProjectNode* analysisNode, bool setCurrent)
{
    if (!analysisNode)
        return;

    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (child && child->getPage()
            && child->getPage()->getPageType() == "CaveSupportComputation")
        {
            if (setCurrent)
            {
                QMessageBox::information(
                    nullptr, QStringLiteral("数量限制"),
                    QStringLiteral("每个洞室开挖分析只能设置一个开挖与加固分析。"));
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(child);
            }
            return;
        }
    }

    auto* page = new CaveSupportComputationPage();
    auto* node = new ProjectNode("开挖与加固分析", QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveSupportComputation");
    analysisNode->addChild(node);
    analysisNode->setExpanded(true);
    syncChildNodeNames(analysisNode);
    connect(page, &CaveSupportComputationPage::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeCaveExcavation(analysisNode);
            });
    connect(page, &CaveSupportComputationPage::liningEnabledChanged, this,
            [this, node](bool)
            {
                syncCaveSupportConditionalNodes(node);
            });
    connect(page, &CaveSupportComputationPage::craneBeamEnabledChanged, this,
            [this, node](bool)
            {
                syncCaveSupportConditionalNodes(node);
            });
    connectStopRequest(page);
    addCaveLiningBoundaryNode(node, true, false);
    addCaveLiningBoundaryNode(node, false, false);
    addCaveCraneBeamAreaNode(node, false);
    addCaveExcavationBodyNode(node, false);
    syncCaveSupportConditionalNodes(node);
    node->setExpanded(true);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveLiningBoundaryNode(ProjectNode* supportNode, bool innerBoundary,
                                               bool setCurrent) const
{
    if (!supportNode)
        return;

    const QString pageType = innerBoundary
        ? QStringLiteral("CaveLiningInnerBoundary")
        : QStringLiteral("CaveLiningOuterBoundary");
    for (ProjectNode* child : supportNode->getChildren())
    {
        if (child && child->getPage() && child->getPage()->getPageType() == pageType)
        {
            if (setCurrent)
            {
                if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
                    pm->setCurrentItem(child);
            }
            return;
        }
    }

    const QString title = innerBoundary ? QStringLiteral("衬砌内边界")
                                        : QStringLiteral("衬砌外边界");
    const QString fileName = innerBoundary ? QStringLiteral("IIJX_CHEN.DAT")
                                           : QStringLiteral("IIJX_WAI.DAT");
    auto* page = new StressBoundaryPage(pageType, title, fileName);
    auto* node = new ProjectNode(title, QUuid::createUuid().toString(), page);
    setupNodeRole(node, innerBoundary ? QStringLiteral("CaveLiningInnerBoundary")
                                      : QStringLiteral("CaveLiningOuterBoundary"));
    supportNode->addChild(node);
    connectBoundaryStatus(node);
    supportNode->setExpanded(true);
    syncChildNodeNames(supportNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveCraneBeamAreaNode(ProjectNode* supportNode, bool setCurrent) const
{
    if (!supportNode)
        return;

    for (ProjectNode* child : supportNode->getChildren())
    {
        if (child && child->getPage()
            && child->getPage()->getPageType() == QStringLiteral("CaveCraneBeamArea"))
        {
            if (setCurrent)
            {
                if (auto* projectManager =
                        ServiceRegistry::instance().getService<ProjectManagerService>())
                    projectManager->setCurrentItem(child);
            }
            return;
        }
    }

    auto* page = new CaveCraneBeamAreaPage();
    auto* node = new ProjectNode(QStringLiteral("吊车梁区域"),
                                 QUuid::createUuid().toString(), page);
    setupNodeRole(node, QStringLiteral("CaveCraneBeamArea"));
    supportNode->addChild(node);
    connect(page, &CaveCraneBeamAreaPage::selectionChanged, this,
            [this, supportNode](int) { syncCaveSupportConditionalNodes(supportNode); });
    supportNode->setExpanded(true);
    syncChildNodeNames(supportNode);

    if (setCurrent)
    {
        if (auto* projectManager =
                ServiceRegistry::instance().getService<ProjectManagerService>())
            projectManager->setCurrentItem(node);
    }
}

void Solver::addCaveExcavationBodyNode(ProjectNode* supportNode, bool setCurrent) const
{
    if (!supportNode)
        return;

    QSet<int> usedLayers;
    for (ProjectNode* child : supportNode->getChildren())
    {
        if (auto* bodyPage = dynamic_cast<CaveExcavationBodyPage*>(child->getPage()))
            usedLayers.insert(bodyPage->excavationLayer());
    }

    int newLayer = -1;
    while (newLayer >= -999 && usedLayers.contains(newLayer))
        --newLayer;
    if (newLayer < -999)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法新增开挖体"),
            QStringLiteral("开挖阶段数量已达到上限。"));
        return;
    }

    auto* page = new CaveExcavationBodyPage();
    page->setExcavationLayer(newLayer);
    auto* node =
        new ProjectNode(QStringLiteral("开挖体"), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveExcavationBody");
    supportNode->addChild(node);
    connect(page, &CaveExcavationBodyPage::excavationLayerChanged, this,
            [this, supportNode](int) { syncChildNodeNames(supportNode); });
    supportNode->setExpanded(true);
    syncChildNodeNames(supportNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::addCaveBlockPlaneNode(ProjectNode* blockNode, bool setCurrent) const
{
    if (!blockNode)
        return;

    auto* page = new CaveBlockPlanePage();
    auto* node = new ProjectNode(QStringLiteral("滑块面"), QUuid::createUuid().toString(), page);
    setupNodeRole(node, "CaveBlockPlane");
    blockNode->addChild(node);
    blockNode->setExpanded(true);
    syncChildNodeNames(blockNode);

    if (setCurrent)
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setCurrentItem(node);
    }
}

void Solver::onSeepageAction()
{
    if (!m_root)
        return;
    auto* seepagePage = new SeepageWidget();
    const QString analysisName = QStringLiteral("渗流分析");
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), seepagePage);
    setupNodeRole(analysisNode, "SeepageAnalysis");

    connect(seepagePage, &SeepageWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeSeepage(analysisNode);
             });
    connect(seepagePage, &SeepageWidget::requirementsChanged, this,
            [this, analysisNode]() { syncSeepageConditionalNodes(analysisNode); });
    connectStopRequest(seepagePage);

    auto addBoundary = [&](const QString& name, const QString& pageType,
                           const QString& fileName) -> SeepageBoundaryPage*
    {
        auto* page = new SeepageBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        setupNodeRole(node,
                      pageType == QStringLiteral("SeepageBoundaryNaturalSeepage")
                          ? QStringLiteral("SeepageNaturalBoundary")
                          : QStringLiteral("SeepageBoundary"));
        analysisNode->addChild(node);
        connectBoundaryStatus(node);
        return page;
    };

    SeepageBoundaryPage* upstreamPage =
        addBoundary("上游渗流边界", "SeepageBoundaryUpstreamWater", "JD_UPSEEP.DAT");
    SeepageBoundaryPage* downstreamPage =
        addBoundary("下游渗流边界", "SeepageBoundaryDownstreamWater", "JD_DWSEEP.DAT");
    addBoundary("天然渗流初始面", "SeepageBoundaryNaturalSeepage", "JDFREE.DAT");
    {
        auto* page = new SeepageBoundaryPage("SeepageBoundaryDrainWell", "排水井区域",
                                             "JDJING.DAT", nullptr, true);
        auto* node = new ProjectNode("排水井区域", QUuid::createUuid().toString(), page);
        setupNodeRole(node, "SeepageBoundary");
        analysisNode->addChild(node);
        connectBoundaryStatus(node);
    }

    seepagePage->setBoundaryPages(upstreamPage, downstreamPage);

    m_root->addChild(analysisNode);
    analysisNode->setExpanded(true);
    syncChildNodeNames(analysisNode);
    syncAnalysisNodeNames();
    syncSeepageConditionalNodes(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onStressStrainAction()
{
    if (!m_root)
        return;
    auto* stressPage = new DamAnalysisWidget();
    const QString analysisName = QStringLiteral("土石坝分析");
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), stressPage);
    setupNodeRole(analysisNode, "DamAnalysis");

    connect(stressPage, &DamAnalysisWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeStressStrain(analysisNode);
             });
    connect(stressPage, &DamAnalysisWidget::requirementsChanged, this,
            [this, analysisNode]() { syncDamConditionalNodes(analysisNode); });
    connectDamLayerUpdates(stressPage, analysisNode);
    connectStopRequest(stressPage);

    auto addBoundary = [&](const QString& name, const QString& pageType, const QString& fileName)
    {
        auto* page = new StressBoundaryPage(pageType, name, fileName);
        auto* node = new ProjectNode(name, QUuid::createUuid().toString(), page);
        setupNodeRole(node, "StressBoundary");
        analysisNode->addChild(node);
        connectBoundaryStatus(node);
    };

    addBoundary("上游水压力作用面", "StressBoundaryDamUpstream", "FA_DAMUP.DAT");

    auto* iceRegionPage =
        new SeepageBoundaryPage("DamIceRegion", "结冰区域", "JMFW.DAT", nullptr, true);
    auto* iceRegionNode =
        new ProjectNode("结冰区域", QUuid::createUuid().toString(), iceRegionPage);
    setupNodeRole(iceRegionNode, "DamIceRegion");
    analysisNode->addChild(iceRegionNode);
    connectBoundaryStatus(iceRegionNode);

    addFillStorageCycleNode(analysisNode, false);
    addDamDynamicAnalysisNode(analysisNode, false);
    syncDamAnalysisMCase(analysisNode);

    m_root->addChild(analysisNode);
    analysisNode->setExpanded(true);
    syncAnalysisNodeNames();
    syncDamConditionalNodes(analysisNode);
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onCaveExcavationAction()
{
    if (!m_root)
        return;
    auto* cavePage = new CaveExcavationWidget();
    const QString analysisName = QStringLiteral("洞室开挖工程");
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), cavePage);
    setupNodeRole(analysisNode, "CaveExcavationAnalysis");
    connectCaveOverview(cavePage, analysisNode);

    addGeoStressRegressionNode(analysisNode, false);
    addCaveSupportComputationNode(analysisNode, false);

    m_root->addChild(analysisNode);
    analysisNode->setExpanded(true);
    syncAnalysisNodeNames();
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onCaveBlockStabilityAction()
{
    if (!m_root)
        return;
    auto* blockPage = new CaveBlockStabilityWidget();
    const QString analysisName = QStringLiteral("块体稳定分析");
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), blockPage);
    setupNodeRole(analysisNode, "CaveBlockStabilityAnalysis");

    connect(blockPage, &CaveBlockStabilityWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeCaveBlockStability(analysisNode);
            });
            connectStopRequest(blockPage);

    addCaveBlockPlaneNode(analysisNode, false);

    m_root->addChild(analysisNode);
    analysisNode->setExpanded(true);
    syncAnalysisNodeNames();
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onSlopeStabilityAction()
{
    if (!m_root)
        return;
    auto* slopePage = new SlopeStabilityWidget();
    const QString analysisName = QStringLiteral("边坡分析");
    auto* analysisNode = new ProjectNode(analysisName, QUuid::createUuid().toString(), slopePage);
    setupNodeRole(analysisNode, "SlopeStabilityAnalysis");

    connect(slopePage, &SlopeStabilityWidget::computeRequested, this,
            [this, analysisNode]()
            {
                onComputeSlopeStability(analysisNode);
            });
            connectStopRequest(slopePage);

    m_root->addChild(analysisNode);
    analysisNode->setExpanded(true);
    syncAnalysisNodeNames();
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
}

void Solver::onSpecificationCheckAction()
{
    if (!m_root)
        return;

    // 规范验算是边坡分析的一种计算方式。优先复用最新的边坡分析，使其 DAT
    // 输入和最近一次结果继续随该分析节点保存；工程中尚无边坡分析时自动创建。
    ProjectNode* analysisNode = nullptr;
    const auto children = m_root->getChildren();
    for (auto it = children.crbegin(); it != children.crend(); ++it)
    {
        ProjectNode* candidate = *it;
        if (candidate && candidate->data(0, ProjectNode::RoleKind).toString()
                             == QStringLiteral("SlopeStabilityAnalysis"))
        {
            analysisNode = candidate;
            break;
        }
    }

    if (!analysisNode)
    {
        onSlopeStabilityAction();
        const auto updatedChildren = m_root->getChildren();
        if (!updatedChildren.isEmpty())
            analysisNode = updatedChildren.last();
    }

    auto* page = analysisNode
                     ? dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage())
                     : nullptr;
    if (!page)
        return;
    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setCurrentItem(analysisNode);
    QTimer::singleShot(0, page, [page]() { page->openStandardMethodDialog(); });
}

void Solver::onComputeSeepage(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    auto* seepagePage = dynamic_cast<SeepageWidget*>(analysisNode ? analysisNode->getPage() : nullptr);
    QString parameterError;
    if (!seepagePage || !seepagePage->validateInputs(&parameterError))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请完善渗流设置"),
            parameterError.isEmpty()
                ? QStringLiteral("当前渗流设置未正确加载。请重新打开工程后再试。")
                : parameterError);
        return;
    }
    syncSeepageConditionalNodes(analysisNode);

    QStringList missingBoundaries;
    auto requireBoundary = [&](const QString& type, const QString& title, bool required)
    {
        if (!required)
            return;
        ProjectNode* node = findChildByPageType(analysisNode, type);
        auto* page = node ? dynamic_cast<SeepageBoundaryPage*>(node->getPage()) : nullptr;
        if (!page || page->selectionCount() <= 0)
            missingBoundaries << title;
    };
    requireBoundary(QStringLiteral("SeepageBoundaryUpstreamWater"),
                    QStringLiteral("上游渗流边界"), true);
    requireBoundary(QStringLiteral("SeepageBoundaryDownstreamWater"),
                    QStringLiteral("下游渗流边界"), true);
    requireBoundary(QStringLiteral("SeepageBoundaryDrainWell"),
                    QStringLiteral("排水井区域"), seepagePage->drainWellEnabled());
    if (!missingBoundaries.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请完善边界设置"),
            QStringLiteral("以下边界尚未选择：\n%1")
                .arg(missingBoundaries.join(QLatin1Char('\n'))));
        return;
    }

    QMap<int, SeepageBoundaryPage::NaturalFreeEntry> mergedNaturalEntries;
    if (seepagePage->naturalFreeSurfaceEnabled())
    {
        QStringList naturalErrors;
        int naturalBoundaryCount = 0;
        for (ProjectNode* child : analysisNode->getChildren())
        {
            auto* naturalPage = child
                ? dynamic_cast<SeepageBoundaryPage*>(child->getPage()) : nullptr;
            if (!naturalPage || naturalPage->getPageType()
                                    != QStringLiteral("SeepageBoundaryNaturalSeepage"))
                continue;

            ++naturalBoundaryCount;
            QString naturalError;
            if (!naturalPage->validateSpecialInputs(&naturalError))
            {
                naturalErrors << QStringLiteral("%1：%2")
                                     .arg(child->text(0), naturalError);
                continue;
            }

            const auto entries = naturalPage->naturalFreeEntries(&naturalError);
            if (!naturalError.isEmpty())
            {
                naturalErrors << QStringLiteral("%1：%2")
                                     .arg(child->text(0), naturalError);
                continue;
            }

            for (const auto& entry : entries)
            {
                auto it = mergedNaturalEntries.find(entry.nodeId);
                if (it == mergedNaturalEntries.end())
                {
                    mergedNaturalEntries.insert(entry.nodeId, entry);
                    continue;
                }

                const bool sameHead = std::abs(it.value().head - entry.head) <= 1.0e-8;
                const bool sameCorrection = it.value().correction == entry.correction;
                if (!sameHead || !sameCorrection)
                {
                    naturalErrors << QStringLiteral(
                        "节点 %1 同时属于多个天然渗流初始面，但水头处理方式不同。")
                                         .arg(entry.nodeId);
                }
            }
        }

        if (naturalBoundaryCount <= 0 || mergedNaturalEntries.isEmpty())
            naturalErrors << QStringLiteral("至少需要设置一个天然渗流初始面。");

        naturalErrors.removeDuplicates();
        if (!naturalErrors.isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善天然渗流初始面"),
                naturalErrors.join(QLatin1Char('\n')));
            return;
        }
    }

    // 纯非稳态计算必须复用当前分析工况自己的初始水头场。
    // 此处不能使用 SolverFilePaths::outputRoot(projectDir)：beginCommonCompute() 尚未
    // 设置本次运行目录，提前调用会落到工程根目录，可能误判或读取其他工况的旧结果。
    if (seepagePage->computeType() == 2)
    {
        const QString workspaceDir = analysisWorkspace(analysisNode);
        if (workspaceDir.isEmpty()
            || seepagePage->transientInitialStateDirectory(workspaceDir).isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("缺少初始状态"),
                QStringLiteral(
                    "当前工况还没有可用于非稳态计算的初始水头场。\n"
                    "请先选择“稳定渗流”完成一次计算，再切换为“非稳定渗流”；"
                    "也可以直接选择“稳定 + 非稳定”。"));
            return;
        }
    }

    if (!beginCommonCompute(analysisNode, "渗流分析"))
        return;

    const QString runDir = SolverFilePaths::outputRoot(projectDir);
    if (!forceIiinLayersToOne(runDir))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("当前网格不满足渗流计算要求。请检查网格设置后重试。"));
        clearSolverRunDirectory();
        return;
    }

    QFile iautoFile(QDir(SolverFilePaths::outputRoot(projectDir))
                        .absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("渗流计算数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "4\n";
    }

    bool ok = true;
    QStringList failedFiles;

    if (!seepagePage || !seepagePage->exportIseepDatFile())
    {
        ok = false;
        failedFiles << QStringLiteral("渗流计算参数");
    }

    // 多个天然渗流初始面在前端独立设置，求解器仍只读取一个 JDFREE.DAT。
    // 这里统一合并，避免各页面依次导出时互相覆盖。
    const QString naturalPath = QDir(SolverFilePaths::outputInputDirectory(projectDir))
                                    .filePath(QStringLiteral("JDFREE.DAT"));
    if (seepagePage->naturalFreeSurfaceEnabled())
    {
        QFile naturalFile(naturalPath);
        if (!naturalFile.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            ok = false;
            failedFiles << QStringLiteral("天然渗流初始面");
        }
        else
        {
            QTextStream os(&naturalFile);
            os.setRealNumberNotation(QTextStream::FixedNotation);
            os.setRealNumberPrecision(8);
            os << mergedNaturalEntries.size() << "\n";
            for (auto it = mergedNaturalEntries.constBegin();
                 it != mergedNaturalEntries.constEnd(); ++it)
            {
                const auto& entry = it.value();
                os << entry.nodeId << " " << entry.head << " "
                   << entry.correction << "\n";
            }
        }
    }
    else if (QFile::exists(naturalPath) && !QFile::remove(naturalPath))
    {
        ok = false;
        failedFiles << QStringLiteral("天然渗流初始面数据");
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundaryPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
        if (!boundaryPage)
            continue;

        const QString type = boundaryPage->getPageType();
        if (type == QStringLiteral("SeepageBoundaryNaturalSeepage"))
            continue; // 已在上方统一合并导出
        if (type == QStringLiteral("SeepageBoundaryDrainWell") && !seepagePage->drainWellEnabled())
            continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << child->getName();
    }

    if (ok)
    {
        launchSolverExecutable(analysisNode, "渗流分析");
    }
    else
    {
        clearSolverRunDirectory();
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("以下设置未能用于计算：\n%1\n请检查后重试。")
                .arg(failedFiles.join(QLatin1Char('\n'))));
    }
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "渗流分析计算失败:" << e.what();
    }
}

void Solver::onComputeStressStrain(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    auto* stressPage = dynamic_cast<DamAnalysisWidget*>(analysisNode ? analysisNode->getPage() : nullptr);
    if (!stressPage)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("当前土石坝设置未正确加载。请重新打开工程后再试。"));
        return;
    }
    syncDamConditionalNodes(analysisNode);
    auto* dynamicNode = findChildByPageType(analysisNode, QStringLiteral("DamDynamicAnalysis"));
    auto* dynamicPage = dynamicNode
        ? dynamic_cast<DamDynamicAnalysisPage*>(dynamicNode->getPage()) : nullptr;
    if (stressPage->dynamicAnalysisEnabled())
    {
        if (!dynamicPage)
        {
            QMessageBox::warning(nullptr, QStringLiteral("请完善动力分析"),
                                 QStringLiteral("当前工况缺少“动力分析”子节点，请重新打开工程。"));
            return;
        }
        QString dynamicError;
        if (!dynamicPage->validateInputs(&dynamicError))
        {
            QMessageBox::warning(nullptr, QStringLiteral("请完善动力分析"), dynamicError);
            return;
        }
    }
    QString parameterError;
    if (!stressPage->validateInputs(&parameterError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请完善土石坝设置"), parameterError);
        return;
    }

    bool needsFaceBoundary = false;
    int cycleIndex = 0;
    QVector<FillStorageCyclePage*> cyclePages;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* cycle = child ? dynamic_cast<FillStorageCyclePage*>(child->getPage()) : nullptr;
        if (!cycle)
            continue;
        cyclePages.push_back(cycle);
        ++cycleIndex;
        QString error;
        if (!cycle->validateInputs(&error))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善填筑蓄水"),
                error);
            return;
        }
        needsFaceBoundary = needsFaceBoundary || (!stressPage->dynamicOnlyEnabled() && cycle->usesDamFacePressure());
    }
    if (cycleIndex == 0)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("缺少填筑蓄水设置"),
            QStringLiteral("当前土石坝工况缺少“填筑蓄水”子节点。"));
        return;
    }
    if (cyclePages.size() > 99)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("工况数量超限"),
            QStringLiteral("单个土石坝分析最多支持 99 个工况。"));
        return;
    }

    // 求解器的无浸润骤降分支把初始状态固定指向 CASE##FILL##，
    // 且 FILL 后缀错误地使用 ICASE。只有“循环数 = 工况号”且对应 FILL 实际存在时才安全。
    for (int i = 0; i < cyclePages.size(); ++i)
    {
        FillStorageCyclePage* cycle = cyclePages.at(i);
        const int caseIndex = i + 1;
        if (cycle && cycle->usesWaterDrop() && !cycle->usesWetting()
            && (cycle->cycleCount() != caseIndex
                || !cycle->hasEffectiveWaterFill(caseIndex)
                || cycle->lastCycleHasReservoirCreep()))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("水位骤降计算限制"),
                QStringLiteral("第 %1 个工况的水位骤降与当前施工阶段冲突。"
                               "请检查蓄水、浸润和流变设置。")
                    .arg(caseIndex));
            return;
        }
    }

    const bool hasModal = dynamicPage && dynamicPage->usesModalAnalysis();
    const bool hasWave = dynamicPage && dynamicPage->usesWaveAnalysis();
    if (stressPage->dynamicOnlyEnabled() && !hasModal && !hasWave)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请设置动力分析"),
            QStringLiteral("当前选择了“仅动力计算”，但“动力分析”中没有启用模态分析或地震波时程分析。"));
        return;
    }
    stressPage->setModalAnalysisRequired(hasModal);

    // 地震加速度表由“约束与荷载”统一维护；动力工况只通过 IFWAVE 决定是否使用。
    // 在生成求解文件前直接校验页面数据，避免没有/重复/空表时直到 Fortran 阶段才报错。
    if (hasWave)
    {
        auto* physicalFieldService =
            ServiceRegistry::instance().getService<PhysicalFieldService>();
        QString waveError;
        if (!physicalFieldService
            || !physicalFieldService->validateSeismicAcceleration(&waveError))
        {
            if (waveError.isEmpty())
                waveError = QStringLiteral("无法读取“约束与荷载”中的地震加速度设置。");
            QMessageBox::warning(
                nullptr, QStringLiteral("请设置地震加速度"),
                QStringLiteral("%1\n\n请在“约束与荷载 → 地震加速度”中检查后重试。")
                    .arg(waveError));
            return;
        }
    }

    // 结冰分支同样把初始状态固定指向 CASE(ICASE)FILL(ICASE)，并且不会继承
    // 后续 WETT / DROP / 最后一循环 FLIU。只允许求解器能够明确闭环的组合。
    if (stressPage->freezeThawEnabled())
    {
        for (int i = 0; i < cyclePages.size(); ++i)
        {
            const FillStorageCyclePage* cycle = cyclePages.at(i);
            const int caseIndex = i + 1;
            const bool supported =
                cycle && cycle->cycleCount() == caseIndex
                && cycle->hasEffectiveWaterFill(caseIndex)
                && !cycle->usesWetting()
                && !cycle->usesWaterDrop()
                && !cycle->lastCycleHasReservoirCreep();
            if (!supported)
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("结冰区域计算限制"),
                    QStringLiteral("第 %1 个工况的结冰变形与当前施工阶段冲突。"
                                   "请确保结冰前已完成蓄水，并关闭浸润变形、水位骤降和蓄水期流变。")
                        .arg(caseIndex));
                return;
            }
        }
    }

    const bool hasCreep = std::any_of(
        cyclePages.cbegin(), cyclePages.cend(),
        [](const FillStorageCyclePage* cycle)
        {
            return cycle && cycle->hasCreepCalculation();
        });
    if (hasCreep)
    {
        bool creepDtOk = false;
        const double creepDt = stressPage->creepTimeStep(&creepDtOk);
        if (!creepDtOk || !std::isfinite(creepDt) || creepDt <= 0.0)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善流变设置"),
                QStringLiteral("已启用填筑期或蓄水期流变，蠕变计算时间步长必须大于 0。"));
            return;
        }
    }

    QStringList missingNodes;
    auto requireStressBoundary = [&](const QString& type, const QString& title, bool required)
    {
        if (!required)
            return;
        ProjectNode* node = findChildByPageType(analysisNode, type);
        auto* page = node ? dynamic_cast<StressBoundaryPage*>(node->getPage()) : nullptr;
        if (!page || page->selectionCount() <= 0)
            missingNodes << title;
    };
    requireStressBoundary(QStringLiteral("StressBoundaryDamUpstream"),
                          QStringLiteral("上游水压力作用面"), needsFaceBoundary);
    if (stressPage->freezeThawEnabled())
    {
        ProjectNode* node = findChildByPageType(analysisNode, QStringLiteral("DamIceRegion"));
        auto* page = node ? dynamic_cast<SeepageBoundaryPage*>(node->getPage()) : nullptr;
        if (!page || page->selectionCount() <= 0)
            missingNodes << QStringLiteral("结冰区域");
    }
    if (!missingNodes.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请完善区域设置"),
            QStringLiteral("以下区域尚未选择：\n%1")
                .arg(missingNodes.join(QLatin1Char('\n'))));
        return;
    }
    // 静力->动力材料关系属于当前土石坝工况的“动力分析”子节点。
    QMap<int, int> dynamicSourceByMaterial; // key=动力材料，value=静力材料
    if (dynamicPage)
    {
        for (const auto& mapping : dynamicPage->activeDynamicMaterialMappings())
            dynamicSourceByMaterial.insert(mapping.second, mapping.first);
    }

    if (!beginCommonCompute(analysisNode, "土石坝分析", dynamicSourceByMaterial))
        return;

    if (hasWave)
    {
        const QString inputDir = SolverFilePaths::outputInputDirectory(projectDir);
        QStringList invalidWaveFiles;
        for (const QString& name : {QStringLiteral("WAVE-AX.DAT"),
                                    QStringLiteral("WAVE-AY.DAT"),
                                    QStringLiteral("WAVE-AZ.DAT")})
        {
            QString detail;
            if (!waveInputHasSamples(QDir(inputDir).filePath(name), &detail))
                invalidWaveFiles << QStringLiteral("%1：%2").arg(name, detail);
        }
        if (!invalidWaveFiles.isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("地震波数据生成失败"),
                QStringLiteral("地震加速度已通过页面校验，但生成的求解输入仍存在问题：\n%1\n\n"
                               "请返回“约束与荷载 → 地震加速度”检查数据。")
                    .arg(invalidWaveFiles.join(QLatin1Char('\n'))));
            clearSolverRunDirectory();
            return;
        }
    }

    for (int i = 0; i < cyclePages.size(); ++i)
    {
        QString associationError;
        if (!prepareDamSeepageFiles(
                cyclePages.at(i), i + 1, projectDir, &associationError))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("渗流结果关联失败"),
                associationError);
            clearSolverRunDirectory();
            return;
        }
    }

    QVector<QPair<int, int>> fillLayerRanges;
    for (FillStorageCyclePage* cycle : std::as_const(cyclePages))
        fillLayerRanges += cycle->fillLayerRanges();

    QString damLayerError;
    const QString damIiinPath =
        QDir(SolverFilePaths::outputInputDirectory(projectDir))
            .filePath(QStringLiteral("IIIN.DAT"));
    if (!validateDamFillRanges(damIiinPath, fillLayerRanges, &damLayerError))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查施工分层"), damLayerError);
        clearSolverRunDirectory();
        return;
    }

    DamLayerSummary damLayerSummary;
    if (!readDamLayerSummary(damIiinPath, &damLayerSummary, &damLayerError))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查施工分层"), damLayerError);
        clearSolverRunDirectory();
        return;
    }

    auto withTrailingNativeSeparator = [](QString path)
    {
        path = QDir::toNativeSeparators(path);
        const QChar separator = QDir::separator();
        if (!path.endsWith(separator))
            path.append(separator);
        return path;
    };
    const QDir damRunRoot(SolverFilePaths::outputRoot(projectDir));
    for (int i = 0; i < cyclePages.size(); ++i)
    {
        FillStorageCyclePage* cycle = cyclePages.at(i);
        cycle->clearPreparedDynamicStateDirectory();
        QString stateDirectoryName =
            cycle->finalStaticStateDirectoryName(i + 1, damLayerSummary.maximumDamLayer);
        // 求解器先执行结冰变形，再进入模态/地震波分支。结冰启用时，动力初始状态
        // 应继承本工况 DICE 的结果，而不是回退到结冰前的 FILL 状态。
        if (stressPage->freezeThawEnabled())
        {
            const int caseIndex = i + 1;
            const QString caseCode = caseIndex < 10
                                         ? QStringLiteral("0%1").arg(caseIndex)
                                         : QString::number(caseIndex);
            stateDirectoryName =
                QStringLiteral("CASE%1DICE%1").arg(caseCode);
        }
        if (stateDirectoryName.isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("无法确定动力初始状态"),
                QStringLiteral("第 %1 个工况没有形成可用于动力计算的静力状态，请检查施工阶段设置。")
                    .arg(i + 1));
            clearSolverRunDirectory();
            return;
        }
        if ((hasModal || hasWave)
            && stateDirectoryName == QStringLiteral("DISK00"))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("缺少动力初始状态"),
                QStringLiteral("第 %1 个工况启用了动力分析，但施工序列没有形成坝体静力结果。")
                    .arg(i + 1));
            clearSolverRunDirectory();
            return;
        }

        const QString dynamicStatePath = damRunRoot.filePath(stateDirectoryName);
        if (stressPage->dynamicOnlyEnabled()
            && (hasModal || hasWave))
        {
            QString sourcePath = dynamicStatePath;
            QString sourceDescription = stateDirectoryName;

            // 仅动力 + 结冰时，求解器会在进入动力分支前从固定 FILL 入口
            // 重新形成 DICE；若已有 DICE 可直接使用，否则检查它所依赖的 FILL。
            if (stressPage->freezeThawEnabled() && !QDir(sourcePath).exists())
            {
                const int caseIndex = i + 1;
                const QString caseCode = caseIndex < 10
                                             ? QStringLiteral("0%1").arg(caseIndex)
                                             : QString::number(caseIndex);
                sourceDescription = QStringLiteral("CASE%1FILL%1").arg(caseCode);
                sourcePath = damRunRoot.filePath(sourceDescription);
            }

            const QDir sourceDir(sourcePath);
            QStringList missingStateFiles;
            const QStringList requiredStateFiles =
                stressPage->freezeThawEnabled() && sourceDescription.contains(QStringLiteral("FILL"))
                    ? QStringList{QStringLiteral("IIDISP.TXT"),
                                  QStringLiteral("ISGMAR.TXT"),
                                  QStringLiteral("IIICMM.TXT")}
                    : QStringList{QStringLiteral("ISGMAR.TXT"),
                                  QStringLiteral("IIICMM.TXT")};
            if (!sourceDir.exists())
            {
                missingStateFiles << QStringLiteral("结果目录不存在");
            }
            else
            {
                for (const QString& fileName : requiredStateFiles)
                {
                    const QFileInfo info(sourceDir.filePath(fileName));
                    if (!info.isFile() || info.size() <= 0)
                        missingStateFiles << fileName;
                }
            }

            if (!missingStateFiles.isEmpty())
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("缺少已有静力结果"),
                    QStringLiteral("第 %1 个工况没有可用的前序静力结果。请先用相同工况完成一次静力计算，再运行“仅动力计算”。")
                        .arg(i + 1));
                clearSolverRunDirectory();
                return;
            }
        }

        cycle->setPreparedDynamicStateDirectory(
            withTrailingNativeSeparator(dynamicStatePath));
    }

    QFile iautoFile(QDir(SolverFilePaths::outputRoot(projectDir))
                        .absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("土石坝计算数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "3\n";
    }

    bool ok = true;
    QStringList failedFiles;

    QString cycleSectionText;
    int mcase = 0;
    QStringList cycleSections;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* fillStorageCyclePage = dynamic_cast<FillStorageCyclePage*>(child->getPage());
        if (!fillStorageCyclePage)
            continue;
        ++mcase;
        cycleSections << fillStorageCyclePage->sectionText(mcase);
    }
    cycleSectionText =
        QStringLiteral("%1    NCASE\n"
                       " ----------------------------------------------\n")
            .arg(mcase)
        + cycleSections.join(QStringLiteral("\n"));
    stressPage->setMCaseCount(mcase);

    if (!stressPage->exportIfillDatFile(mcase, cycleSectionText))
    {
        ok = false;
        failedFiles << "土石坝主控数据";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundaryPage = dynamic_cast<StressBoundaryPage*>(child->getPage());
        if (!boundaryPage)
            continue;

        const bool oneOk = boundaryPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << child->getName();
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* iceRegionPage = dynamic_cast<SeepageBoundaryPage*>(child->getPage());
        if (!iceRegionPage || iceRegionPage->getPageType() != QStringLiteral("DamIceRegion"))
            continue;

        if (!stressPage->freezeThawEnabled())
        {
            QFile::remove(QDir(SolverFilePaths::outputInputDirectory(projectDir))
                              .filePath(QStringLiteral("JMFW.DAT")));
            continue;
        }

        const bool oneOk = iceRegionPage->exportDatFile();
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << child->getName();
    }

    if (ok)
    {
        launchSolverExecutable(analysisNode, "土石坝分析");
    }
    else
    {
        clearSolverRunDirectory();
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("以下设置未能用于计算：\n%1\n请检查后重试。")
                .arg(failedFiles.join(QLatin1Char('\n'))));
    }
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "土石坝分析失败:" << e.what();
    }
}

void Solver::onComputeCaveExcavation(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    // 先收集开挖级，再生成 IIIN/ICAVE。旧代码先导出 IIIN，导致开挖层号来不及写入。
    struct ExcavationBodyItem
    {
        ProjectNode* node = nullptr;
        CaveExcavationBodyPage* page = nullptr;
        int layer = -1;
    };

    CaveSupportComputationPage* supportPage = nullptr;
    CaveCraneBeamAreaPage* craneBeamAreaPage = nullptr;
    StressBoundaryPage* liningInnerBoundaryPage = nullptr;
    StressBoundaryPage* liningOuterBoundaryPage = nullptr;
    QVector<ExcavationBodyItem> excavationBodies;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* candidate = dynamic_cast<CaveSupportComputationPage*>(child->getPage());
        if (!candidate)
            continue;

        supportPage = candidate;
        for (ProjectNode* bodyNode : child->getChildren())
        {
            if (!bodyNode || !bodyNode->getPage())
                continue;
            const QString pageType = bodyNode->getPage()->getPageType();
            if (pageType == QStringLiteral("CaveLiningInnerBoundary"))
                liningInnerBoundaryPage =
                    dynamic_cast<StressBoundaryPage*>(bodyNode->getPage());
            else if (pageType == QStringLiteral("CaveLiningOuterBoundary"))
                liningOuterBoundaryPage =
                    dynamic_cast<StressBoundaryPage*>(bodyNode->getPage());
            else if (pageType == QStringLiteral("CaveCraneBeamArea"))
                craneBeamAreaPage = dynamic_cast<CaveCraneBeamAreaPage*>(bodyNode->getPage());
            else if (auto* bodyPage =
                         dynamic_cast<CaveExcavationBodyPage*>(bodyNode->getPage()))
                excavationBodies.push_back(
                    ExcavationBodyItem{bodyNode, bodyPage, bodyPage->excavationLayer()});
        }
        break;
    }

    if (!supportPage)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("当前洞室开挖设置未正确加载。请重新打开工程后再试。"));
        return;
    }
    CaveReinforcementFieldPage* reinforcementPage =
        supportPage->reinforcementFieldPage();
    QString caveParameterError;
    if (!supportPage->validateInputs(&caveParameterError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请完善洞室开挖设置"),
                             caveParameterError);
        return;
    }
    if (supportPage->craneBeamEnabled())
    {
        if (!craneBeamAreaPage)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善吊车梁设置"),
                QStringLiteral("吊车梁区域设置未正确加载。请重新打开工程后再试。"));
            return;
        }
        QString craneError;
        if (craneBeamAreaPage->selectedMaterialIds(&craneError).isEmpty())
        {
            QMessageBox::warning(nullptr, QStringLiteral("请完善吊车梁设置"), craneError);
            return;
        }
    }
    if (supportPage->liningEnabled())
    {
        if (!liningInnerBoundaryPage || !liningOuterBoundaryPage)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善衬砌设置"),
                QStringLiteral("衬砌边界设置未正确加载。请重新打开工程后再试。"));
            return;
        }
        if (liningInnerBoundaryPage->selectionCount() <= 0)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善衬砌设置"),
                QStringLiteral("请在“衬砌内边界”中选择衬砌与洞腔接触的网格边或网格面。"));
            return;
        }
        if (liningOuterBoundaryPage->selectionCount() <= 0)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善衬砌设置"),
                QStringLiteral("请在“衬砌外边界”中选择衬砌与围岩接触的网格边或网格面。"));
            return;
        }
    }
    if (excavationBodies.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请添加开挖体"),
            QStringLiteral("请至少添加一个开挖体，并选择对应区域。"));
        return;
    }

    QHash<int, QString> ownerByLayer;
    for (const ExcavationBodyItem& body : std::as_const(excavationBodies))
    {
        if (body.layer >= 0)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖顺序"),
                QStringLiteral("“%1”的开挖顺序无效，请从第 1 级开始连续设置。")
                    .arg(body.node ? body.node->text(0) : QStringLiteral("开挖体")));
            return;
        }
        if (ownerByLayer.contains(body.layer))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖顺序"),
                QStringLiteral("“%1”和“%2”使用了同一开挖阶段。每个阶段只保留一个开挖体；"
                               "同一阶段的多个区域请合并到同一个节点。")
                    .arg(ownerByLayer.value(body.layer),
                         body.node ? body.node->text(0) : QStringLiteral("开挖体")));
            return;
        }
        ownerByLayer.insert(
            body.layer, body.node ? body.node->text(0) : QStringLiteral("开挖体"));
    }

    std::stable_sort(excavationBodies.begin(), excavationBodies.end(),
                     [](const ExcavationBodyItem& lhs, const ExcavationBodyItem& rhs)
                     {
                         return lhs.layer > rhs.layer; // -1、-2、-3……
                     });
    for (int index = 0; index < excavationBodies.size(); ++index)
    {
        const int expectedLayer = -(index + 1);
        if (excavationBodies[index].layer != expectedLayer)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖顺序"),
                QStringLiteral("开挖阶段必须从第 1 级开始连续设置，不能跳级。"));
            return;
        }
    }

    const int caveCalculationType = supportPage->calculationTypeCode();
    const int caveLoadMethod = supportPage->excavationLoadMethodCode();
    if (caveCalculationType < 1 || caveCalculationType > 3)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查计算类型"),
            QStringLiteral("请选择静力、动力或静力与动力组合计算。"));
        return;
    }
    if (caveLoadMethod < 1 || caveLoadMethod > 2)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查开挖荷载"),
            QStringLiteral("请选择按开挖面或按开挖单元施加开挖荷载。"));
        return;
    }

    auto* caveMeshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    vtkSmartPointer<vtkUnstructuredGrid> caveGrid =
        caveMeshSvc ? caveMeshSvc->getVtkUnstructuredGridSolid() : nullptr;
    if (!caveGrid || caveGrid->GetNumberOfCells() <= 0)
        caveGrid = caveMeshSvc ? caveMeshSvc->getVtkUnstructuredGrid() : nullptr;

    bool hasTetrahedron = false;
    if (caveGrid)
    {
        for (vtkIdType cid = 0; cid < caveGrid->GetNumberOfCells(); ++cid)
        {
            vtkCell* cell = caveGrid->GetCell(cid);
            if (cell && cell->GetCellDimension() == 3 && cell->GetCellType() == VTK_TETRA)
            {
                hasTetrahedron = true;
                break;
            }
        }
    }
    if (hasTetrahedron)
    {
        qWarning() << "cave excavation continues with four-node tetrahedral cells";
    }

    const QDir projectRoot(QDir(projectDir).absolutePath());
    const QString stressRelativePath = caveLoadMethod == 1
                                           ? QStringLiteral("GEOSTR/MODSTR.TXT")
                                           : QStringLiteral("GEOSTR/ISGMAR.TXT");
    QString stressFile = QDir(analysisWorkspace(analysisNode)).filePath(stressRelativePath);
    if (!QFileInfo::exists(stressFile))
        stressFile = projectRoot.filePath(stressRelativePath);
    if (!QFileInfo::exists(stressFile))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("缺少初始地应力"),
            QStringLiteral("当前工程还没有可用的初始地应力结果。请先完成地应力回归分析。"));
        return;
    }

    const bool needsDynamicFiles = caveCalculationType != 1 ||
                                   supportPage->modalAnalysisEnabled() ||
                                   supportPage->timeHistoryEnabled();
    if (needsDynamicFiles &&
        !QFileInfo::exists(QDir(SolverFilePaths::projectInputDirectory(projectDir))
                               .filePath(QStringLiteral("TDTRACE.DAT"))))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请完善动力设置"),
            QStringLiteral("动力计算设置不完整。仅进行静力开挖时，请关闭模态分析和时程分析。"));
        return;
    }

    QVector<QVector<int>> selectedSourceElements;
    selectedSourceElements.reserve(excavationBodies.size());
    QSet<int> assignedSourceElements;
    for (int level = 0; level < excavationBodies.size(); ++level)
    {
        QVector<int> elemIds = excavationBodies[level].page->selectedElementIds();
        std::sort(elemIds.begin(), elemIds.end());
        elemIds.erase(std::unique(elemIds.begin(), elemIds.end()), elemIds.end());

        if (elemIds.isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖区域"),
                QStringLiteral("“%1”尚未选择有效的实体区域。")
                    .arg(excavationBodies[level].node
                             ? excavationBodies[level].node->text(0)
                             : QStringLiteral("开挖体")));
            return;
        }

        for (int elemId : elemIds)
        {
            if (assignedSourceElements.contains(elemId))
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("请检查开挖区域"),
                    QStringLiteral("同一网格区域被重复分配到多个开挖阶段，请删除重复选择。"));
                return;
            }
            assignedSourceElements.insert(elemId);
        }
        selectedSourceElements.push_back(elemIds);
    }

    // ICAVE 中的 IEXCA_MAX、IEXCA_STA、IEXCA_END 与实际开挖体数量保持一致。
    supportPage->setExcavationLevelCount(excavationBodies.size());
    if (!beginCommonCompute(analysisNode, "洞室开挖"))
        return;

    const QString supportCaseSection = supportPage->sectionText();
    QFile iautoFile(QDir(SolverFilePaths::outputRoot(projectDir))
                        .absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("洞室开挖计算数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "5\n";
    }

    bool ok = true;
    QStringList failedFiles;

    if (!supportPage->exportICaveDatFile(supportCaseSection))
    {
        ok = false;
        failedFiles << "ICAVE.DAT";
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* geoPage = dynamic_cast<GeoStressRegressionWidget*>(child->getPage());
        if (!geoPage)
            continue;

        const bool oneOk = geoPage->exportIGEOSDatFile(buildGeoStressPayload(child));
        ok = ok && oneOk;
        if (!oneOk)
            failedFiles << "IGEOS.DAT";
    }

    QDir wd(SolverFilePaths::outputRoot(projectDir));
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
    {
        ok = false;
        failedFiles << QStringLiteral("计算输入");
    }
    else
    {
        if (supportPage->liningEnabled())
        {
            if (!liningInnerBoundaryPage->exportDatFile())
            {
                ok = false;
                failedFiles << QStringLiteral("衬砌内边界");
            }
            if (!liningOuterBoundaryPage->exportDatFile())
            {
                ok = false;
                failedFiles << QStringLiteral("衬砌外边界");
            }
        }
        else
        {
            QFile::remove(inputDir.filePath(QStringLiteral("IIJX_CHEN.DAT")));
            QFile::remove(inputDir.filePath(QStringLiteral("IIJX_WAI.DAT")));
        }

        if (supportPage->craneBeamEnabled())
        {
            QString craneError;
            if (!craneBeamAreaPage
                || !craneBeamAreaPage->exportMaterialSet(inputDir.absolutePath(), &craneError))
            {
                ok = false;
                failedFiles << (craneError.isEmpty()
                                    ? QStringLiteral("吊车梁区域") : craneError);
            }
        }
        else
        {
            QFile::remove(inputDir.filePath(QStringLiteral("MASET_DCL.DAT")));
        }

        QString caveAuxiliaryError;
        if (!supportPage->prepareAuxiliaryInputFiles(inputDir.absolutePath(),
                                                      &caveAuxiliaryError))
        {
            ok = false;
            failedFiles << (caveAuxiliaryError.isEmpty()
                                ? QStringLiteral("洞室地下水数据")
                                : caveAuxiliaryError);
        }

        auto writeReleKetoke = [&]() -> bool
        {
            auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            if (!meshSvc)
                return false;

            vtkSmartPointer<vtkUnstructuredGrid> grid = meshSvc->getVtkUnstructuredGrid();
            if (!grid || grid->GetNumberOfCells() <= 0)
                return false;

            vtkDataArray* elemIdArr =
                vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

            struct ElemCellPair
            {
                int elemId = 0;
                vtkIdType cellId = -1;
            };

            const int targetDim =
                PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
            const QFileInfo sourceInfo(meshSvc->sourceMeshFilePath());
            const bool keepSourceElementIds =
                sourceInfo.exists() &&
                sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;

            QVector<ElemCellPair> elemCells;
            elemCells.reserve(int(grid->GetNumberOfCells()));
            int exportedElementIndex = 0;
            for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell)
                    continue;
                const int cellDim = cell->GetCellDimension();
                if (cellDim != targetDim && cellDim != 1)
                    continue;

                ++exportedElementIndex;
                const int sourceElemId =
                    elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                const int calculationElemId =
                    keepSourceElementIds ? sourceElemId : exportedElementIndex;
                elemCells.push_back({calculationElemId, cellId});
            }
            std::sort(elemCells.begin(), elemCells.end(),
                      [](const ElemCellPair& a, const ElemCellPair& b)
                      { return a.elemId < b.elemId; });

            QHash<vtkIdType, QVector<int>> pointToElemIds;
            pointToElemIds.reserve(int(grid->GetNumberOfPoints() * 1.3));
            for (const ElemCellPair& ec : elemCells)
            {
                vtkCell* cell = grid->GetCell(ec.cellId);
                vtkIdList* pids = cell ? cell->GetPointIds() : nullptr;
                if (!pids)
                    continue;
                for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
                    pointToElemIds[pids->GetId(i)].push_back(ec.elemId);
            }

            QVector<QVector<int>> neighborsByElem(elemCells.size());
            int maxRelated = 0;
            for (int i = 0; i < elemCells.size(); ++i)
            {
                const int selfElemId = elemCells[i].elemId;
                vtkCell* cell = grid->GetCell(elemCells[i].cellId);
                vtkIdList* pids = cell ? cell->GetPointIds() : nullptr;
                if (!pids)
                    continue;

                QSet<int> related;
                for (vtkIdType j = 0; j < pids->GetNumberOfIds(); ++j)
                {
                    const auto list = pointToElemIds.value(pids->GetId(j));
                    for (int elemId : list)
                    {
                        if (elemId != selfElemId)
                            related.insert(elemId);
                    }
                }

                QVector<int> rel = related.values().toVector();
                std::sort(rel.begin(), rel.end());
                maxRelated = qMax(maxRelated, rel.size());
                neighborsByElem[i] = std::move(rel);
            }

            QFile file(inputDir.filePath(QStringLiteral("RELE_KETOKE.DAT")));
            if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QTextStream os(&file);
            const int nelem = elemCells.size();
            os << QString("%1%2%3%4   NUMNP   MALIA  NLIA_MAX   IP\n")
                      .arg(nelem, 12)
                      .arg(maxRelated, 10)
                      .arg(maxRelated, 10)
                      .arg(nelem, 10);
            os << "NJM   LJM(1)    LJM(2)   LJM(3)  .........LJM(N)\n";

            for (int i = 0; i < elemCells.size(); ++i)
            {
                const QVector<int>& rel = neighborsByElem[i];
                os << QString("%1").arg(rel.size(), 4);
                for (int j = 0; j < maxRelated; ++j)
                {
                    const int v = (j < rel.size()) ? rel[j] : 0;
                    os << QString("%1").arg(v, 8);
                }
                os << QString("%1\n").arg(elemCells[i].elemId, 9);
            }
            return true;
        };

        if (!writeReleKetoke())
        {
            ok = false;
            failedFiles << QStringLiteral("RELE_KETOKE.DAT");
        }

        const QString inputPath =
            QDir::toNativeSeparators(inputDir.absolutePath()) + QDir::separator();

        struct CaveElemContext
        {
            vtkSmartPointer<vtkUnstructuredGrid> grid;
            vtkDataArray* elemIdArr = nullptr;
            vtkDataArray* nodeIdArr = nullptr;
            QHash<int, vtkIdType> sourceElemToCellId;
            QHash<int, int> sourceElemToCalculationElemId;
            QHash<int, vtkIdType> calculationElemToCellId;
            int targetDim = 3;
            bool keepSourceElementIds = false;
        };

        auto buildCaveElemContext = [&]() -> CaveElemContext
        {
            CaveElemContext ctx;
            ctx.targetDim =
                PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();

            auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
            if (!meshSvc)
                return ctx;

            const QFileInfo sourceInfo(meshSvc->sourceMeshFilePath());
            ctx.keepSourceElementIds =
                sourceInfo.exists() &&
                sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;

            ctx.grid = meshSvc->getVtkUnstructuredGrid();

            if (!ctx.grid || ctx.grid->GetNumberOfCells() <= 0)
                return ctx;

            ctx.elemIdArr =
                vtkDataArray::SafeDownCast(ctx.grid->GetCellData()->GetArray("elem_id"));

            if (ctx.grid->GetPointData())
            {
                ctx.nodeIdArr =
                    vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("node_id"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("nodeId"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("point_id"));
                if (!ctx.nodeIdArr)
                    ctx.nodeIdArr =
                        vtkDataArray::SafeDownCast(ctx.grid->GetPointData()->GetArray("global_node_id"));
            }

            ctx.sourceElemToCellId.reserve(int(ctx.grid->GetNumberOfCells()));
            ctx.sourceElemToCalculationElemId.reserve(int(ctx.grid->GetNumberOfCells()));
            ctx.calculationElemToCellId.reserve(int(ctx.grid->GetNumberOfCells()));

            int exportedElementIndex = 0;
            for (vtkIdType cellId = 0; cellId < ctx.grid->GetNumberOfCells(); ++cellId)
            {
                vtkCell* cell = ctx.grid->GetCell(cellId);
                if (!cell)
                    continue;

                const int cellDim = cell->GetCellDimension();
                if (cellDim != ctx.targetDim && cellDim != 1)
                    continue;

                ++exportedElementIndex;

                // 杆/线单元会导出，但不能作为开挖单元。
                if (cellDim != ctx.targetDim)
                    continue;

                const int sourceElemId =
                    ctx.elemIdArr ? int(ctx.elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
                if (sourceElemId <= 0)
                    continue;

                // 直接导入 IIIN.DAT 时保留原单元号；其他网格导出 IIIN 时按导出顺序重新编号。
                const int calculationElemId =
                    ctx.keepSourceElementIds ? sourceElemId : exportedElementIndex;

                if (!ctx.sourceElemToCellId.contains(sourceElemId))
                    ctx.sourceElemToCellId.insert(sourceElemId, cellId);
                if (!ctx.sourceElemToCalculationElemId.contains(sourceElemId))
                    ctx.sourceElemToCalculationElemId.insert(sourceElemId, calculationElemId);
                if (!ctx.calculationElemToCellId.contains(calculationElemId))
                    ctx.calculationElemToCellId.insert(calculationElemId, cellId);
            }

            return ctx;
        };

        CaveElemContext caveCtx = buildCaveElemContext();

        auto normalizeExcavationElems = [&](const QVector<int>& rawSourceElemIds) -> QVector<int>
        {
            QSet<int> uniq;
            for (int sourceElemId : rawSourceElemIds)
            {
                // 将界面保存的原始 elem_id 转成 IIIN/IIKE 实际使用的计算单元号。
                const auto it = caveCtx.sourceElemToCalculationElemId.constFind(sourceElemId);
                if (it != caveCtx.sourceElemToCalculationElemId.constEnd())
                    uniq.insert(it.value());
            }

            QVector<int> out = uniq.values().toVector();
            std::sort(out.begin(), out.end());
            return out;
        };

        if (!caveCtx.grid || caveCtx.calculationElemToCellId.isEmpty())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖区域"),
                QStringLiteral("部分开挖区域与当前网格不一致。请重新选择开挖区域后重试。"));
            return;
        }

        QVector<QVector<int>> calculationElementsByLevel;
        calculationElementsByLevel.reserve(selectedSourceElements.size());
        for (int level = 0; level < selectedSourceElements.size(); ++level)
        {
            const QVector<int> calculationIds =
                normalizeExcavationElems(selectedSourceElements[level]);
            if (calculationIds.size() != selectedSourceElements[level].size())
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("请检查开挖区域"),
                    QStringLiteral("第 %1 级开挖体与当前网格不一致。请重新选择该区域后重试。")
                        .arg(level + 1));
                return;
            }
            calculationElementsByLevel.push_back(calculationIds);
        }

        QHash<int, int> caveLayerByElementId;
        caveLayerByElementId.reserve(caveCtx.calculationElemToCellId.size());
        for (auto it = caveCtx.calculationElemToCellId.constBegin();
             it != caveCtx.calculationElemToCellId.constEnd(); ++it)
        {
            caveLayerByElementId.insert(it.key(), 1);
        }
        for (int level = 0; level < calculationElementsByLevel.size(); ++level)
        {
            for (int calculationElemId : calculationElementsByLevel[level])
                caveLayerByElementId[calculationElemId] = excavationBodies[level].layer;
        }

        if (!patchIiinElementLayers(inputDir.filePath(QStringLiteral("IIIN.DAT")),
                                    caveLayerByElementId))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查开挖顺序"),
                QStringLiteral("开挖层级设置无效。请确认各级开挖从第 1 级开始连续设置。"));
            clearSolverRunDirectory();
            return;
        }

        // 删除上一轮遗留的开挖文件，避免旧级数文件干扰本次计算。
        QDir inputDirectory(inputDir.absolutePath());
        const QStringList staleFilters = {
            QStringLiteral("IIKE_E*.DAT"), QStringLiteral("IIKE_T*.DAT"),
            QStringLiteral("IIJX_E*.DAT"), QStringLiteral("IIJX_T*.DAT")};
        for (const QString& staleName :
             inputDirectory.entryList(staleFilters, QDir::Files | QDir::NoSymLinks))
        {
            inputDirectory.remove(staleName);
        }

        auto nodeIdOfPoint = [&](vtkIdType pointId) -> int
        {
            if (caveCtx.nodeIdArr && pointId >= 0 &&
                pointId < caveCtx.nodeIdArr->GetNumberOfTuples())
            {
                const int nodeId = int(caveCtx.nodeIdArr->GetTuple1(pointId));
                if (nodeId > 0)
                    return nodeId;
            }
            return int(pointId + 1);
        };

        auto writeIike = [&](const QString& fileName, const QVector<int>& kes, int iex1,
                             int iex2) -> bool
        {
            QFile f(inputDir.filePath(fileName));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QTextStream os(&f);
            os.setEncoding(QStringConverter::System);
            os << QString("%1  NUMKE\n").arg(kes.size(), 12);
            for (int i = 0; i < kes.size(); ++i)
                os << QString("%1%2\n").arg(kes[i], 8).arg(i + 1, 10);
            os << QString("    DIR=%1                                                    \n").arg(inputPath);
            os << " ISHIFT=       1\n";
            os << "   NJX=       0\n";
            os << QString(" IEX1=%1  IEX2=%2  \n").arg(iex1, 4).arg(iex2, 4);
            return true;
        };

        struct IijxRow
        {
            int ke1 = 0;
            int ke2 = -1;
            QVector<int> nodeIds;
            int jat = 0;
            std::array<double, 3> center{{0.0, 0.0, 0.0}};
        };

        QVector<ReinforcementFaceSample> reinforcementFaces;

        auto writeIijx = [&](const QString& fileName, const QVector<int>& kes, int iex1,
                             int iex2) -> bool
        {
            QFile f(inputDir.filePath(fileName));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return false;

            QVector<IijxRow> rows;

            if (caveCtx.grid)
            {
                QHash<vtkIdType, int> cellToCalculationElemId;
                cellToCalculationElemId.reserve(caveCtx.calculationElemToCellId.size());
                for (auto it = caveCtx.calculationElemToCellId.constBegin();
                     it != caveCtx.calculationElemToCellId.constEnd(); ++it)
                {
                    cellToCalculationElemId.insert(it.value(), it.key());
                }

                if (caveCtx.targetDim == 2)
                {
                    struct EdgeOwner
                    {
                        int ke = 0;
                        QVector<vtkIdType> pointIds;
                    };

                    std::map<std::vector<vtkIdType>, std::vector<EdgeOwner>> edgeOwners;

                    for (vtkIdType cellId = 0; cellId < caveCtx.grid->GetNumberOfCells();
                         ++cellId)
                    {
                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int ownerKe = cellToCalculationElemId.value(cellId, 0);
                        if (ownerKe <= 0)
                            continue;

                        const int edgeCount = cell->GetNumberOfEdges();
                        for (int edgeId = 0; edgeId < edgeCount; ++edgeId)
                        {
                            vtkCell* edge = cell->GetEdge(edgeId);
                            if (!edge || !edge->GetPointIds() ||
                                edge->GetPointIds()->GetNumberOfIds() < 2)
                                continue;

                            EdgeOwner owner;
                            owner.ke = ownerKe;
                            owner.pointIds = {edge->GetPointId(0), edge->GetPointId(1)};

                            std::vector<vtkIdType> key{
                                owner.pointIds[0], owner.pointIds[1]};
                            std::sort(key.begin(), key.end());
                            edgeOwners[key].push_back(owner);
                        }
                    }

                    QSet<int> selectedKes;
                    for (int ke : kes)
                        selectedKes.insert(ke);

                    std::set<std::vector<vtkIdType>> writtenEdges;
                    for (int ke : kes)
                    {
                        const vtkIdType cellId =
                            caveCtx.calculationElemToCellId.value(ke, -1);
                        if (cellId < 0)
                            continue;

                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int edgeCount = cell->GetNumberOfEdges();
                        for (int edgeId = 0; edgeId < edgeCount; ++edgeId)
                        {
                            vtkCell* edge = cell->GetEdge(edgeId);
                            if (!edge || !edge->GetPointIds() ||
                                edge->GetPointIds()->GetNumberOfIds() < 2)
                                continue;

                            const vtkIdType p1 = edge->GetPointId(0);
                            const vtkIdType p2 = edge->GetPointId(1);
                            std::vector<vtkIdType> key{p1, p2};
                            std::sort(key.begin(), key.end());

                            if (writtenEdges.find(key) != writtenEdges.end())
                                continue;

                            const auto ownerIt = edgeOwners.find(key);
                            if (ownerIt == edgeOwners.end())
                                continue;

                            bool hasOtherSelectedOwner = false;
                            int oppositeKe = -1;
                            for (const EdgeOwner& owner : ownerIt->second)
                            {
                                if (owner.ke == ke)
                                    continue;

                                if (selectedKes.contains(owner.ke))
                                {
                                    hasOtherSelectedOwner = true;
                                    break;
                                }

                                if (oppositeKe < 0)
                                    oppositeKe = owner.ke;
                            }

                            if (hasOtherSelectedOwner)
                                continue;

                            writtenEdges.insert(key);

                            IijxRow row;
                            row.ke1 = ke;
                            row.ke2 = oppositeKe;
                            row.nodeIds = {nodeIdOfPoint(p1), nodeIdOfPoint(p2)};
                            double xyz[3]{};
                            caveCtx.grid->GetPoint(p1, xyz);
                            for (int axis = 0; axis < 3; ++axis)
                                row.center[axis] += xyz[axis] * 0.5;
                            caveCtx.grid->GetPoint(p2, xyz);
                            for (int axis = 0; axis < 3; ++axis)
                                row.center[axis] += xyz[axis] * 0.5;
                            rows.push_back(row);
                        }
                    }
                }
                else
                {
                    struct FaceOwner
                    {
                        int ke = 0;
                        QVector<vtkIdType> pointIds;
                    };

                    std::map<std::vector<vtkIdType>, std::vector<FaceOwner>> faceOwners;

                    for (vtkIdType cellId = 0; cellId < caveCtx.grid->GetNumberOfCells();
                         ++cellId)
                    {
                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int ownerKe = cellToCalculationElemId.value(cellId, 0);
                        if (ownerKe <= 0)
                            continue;

                        const int faceCount = cell->GetNumberOfFaces();
                        for (int faceId = 0; faceId < faceCount; ++faceId)
                        {
                            vtkCell* face = cell->GetFace(faceId);
                            if (!face || !face->GetPointIds())
                                continue;

                            const vtkIdType faceNodeCount =
                                face->GetPointIds()->GetNumberOfIds();
                            if (faceNodeCount < 3 || faceNodeCount > 4)
                                continue;

                            FaceOwner owner;
                            owner.ke = ownerKe;
                            owner.pointIds.reserve(int(faceNodeCount));

                            std::vector<vtkIdType> key;
                            key.reserve(size_t(faceNodeCount));
                            for (vtkIdType i = 0; i < faceNodeCount; ++i)
                            {
                                const vtkIdType pid = face->GetPointId(i);
                                owner.pointIds.push_back(pid);
                                key.push_back(pid);
                            }
                            std::sort(key.begin(), key.end());
                            faceOwners[key].push_back(owner);
                        }
                    }

                    QSet<int> selectedKes;
                    for (int ke : kes)
                        selectedKes.insert(ke);

                    std::set<std::vector<vtkIdType>> writtenFaces;
                    for (int ke : kes)
                    {
                        const vtkIdType cellId =
                            caveCtx.calculationElemToCellId.value(ke, -1);
                        if (cellId < 0)
                            continue;

                        vtkCell* cell = caveCtx.grid->GetCell(cellId);
                        if (!cell || cell->GetCellDimension() != caveCtx.targetDim)
                            continue;

                        const int faceCount = cell->GetNumberOfFaces();
                        for (int faceId = 0; faceId < faceCount; ++faceId)
                        {
                            vtkCell* face = cell->GetFace(faceId);
                            if (!face || !face->GetPointIds())
                                continue;

                            const vtkIdType faceNodeCount =
                                face->GetPointIds()->GetNumberOfIds();
                            if (faceNodeCount < 3 || faceNodeCount > 4)
                                continue;

                            QVector<vtkIdType> orientedPointIds;
                            orientedPointIds.reserve(int(faceNodeCount));
                            std::vector<vtkIdType> key;
                            key.reserve(size_t(faceNodeCount));

                            for (vtkIdType i = 0; i < faceNodeCount; ++i)
                            {
                                const vtkIdType pid = face->GetPointId(i);
                                orientedPointIds.push_back(pid);
                                key.push_back(pid);
                            }
                            std::sort(key.begin(), key.end());

                            if (writtenFaces.find(key) != writtenFaces.end())
                                continue;

                            const auto ownerIt = faceOwners.find(key);
                            if (ownerIt == faceOwners.end())
                                continue;

                            bool hasOtherSelectedOwner = false;
                            int oppositeKe = -1;
                            for (const FaceOwner& owner : ownerIt->second)
                            {
                                if (owner.ke == ke)
                                    continue;

                                if (selectedKes.contains(owner.ke))
                                {
                                    hasOtherSelectedOwner = true;
                                    break;
                                }

                                if (oppositeKe < 0)
                                    oppositeKe = owner.ke;
                            }

                            // 两侧都属于开挖体的是内部面，不应写入 IIJX。
                            if (hasOtherSelectedOwner)
                                continue;

                            writtenFaces.insert(key);

                            IijxRow row;
                            row.ke1 = ke;
                            row.ke2 = oppositeKe;
                            row.nodeIds.reserve(int(faceNodeCount));
                            for (vtkIdType pid : orientedPointIds)
                                row.nodeIds.push_back(nodeIdOfPoint(pid));

                            for (vtkIdType pid : orientedPointIds)
                            {
                                double xyz[3]{};
                                caveCtx.grid->GetPoint(pid, xyz);
                                for (int axis = 0; axis < 3; ++axis)
                                    row.center[axis] += xyz[axis] / orientedPointIds.size();
                            }

                            // 四面体边界面保留 3 个节点；六面体边界面保留 4 个节点。
                            // MAKOD 必须等于 2 + 实际面节点数，否则 EXCAIIWL 会在 IIWL 中
                            // 生成错误的 NFAJD。
                            if (row.nodeIds.size() < 3 || row.nodeIds.size() > 4)
                                continue;

                            rows.push_back(row);
                        }
                    }
                }
            }

            int faceNodeCount = (caveCtx.targetDim == 2 ? 2 : 0);
            for (const IijxRow& row : rows)
                faceNodeCount = qMax(faceNodeCount, row.nodeIds.size());

            if (caveCtx.targetDim == 3 && faceNodeCount == 0)
                faceNodeCount = 3;

            // IIJX 的 MAKOD = KE1、KE2 两列 + 实际面节点数。
            // 四面体：3 节点面 -> MAKOD=5 -> 后续 IIWL 中 NFAJD=3。
            // 六面体：4 节点面 -> MAKOD=6 -> 后续 IIWL 中 NFAJD=4。
            const int makod = faceNodeCount + 2;

            // 若同一文件同时存在三角面和四边形面，统一按最大节点数输出；
            // 只有这种混合情况才补重复节点。
            if (caveCtx.targetDim == 3 && faceNodeCount == 4)
            {
                for (IijxRow& row : rows)
                {
                    while (row.nodeIds.size() < 4 && !row.nodeIds.isEmpty())
                        row.nodeIds.push_back(row.nodeIds.last());
                }
            }

            if (fileName.startsWith(QStringLiteral("IIJX_T")))
            {
                // 始终保留当前累计开挖体的外表面；循环结束后即为最终开挖边界，
                // 不会把相邻开挖体之间已经消失的内部交界面当作加固面。
                reinforcementFaces.clear();
                for (const IijxRow& row : std::as_const(rows))
                {
                    reinforcementFaces.push_back(
                        {row.center, caveLayerByElementId.value(row.ke1, iex2)});
                }
            }

            QTextStream os(&f);
            os.setEncoding(QStringConverter::System);
            os << QString("%1%2     NJX   MAKOD\n")
                      .arg(rows.size(), 9)
                      .arg(makod, 9);

            if (caveCtx.targetDim == 2)
                os << "    KJX     KE1     KE2     JD1     JD2  JAT\n";
            else if (faceNodeCount == 3)
                os << "    KJX     KE1     KE2     JD1     JD2     JD3  JAT\n";
            else
                os << "    KJX     KE1     KE2     JD1     JD2     JD3     JD4  JAT\n";

            for (int i = 0; i < rows.size(); ++i)
            {
                const IijxRow& row = rows[i];

                os << QString("%1%2%3")
                          .arg(i + 1, 9)
                          .arg(row.ke1, 9)
                          .arg(row.ke2, 9);

                for (int nodeId : row.nodeIds)
                    os << QString("%1").arg(nodeId, 9);

                // 通用 READ_IIJX 会读取 JAT；ARCHFTVECT 在读满所需字段后会丢弃本行尾部 JAT。
                os << QString("%1\n").arg(row.jat, 10);
            }

            os << QString("    DIR=%1                                                    \n").arg(inputPath);
            os << " ISHIFT=       2\n";
            os << QString("   NJX=%1\n").arg(rows.size(), 8);
            os << QString(" IEX1=%1  IEX2=%2  \n").arg(iex1, 4).arg(iex2, 4);
            return !rows.isEmpty();
        };

        QSet<int> cumulative;
        for (int i = 0; i < excavationBodies.size(); ++i)
        {
            const QVector<int>& e = calculationElementsByLevel[i];
            const int excavationLayer = excavationBodies[i].layer;

            const QString idx =
                QString("%1").arg(qAbs(excavationLayer), 2, 10, QLatin1Char('0'));
            const QString eFile = QString("IIKE_E%1-%1.DAT").arg(idx);
            if (!writeIike(eFile, e, excavationLayer, excavationLayer))
            {
                ok = false;
                failedFiles << eFile;
            }

            const QString ejxFile = QString("IIJX_E%1-%1.DAT").arg(idx);
            if (!writeIijx(ejxFile, e, excavationLayer, excavationLayer))
            {
                ok = false;
                failedFiles << ejxFile;
            }

            for (int ke : e)
                cumulative.insert(ke);

            QVector<int> t = cumulative.values().toVector();
            std::sort(t.begin(), t.end());

            const QString tFile = QString("IIKE_T01-%1.DAT").arg(idx);
            if (!writeIike(tFile, t, -1, excavationLayer))
            {
                ok = false;
                failedFiles << tFile;
            }

            const QString tjxFile = QString("IIJX_T01-%1.DAT").arg(idx);
            if (!writeIijx(tjxFile, t, -1, excavationLayer))
            {
                ok = false;
                failedFiles << tjxFile;
            }
        }


        QHash<vtkIdType, int> effectiveCellLayers;
        QHash<vtkIdType, int> calculationElementByCell;
        calculationElementByCell.reserve(caveCtx.calculationElemToCellId.size());
        for (auto it = caveCtx.calculationElemToCellId.constBegin();
             it != caveCtx.calculationElemToCellId.constEnd(); ++it)
            calculationElementByCell.insert(it.value(), it.key());
        vtkDataArray* gridLayers = caveCtx.grid->GetCellData()->GetArray("LAYER");
        for (vtkIdType cellId = 0; cellId < caveCtx.grid->GetNumberOfCells(); ++cellId)
        {
            vtkCell* cell = caveCtx.grid->GetCell(cellId);
            if (!cell || (cell->GetCellDimension() != caveCtx.targetDim &&
                          cell->GetCellDimension() != 1))
                continue;
            int layer = gridLayers ? static_cast<int>(gridLayers->GetTuple1(cellId)) : 1;
            if (cell->GetCellDimension() == caveCtx.targetDim)
            {
                const int calculationElement = calculationElementByCell.value(cellId, 0);
                if (calculationElement > 0)
                    layer = caveLayerByElementId.value(calculationElement, layer);
            }
            effectiveCellLayers.insert(cellId, layer);
        }

        QString reinforcementError;
        if (reinforcementPage && supportPage->reinforcedTaskEnabled())
        {
            if (!reinforcementPage->exportFieldFiles(
                    inputDir.absolutePath(), caveCtx.grid, caveCtx.targetDim,
                    effectiveCellLayers, reinforcementFaces, &reinforcementError))
            {
                ok = false;
                failedFiles << (reinforcementError.isEmpty()
                                    ? QStringLiteral("ALFAIE.DAT/ALFAJD.DAT")
                                    : reinforcementError);
            }
        }
        else
        {
            QFile::remove(inputDir.filePath(QStringLiteral("ALFAIE.DAT")));
            QFile::remove(inputDir.filePath(QStringLiteral("ALFAJD.DAT")));
        }


        auto hasPositiveRecordCount = [&](const QString& fileName) -> bool
        {
            QFile file(inputDir.filePath(fileName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                return false;
            const QString firstLine = QString::fromLocal8Bit(file.readLine());
            const QRegularExpressionMatch match =
                QRegularExpression(QStringLiteral("[-+]?\\d+")).match(firstLine);
            return match.hasMatch() && match.captured().toInt() > 0;
        };

        const QString lastIndex =
            QString("%1").arg(excavationBodies.size(), 2, 10, QLatin1Char('0'));
        const QString expectedTotalIike = QString("IIKE_T01-%1.DAT").arg(lastIndex);
        const QString expectedTotalIijx = QString("IIJX_T01-%1.DAT").arg(lastIndex);
        if (!hasPositiveRecordCount(expectedTotalIike))
        {
            ok = false;
            failedFiles << expectedTotalIike + QStringLiteral("（不存在或无开挖单元）");
        }
        if (!QFileInfo::exists(inputDir.filePath(expectedTotalIijx)))
        {
            ok = false;
            failedFiles << expectedTotalIijx;
        }
    }

    if (ok)
    {
        launchSolverExecutable(analysisNode, "洞室开挖");
    }
    else
    {
        clearSolverRunDirectory();
        qWarning() << "洞室开挖数据准备失败:" << failedFiles;
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("洞室开挖数据准备失败。请检查开挖步骤和区域选择后重试。"));
    }
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "洞室开挖计算失败:" << e.what();
    }
}

void Solver::onComputeSlopeStability(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (!beginCommonCompute(analysisNode, "边坡分析"))
        return;

    const QString runDir = SolverFilePaths::outputRoot(projectDir);
    QFile iautoFile(QDir(runDir).absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("边坡计算数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "8\n";
    }

    if (!forceIiinLayersToOne(runDir))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查网格"),
            QStringLiteral("当前网格不满足边坡计算要求。请检查网格设置后重试。"));
        clearSolverRunDirectory();
        return;
    }

    auto* slopePage = dynamic_cast<SlopeStabilityWidget*>(analysisNode->getPage());
    if (slopePage && slopePage->exportIslopDatFile())
    {
        launchSolverExecutable(analysisNode, "边坡分析");
    }
    else
    {
        clearSolverRunDirectory();
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("边坡计算数据准备失败。请检查设置后重试。"));
    }
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "边坡分析计算失败:" << e.what();
    }
}

bool Solver::normalizeGeoStressSolidLayers(QString* errorMessage) const
{
    const auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return fail(QStringLiteral("无法读取当前网格。请重新打开工程后重试。"));

    vtkSmartPointer<vtkUnstructuredGrid> grid = meshSvc->getVtkUnstructuredGridSolid();
    if (!grid || grid->GetNumberOfCells() <= 0)
        grid = meshSvc->getVtkUnstructuredGrid();
    if (!grid || grid->GetNumberOfCells() <= 0)
        return fail(QStringLiteral("当前工程没有可计算的实体网格。"));

    const int targetDim =
        PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const QFileInfo sourceInfo(meshSvc->sourceMeshFilePath());
    const bool keepSourceElementIds =
        sourceInfo.exists() &&
        sourceInfo.suffix().compare(QStringLiteral("dat"), Qt::CaseInsensitive) == 0;
    vtkDataArray* elemIdArr =
        vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

    QHash<int, int> layerByCalculationElement;
    layerByCalculationElement.reserve(int(grid->GetNumberOfCells()));
    int compatibilityElementId = std::numeric_limits<int>::max();

    int exportedElementIndex = 0;
    for (vtkIdType cellId = 0; cellId < grid->GetNumberOfCells(); ++cellId)
    {
        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        const int cellDim = cell->GetCellDimension();
        if (cellDim != targetDim && cellDim != 1)
            continue;

        ++exportedElementIndex;
        if (cellDim != targetDim)
            continue;

        const int sourceElementId =
            elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : int(cellId + 1);
        if (sourceElementId <= 0)
            continue;

        const int calculationElementId =
            keepSourceElementIds ? sourceElementId : exportedElementIndex;
        layerByCalculationElement.insert(calculationElementId, 1);
        compatibilityElementId = qMin(compatibilityElementId, calculationElementId);
    }
    if (layerByCalculationElement.isEmpty() ||
        compatibilityElementId == std::numeric_limits<int>::max())
        return fail(QStringLiteral("当前实体网格无法用于地应力回归。请检查网格后重试。"));

    // 旧求解器要求地应力输入至少包含一个待还原层；计算第 1 步会把 -1 还原为 1。
    // 这里只修改本次分析的临时输入，不改变项目网格及其分层设置。
    layerByCalculationElement[compatibilityElementId] = -1;

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    const QString iiinPath =
        QDir(SolverFilePaths::outputInputDirectory(projectDir))
            .filePath(QStringLiteral("IIIN.DAT"));
    if (!patchIiinElementLayers(iiinPath, layerByCalculationElement))
        return fail(QStringLiteral("当前网格的初始地层设置无法用于计算。请检查网格分层后重试。"));

    return true;
}

void Solver::onComputeGeoStressRegression(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    auto* page = analysisNode
                     ? dynamic_cast<GeoStressRegressionWidget*>(analysisNode->getPage())
                     : nullptr;
    if (!page)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("当前地应力回归设置未正确加载。请重新打开工程后再试。"));
        return;
    }

    int sideFaceCount = 0;
    int measurePointCount = 0;
    QStringList unsetSideFaces;
    QVector<GeoStressSideFacePage*> sidePages;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (!child || !child->getPage())
            continue;
        if (auto* side = dynamic_cast<GeoStressSideFacePage*>(child->getPage()))
        {
            ++sideFaceCount;
            sidePages.push_back(side);
            if (side->selectionCount() <= 0)
                unsetSideFaces << child->getName();
        }
        else if (auto* points = dynamic_cast<GeoStressMeasurePointPage*>(child->getPage()))
        {
            measurePointCount += points->points().size();
        }
    }

    const int minimumSideFaces = minimumGeoStressSideFaceCount();
    if (sideFaceCount < minimumSideFaces || sideFaceCount > 4)
    {
        const int dimension =
            PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查侧立面"),
            QStringLiteral("%1维地应力回归需要至少 %2 个侧立面，且最多允许 4 个。")
                .arg(dimension == 2 ? 2 : 3)
                .arg(minimumSideFaces));
        return;
    }
    if (!unsetSideFaces.isEmpty())
    {
        const QString objectName = minimumSideFaces == 1 ? QStringLiteral("网格边")
                                                          : QStringLiteral("网格面");
        QMessageBox::warning(nullptr, QStringLiteral("请完善侧立面"),
                             QStringLiteral("请为以下节点选择%1：\n%2")
                                 .arg(objectName, unsetSideFaces.join(QLatin1Char('\n'))));
        return;
    }
    if (page->calculationModeCode() != 0)
    {
        int verticalAxis = 2;
        if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            verticalAxis = meshSvc->verticalAxis();

        QStringList pressureErrors;
        for (GeoStressSideFacePage* side : std::as_const(sidePages))
        {
            QString error;
            if (side && !side->validatePressure(verticalAxis, &error))
                pressureErrors << error;
        }
        if (!pressureErrors.isEmpty())
        {
            QMessageBox::warning(nullptr, QStringLiteral("请检查侧立面参数"),
                                 pressureErrors.join(QLatin1Char('\n')));
            return;
        }
    }
    if (measurePointCount > 100)
    {
        QMessageBox::warning(nullptr, QStringLiteral("实测点数量已达上限"),
                             QStringLiteral("实测地应力点最多允许 100 个。"));
        return;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (!beginCommonCompute(analysisNode, "地应力回归分析"))
        return;

    QString initialLayerError;
    if (!normalizeGeoStressSolidLayers(&initialLayerError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请检查初始地层"),
                             initialLayerError);
        clearSolverRunDirectory();
        return;
    }

    QFile iautoFile(QDir(SolverFilePaths::outputRoot(projectDir))
                        .absoluteFilePath("MESG_IAUTO.DAT"));
    if (!iautoFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("地应力回归计算数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return;
    }
    {
        QTextStream os(&iautoFile);
        os << "6\n";
    }

    const GeoStressRegressionWidget::ExportPayload payload = buildGeoStressPayload(analysisNode);
    if (!payload.roofFileReady)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查地表边界"),
            QStringLiteral("地表边界设置无法用于计算，请重新检查所选网格边界。"));
        clearSolverRunDirectory();
        return;
    }
    if (payload.faceRows.size() != sideFaceCount)
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请检查侧立面"),
            QStringLiteral("侧立面设置无法用于计算，请重新检查所选网格边界。"));
        clearSolverRunDirectory();
        return;
    }
    if (page && page->calculationModeCode() == 1 && payload.measureRows.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请添加实测点"),
            QStringLiteral("已启用实测地应力回归，请至少添加一个实测点。"));
        clearSolverRunDirectory();
        return;
    }

    if (page && page->exportIGEOSDatFile(payload))
    {
        QDir previousGeoStressDir(
            QDir(SolverFilePaths::outputRoot(projectDir)).filePath(QStringLiteral("GEOSTR")));
        if (previousGeoStressDir.exists() && !previousGeoStressDir.removeRecursively())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("无法开始计算"),
                QStringLiteral("上一轮计算结果正在被其他程序使用。请关闭相关程序后重试。"));
            clearSolverRunDirectory();
            return;
        }
        launchSolverExecutable(analysisNode, "地应力回归分析");
    }
    else
    {
        clearSolverRunDirectory();
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("地应力回归数据准备失败。请检查设置后重试。"));
    }
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "地应力回归分析失败:" << e.what();
    }
}

void Solver::onComputeCaveBlockStability(ProjectNode* analysisNode)
{
    try
    {
    if (m_batchActive && !m_batchDispatching)
    {
        QMessageBox::information(nullptr, QStringLiteral("批量计算进行中"),
                                 QStringLiteral("请先完成或停止当前批量计算。"));
        return;
    }
    m_pendingComputeWidget = qobject_cast<QWidget*>(sender());
    if (!analysisNode || !analysisNode->getPage())
        return;

    auto* blockPage = dynamic_cast<CaveBlockStabilityWidget*>(analysisNode->getPage());
    if (!blockPage)
        return;
    QString blockError;
    if (!blockPage->validateInputs(&blockError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请完善块体稳定设置"), blockError);
        return;
    }

    QVector<CaveBlockPlanePage*> planePages;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (auto* planePage = dynamic_cast<CaveBlockPlanePage*>(child->getPage()))
            planePages.push_back(planePage);
    }

    if (planePages.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("请添加滑块面"),
            QStringLiteral("请至少添加一个滑块面后再计算。"));
        return;
    }
    for (int i = 0; i < planePages.size(); ++i)
    {
        QString planeError;
        if (!planePages[i]->validateInputs(&planeError))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善滑块面设置"),
                QStringLiteral("第 %1 个滑块面：%2").arg(i + 1).arg(planeError));
            return;
        }
    }

    if (blockPage->exportDatFiles(planePages))
    {
        launchCaveBlockSolverExecutable(analysisNode);
    }
    else
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("块体稳定计算数据准备失败。请检查滑块面和参数设置后重试。"));
    }
    catch (const std::exception& e)
    {
        qWarning() << "洞室块体稳定性计算失败:" << e.what();
    }
}


void Solver::connectStopRequest(QObject* page) const
{
    if (!page || page->metaObject()->indexOfSignal("stopRequested()") < 0)
        return;

    connect(page, SIGNAL(stopRequested()), this, SLOT(stopSolverExecutable()), Qt::UniqueConnection);
}

void Solver::setComputeWidgetRunning(QWidget* widget, bool computing) const
{
    if (!widget)
        return;

    QMetaObject::invokeMethod(widget, "setComputing", Qt::DirectConnection, Q_ARG(bool, computing));
}

void Solver::consumeSolverOutput()
{
    if (!m_solverProcess)
        return;

    const QString output =
        QString::fromLocal8Bit(m_solverProcess->readAllStandardOutput()).trimmed();
    if (output.isEmpty())
        return;

    qInfo().noquote() << ("__RAW__" + output);

    const QString error = solverErrorFromText(output);
    if (!error.isEmpty())
        m_solverFailureDetail = error;

    if (!m_solverFailureDetail.isEmpty() &&
        output.contains(QStringLiteral("PRESS ANY LETTER"), Qt::CaseInsensitive))
    {
        m_solverProcess->write("x\n");
        m_solverProcess->waitForBytesWritten(1000);
    }

    if (output.contains(QStringLiteral("结束进程")) && m_solverFailureDetail.isEmpty())
    {
        m_solverFailureDetail =
            solverErrorFromRunLogs(m_activeAnalysisWorkspace, m_solverStartedAtMs);
        if (m_solverFailureDetail.isEmpty())
        {
            const bool batchItemFinished = m_batchActive && !m_batchCurrentUuid.isEmpty();
            notifySolverFinished();
            finishSolverProcess(true);
            if (batchItemFinished)
            {
                completeBatchItem(true);
                QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
            }
        }
    }
}

void Solver::handleSolverProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (m_solverFailureDetail.isEmpty())
        m_solverFailureDetail =
            solverErrorFromRunLogs(m_activeAnalysisWorkspace, m_solverStartedAtMs);

    const bool succeeded = exitStatus == QProcess::NormalExit && exitCode == 0
                           && m_solverFailureDetail.isEmpty();
    QString failureReason;
    if (succeeded)
    {
        notifySolverFinished();

        if (m_activeAnalysisKind == QStringLiteral("SeepageAnalysis") && m_root)
        {
            for (ProjectNode* analysis : m_root->getChildren())
            {
                if (analysis &&
                    analysis->data(0, ProjectNode::RoleKind).toString()
                        == QStringLiteral("DamAnalysis"))
                    syncDamConditionalNodes(analysis);
            }
        }
    }
    else
    {
        writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                              m_activeAnalysisName, m_activeAnalysisKind,
                              m_activeAnalysisType, QStringLiteral("failed"));

        failureReason = m_solverFailureDetail;
        if (failureReason.isEmpty() && exitStatus == QProcess::CrashExit)
            failureReason = QStringLiteral("求解器进程异常退出");
        if (failureReason.isEmpty())
            failureReason = QStringLiteral("求解器退出码：%1").arg(exitCode);

        qWarning().noquote() << m_activeAnalysisType << "failed:" << failureReason
                             << "exitCode=" << exitCode
                             << "exitStatus=" << static_cast<int>(exitStatus);
        QMessageBox::warning(
            nullptr, QStringLiteral("计算未完成"),
            QStringLiteral("%1未完成。\n%2")
                .arg(m_activeAnalysisType, customerFacingSolverFailure(failureReason)));
    }

    const bool batchItemFinished = m_batchActive && !m_batchCurrentUuid.isEmpty();
    finishSolverProcess(false);
    if (batchItemFinished)
    {
        completeBatchItem(succeeded,
                          succeeded ? QString() : QStringLiteral("求解失败"));
        QTimer::singleShot(0, this, &Solver::startNextBatchCompute);
    }
}

void Solver::finishSolverProcess(bool terminateProcess)
{
    clearSolverRunDirectory();
    if (terminateProcess && !m_solverFinishedNotified && !m_activeAnalysisWorkspace.isEmpty())
        writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                              m_activeAnalysisName, m_activeAnalysisKind,
                              m_activeAnalysisType, QStringLiteral("stopped"));

    if (terminateProcess)
        terminateDeployedSolvers();

    if (m_solverProcess)
    {
        m_solverProcess->disconnect(this);
        if (terminateProcess && m_solverProcess->state() != QProcess::NotRunning)
        {
            m_solverProcess->kill();
            m_solverProcess->waitForFinished(3000);
        }
        m_solverProcess->deleteLater();
        m_solverProcess = nullptr;
    }

    setComputeWidgetRunning(m_activeComputeWidget, false);
    m_activeComputeWidget = nullptr;
    m_activeProjectDir.clear();
    m_activeAnalysisWorkspace.clear();
    m_activeAnalysisUuid.clear();
    m_activeAnalysisName.clear();
    m_activeAnalysisKind.clear();
    m_activeAnalysisType.clear();
    m_solverFailureDetail.clear();
    m_solverStartedAtMs = 0;
}

void Solver::clearSolverRunDirectory() const
{
    auto& variables = PublicVariables::instance();
    const QString key = QString::fromLatin1(PublicConstants::kSolverRunDirectoryKey);
    if (variables.ownerOf(key) == QStringLiteral("solver_runtime"))
        variables.remove(key, QStringLiteral("solver_runtime"));
}

ProjectNode* Solver::findChildByPageType(ProjectNode* parent, const QString& pageType) const
{
    if (!parent)
        return nullptr;
    for (ProjectNode* child : parent->getChildren())
    {
        if (child && child->getPage() && child->getPage()->getPageType() == pageType)
            return child;
    }
    return nullptr;
}

bool Solver::canDeleteNode(ProjectNode* node) const
{
    if (!node || node == m_root)
        return false;
    const QString kind = node->data(0, ProjectNode::RoleKind).toString();
    static const QSet<QString> fixedKinds = {
        QStringLiteral("SeepageBoundary"), QStringLiteral("StressBoundary"),
        QStringLiteral("CaveLiningInnerBoundary"),
        QStringLiteral("CaveLiningOuterBoundary"),
        QStringLiteral("CaveCraneBeamArea"),
        QStringLiteral("DamIceRegion"),
        QStringLiteral("DamDynamicAnalysis")};
    if (fixedKinds.contains(kind))
        return false;

    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
    if (!parent)
        return false;
    if (kind == QStringLiteral("FillStorageCycle"))
        return countFillStorageCycles(parent) > 1;
    if (kind == QStringLiteral("SeepageNaturalBoundary"))
        return countGeoStressChildren(
                   parent, QStringLiteral("SeepageBoundaryNaturalSeepage")) > 1;
    if (kind == QStringLiteral("CaveExcavationBody"))
        return countGeoStressChildren(parent, QStringLiteral("CaveExcavationBody")) > 1;
    if (kind == QStringLiteral("CaveBlockPlane"))
        return countGeoStressChildren(parent, QStringLiteral("CaveBlockPlane")) > 1;
    if (kind == QStringLiteral("GeoStressRegressionSideFace"))
        return countGeoStressChildren(parent, QStringLiteral("GeoStressRegressionSideFace"))
               > minimumGeoStressSideFaceCount();
    if (kind == QStringLiteral("GeoStressRegression"))
        return countGeoStressChildren(parent, QStringLiteral("GeoStressRegressionAnalysis")) > 1;
    if (kind == QStringLiteral("CaveSupportComputation"))
        return countGeoStressChildren(parent, QStringLiteral("CaveSupportComputation")) > 1;
    if (kind == QStringLiteral("GeoStressRegressionMeasurePoint"))
        return countGeoStressChildren(parent,
                                      QStringLiteral("GeoStressRegressionMeasurePoint")) > 1;
    if (kind == QStringLiteral("GeoStressRegressionOuterForce"))
        return countGeoStressChildren(parent,
                                      QStringLiteral("GeoStressRegressionOuterForce")) > 1;
    return true;
}

void Solver::updateBoundaryNodeStatus(ProjectNode* node, int selectionCount, bool enabled,
                                      const QString& disabledReason) const
{
    if (!node)
        return;
    node->setData(0, ProjectNode::RoleDimmed, !enabled);
    node->setForeground(0, enabled ? QBrush() : QBrush(QColor(QStringLiteral("#8a95a5"))));
    node->setIcon(0, QIcon());

    Qt::ItemFlags flags = node->flags();
    flags.setFlag(Qt::ItemIsEnabled, enabled);
    flags.setFlag(Qt::ItemIsSelectable, enabled);
    node->setFlags(flags);
    if (node->getPage() && node->getPage()->getWidget())
        node->getPage()->getWidget()->setEnabled(enabled);

    QString objectName = QStringLiteral("网格节点");
    if (dynamic_cast<StressBoundaryPage*>(node->getPage()))
    {
        const int dimension =
            PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
        objectName = dimension == 2 ? QStringLiteral("网格边") : QStringLiteral("网格面");
    }
    else if (dynamic_cast<CaveCraneBeamAreaPage*>(node->getPage()))
    {
        objectName = QStringLiteral("实体区域");
    }
    QStringList tooltip;
    const QString relationship = solverNodeRelationshipHint(node);
    if (!relationship.isEmpty())
        tooltip.push_back(relationship);
    if (!enabled)
        tooltip.push_back(disabledReason.isEmpty() ? QStringLiteral("未启用") : disabledReason);
    tooltip.push_back(QStringLiteral("选择对象：%1").arg(objectName));
    tooltip.push_back(selectionCount > 0 ? QStringLiteral("已选择 %1 个").arg(selectionCount)
                                         : QStringLiteral("未选择"));
    if (auto* seepageBoundary = dynamic_cast<SeepageBoundaryPage*>(node->getPage()))
    {
        if (seepageBoundary->hasHydraulicParameters())
        {
            if (seepageBoundary->usesWaterLevel())
                tooltip.push_back(QStringLiteral("水位：%1 m")
                                      .arg(seepageBoundary->waterLevel(), 0, 'g', 10));
            else if (seepageBoundary->usesFlux())
                tooltip.push_back(QStringLiteral("通量：%1 m/s")
                                      .arg(seepageBoundary->flux(), 0, 'g', 10));
        }
        if (seepageBoundary->getPageType()
            == QStringLiteral("SeepageBoundaryNaturalSeepage"))
        {
            const int correction = seepageBoundary->naturalHeadCorrection();
            tooltip.push_back(
                correction == 1 ? QStringLiteral("水头处理：低于上游水头时改为上游水头")
              : correction == 2 ? QStringLiteral("水头处理：低于下游水头时改为下游水头")
                                : QStringLiteral("水头处理：保持天然水头"));
        }
    }
    node->setToolTip(0, tooltip.join(QLatin1Char('\n')));
}

void Solver::connectBoundaryStatus(ProjectNode* node) const
{
    if (!node || !node->getPage())
        return;
    if (auto* page = dynamic_cast<SeepageBoundaryPage*>(node->getPage()))
    {
        connect(page, &SeepageBoundaryPage::selectionCountChanged, this,
                [this, node](int count)
                {
                    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
                    if (parent && parent->data(0, ProjectNode::RoleKind).toString() ==
                                      QStringLiteral("SeepageAnalysis"))
                        syncSeepageConditionalNodes(parent);
                    else if (parent && parent->data(0, ProjectNode::RoleKind).toString() ==
                                               QStringLiteral("DamAnalysis"))
                        syncDamConditionalNodes(parent);
                    else
                        updateBoundaryNodeStatus(node, count);
                });
        connect(page, &SeepageBoundaryPage::hydraulicParametersChanged, this,
                [this, node, page]()
                {
                    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
                    if (parent && parent->data(0, ProjectNode::RoleKind).toString() ==
                                      QStringLiteral("SeepageAnalysis"))
                        syncSeepageConditionalNodes(parent);
                    else
                        updateBoundaryNodeStatus(node, page->selectionCount());
                });
        updateBoundaryNodeStatus(node, page->selectionCount());
    }
    else if (auto* page = dynamic_cast<StressBoundaryPage*>(node->getPage()))
    {
        connect(page, &StressBoundaryPage::selectionCountChanged, this,
                [this, node](int count)
                {
                    auto* parent = dynamic_cast<ProjectNode*>(node->parent());
                    if (parent && parent->data(0, ProjectNode::RoleKind).toString() ==
                                      QStringLiteral("DamAnalysis"))
                        syncDamConditionalNodes(parent);
                    else if (parent && parent->data(0, ProjectNode::RoleKind).toString() ==
                                           QStringLiteral("CaveSupportComputation"))
                        syncCaveSupportConditionalNodes(parent);
                    else
                        updateBoundaryNodeStatus(node, count);
                });
        updateBoundaryNodeStatus(node, page->selectionCount());
    }
}

void Solver::syncSeepageConditionalNodes(ProjectNode* analysisNode) const
{
    auto* page = analysisNode ? dynamic_cast<SeepageWidget*>(analysisNode->getPage()) : nullptr;
    if (!page)
        return;

    bool upstreamAvailable = false;
    bool downstreamAvailable = false;
    double upstreamLevel = 0.0;
    double downstreamLevel = 0.0;

    // 天然渗流初始面的水头处理直接引用同一分析工况中的上下游水位。
    // 只有真正的水位边界且已经选择节点时，才作为可用参考值。
    for (ProjectNode* child : analysisNode->getChildren())
    {
        auto* boundary = child ? dynamic_cast<SeepageBoundaryPage*>(child->getPage()) : nullptr;
        if (!boundary || !boundary->usesWaterLevel() || boundary->selectionCount() <= 0)
            continue;
        if (boundary->getPageType() == QStringLiteral("SeepageBoundaryUpstreamWater"))
        {
            upstreamAvailable = true;
            upstreamLevel = boundary->waterLevel();
        }
        else if (boundary->getPageType() == QStringLiteral("SeepageBoundaryDownstreamWater"))
        {
            downstreamAvailable = true;
            downstreamLevel = boundary->waterLevel();
        }
    }

    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (auto* boundary = child ? dynamic_cast<SeepageBoundaryPage*>(child->getPage()) : nullptr)
        {
            bool enabled = true;
            QString reason;
            if (boundary->getPageType() == QStringLiteral("SeepageBoundaryNaturalSeepage"))
            {
                enabled = page->naturalFreeSurfaceEnabled();
                reason = QStringLiteral("天然渗流未启用");
                boundary->setNaturalReferenceWaterLevels(
                    upstreamAvailable, upstreamLevel,
                    downstreamAvailable, downstreamLevel);
            }
            else if (boundary->getPageType() == QStringLiteral("SeepageBoundaryDrainWell"))
            {
                enabled = page->drainWellEnabled();
                reason = QStringLiteral("排水井未启用");
            }
            updateBoundaryNodeStatus(child, boundary->selectionCount(), enabled, reason);
        }
    }
}

QVector<QPair<QString, QString>> Solver::seepageAnalysisSources() const
{
    QVector<QPair<QString, QString>> sources;
    if (!m_root)
        return sources;

    for (ProjectNode* node : m_root->getChildren())
    {
        if (!node ||
            node->data(0, ProjectNode::RoleKind).toString()
                != QStringLiteral("SeepageAnalysis"))
            continue;
        sources.push_back(qMakePair(node->getUuid(), node->text(0).trimmed()));
    }
    return sources;
}

ProjectNode* Solver::findAnalysisNodeByUuid(const QString& uuid) const
{
    if (!m_root || uuid.trimmed().isEmpty())
        return nullptr;

    for (ProjectNode* node : m_root->getChildren())
    {
        if (node && node->getUuid() == uuid)
            return node;
    }
    return nullptr;
}

bool Solver::prepareDamSeepageFiles(FillStorageCyclePage* cyclePage,
                                    int cycleIndex,
                                    const QString& projectDir,
                                    QString* errorMessage) const
{
    if (errorMessage)
        errorMessage->clear();
    if (!cyclePage)
        return false;

    cyclePage->clearPreparedSeepageFiles();
    if (!cyclePage->needsSeepageAssociation())
        return true;

    auto fail = [errorMessage](const QString& message)
    {
        if (errorMessage)
            *errorMessage = message;
        return false;
    };

    ProjectNode* sourceNode =
        findAnalysisNodeByUuid(cyclePage->seepageSourceUuid());
    if (!sourceNode ||
        sourceNode->data(0, ProjectNode::RoleKind).toString()
            != QStringLiteral("SeepageAnalysis"))
        return fail(QStringLiteral("关联的渗流工况不存在，请重新选择。"));

    const QString sourceWorkspace = analysisWorkspace(sourceNode);
    const QString steadyDir =
        seepageResultDirectory(sourceWorkspace, QStringLiteral("SEEPCASE01"));
    const QString transientDir =
        seepageResultDirectory(sourceWorkspace, QStringLiteral("SEEPDYNA01"));
    const bool needBody = cyclePage->usesImportedSeepageBody();
    // 均质坝的渗流体力由 FILLFBLOAD2 处理，它会同时读取 CAHEAD/CABODY。
    const bool needHead = needBody;

    QString headPath;
    QString bodyPath;
    QString description;

    if (cyclePage->seepageResultType() == QStringLiteral("steady"))
    {
        description = QStringLiteral("稳态结果");
        if (!steadyDir.isEmpty())
        {
            headPath = QDir(steadyDir).filePath(QStringLiteral("IIDISP.TXT"));
            bodyPath = QDir(steadyDir).filePath(QStringLiteral("INBODY.TXT"));
        }
    }
    else
    {
        const QString selectedStep = cyclePage->seepageTransientStep();
        if (selectedStep.isEmpty())
            return fail(QStringLiteral("请选择一个明确的非稳态计算时刻。"));

        description = cyclePage->seepageTransientResultLabel();
        if (description.isEmpty())
            description = QStringLiteral("所选非稳态计算时刻");

        if (selectedStep == QStringLiteral("final"))
        {
            if (!transientDir.isEmpty())
            {
                headPath =
                    QDir(transientDir).filePath(QStringLiteral("IIDISP.TXT"));
                bodyPath =
                    QDir(transientDir).filePath(QStringLiteral("INBODY.TXT"));
            }
        }
        else
        {
            bool stepOk = false;
            const int step = selectedStep.toInt(&stepOk);
            if (!stepOk || step < 0)
                return fail(QStringLiteral("所选非稳态计算时刻无效。"));

            if (!transientDir.isEmpty())
            {
                headPath = QDir(transientDir).filePath(
                    QStringLiteral("STEP%1_IIDISP.TXT").arg(step));
                bodyPath = QDir(transientDir).filePath(
                    QStringLiteral("STEP%1_INBODY.TXT").arg(step));
            }
        }
    }

    if (needHead && !QFileInfo::exists(headPath))
        return fail(QStringLiteral("%1缺少节点水头文件。").arg(description));
    if (needBody && !QFileInfo::exists(bodyPath))
        return fail(QStringLiteral("%1缺少节点体力文件。").arg(description));

    const QString currentIiin =
        QDir(SolverFilePaths::outputInputDirectory(projectDir))
            .filePath(QStringLiteral("IIIN.DAT"));
    const int currentNodeCount = readFirstLineInteger(currentIiin, 0);
    if (currentNodeCount <= 0)
        return fail(QStringLiteral("无法读取当前土石坝网格节点数。"));

    // IFILL.DAT 第一行的 DIR_DEFAULT 指向“计算输入”目录；
    // PACKIIWL/FILLFTSEEP2/FILLFBLOAD2 都从该目录读取 CAHEAD/CABODY。
    const QString inputDirectory =
        SolverFilePaths::outputInputDirectory(projectDir);
    if (!QDir().mkpath(inputDirectory))
        return fail(QStringLiteral("无法写入土石坝计算输入目录。"));

    QString preparedHeadFile;
    QString preparedBodyFile;
    if (needHead)
    {
        const int sourceNodeCount = seepageFieldNodeCount(headPath);
        if (sourceNodeCount != currentNodeCount)
        {
            return fail(
                QStringLiteral("关联工况“%1”的节点水头与当前网格不一致（%2 / %3 个节点）。")
                    .arg(sourceNode->text(0).trimmed())
                    .arg(sourceNodeCount)
                    .arg(currentNodeCount));
        }

        preparedHeadFile =
            QStringLiteral("SEEP_C%1_HEAD.TXT")
                .arg(cycleIndex, 2, 10, QLatin1Char('0'));
        if (!copyFileReplacing(
                headPath, QDir(inputDirectory).filePath(preparedHeadFile)))
            return fail(QStringLiteral("复制%1节点水头失败。").arg(description));
    }

    if (needBody)
    {
        const int sourceNodeCount = seepageFieldNodeCount(bodyPath);
        if (sourceNodeCount != currentNodeCount)
        {
            return fail(
                QStringLiteral("关联工况“%1”的节点体力与当前网格不一致（%2 / %3 个节点）。")
                    .arg(sourceNode->text(0).trimmed())
                    .arg(sourceNodeCount)
                    .arg(currentNodeCount));
        }

        preparedBodyFile =
            QStringLiteral("SEEP_C%1_BODY.TXT")
                .arg(cycleIndex, 2, 10, QLatin1Char('0'));
        if (!copyFileReplacing(
                bodyPath, QDir(inputDirectory).filePath(preparedBodyFile)))
            return fail(QStringLiteral("复制%1节点体力失败。").arg(description));
    }

    cyclePage->setPreparedSeepageFiles(
        preparedHeadFile, preparedBodyFile);
    return true;
}

void Solver::syncDamConditionalNodes(ProjectNode* analysisNode) const
{
    auto* page = analysisNode ? dynamic_cast<DamAnalysisWidget*>(analysisNode->getPage()) : nullptr;
    if (!page)
        return;

    const int calculationType = page->dynamicOnlyEnabled()
                                    ? 2
                                    : (page->dynamicAnalysisEnabled() ? 3 : 1);
    const bool staticEnabled = calculationType == 1 || calculationType == 3;
    const bool dynamicEnabled = calculationType == 2 || calculationType == 3;

    FillStorageCyclePage* fillPage = nullptr;
    ProjectNode* dynamicNode = nullptr;
    DamDynamicAnalysisPage* dynamicPage = nullptr;
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (!child || !child->getPage())
            continue;
        if (!fillPage)
        {
            if (auto* candidate = dynamic_cast<FillStorageCyclePage*>(child->getPage()))
            {
                fillPage = candidate;
                continue;
            }
        }
        if (!dynamicPage)
        {
            if (auto* candidate = dynamic_cast<DamDynamicAnalysisPage*>(child->getPage()))
            {
                dynamicPage = candidate;
                dynamicNode = child;
            }
        }
    }

    // 一个土石坝分析节点本身就是一个工况；两个兄弟子节点始终保留。
    // “填筑蓄水”在仅动力计算时仍用于查看/匹配既有静力状态；
    // “动力分析”在仅静力计算时参照“结冰区域”的交互方式置灰，而不是隐藏。
    if (dynamicNode)
    {
        dynamicNode->setData(0, ProjectNode::RoleDimmed, !dynamicEnabled);
        dynamicNode->setForeground(
            0, dynamicEnabled ? QBrush() : QBrush(QColor(QStringLiteral("#8a95a5"))));

        Qt::ItemFlags flags = dynamicNode->flags();
        flags.setFlag(Qt::ItemIsEnabled, dynamicEnabled);
        flags.setFlag(Qt::ItemIsSelectable, dynamicEnabled);
        dynamicNode->setFlags(flags);
        if (dynamicNode->getPage() && dynamicNode->getPage()->getWidget())
            dynamicNode->getPage()->getWidget()->setEnabled(dynamicEnabled);

        QStringList tooltip;
        const QString relationship = solverNodeRelationshipHint(dynamicNode);
        if (!relationship.isEmpty())
            tooltip.push_back(relationship);
        if (!dynamicEnabled)
            tooltip.push_back(QStringLiteral("当前为仅静力计算，动力分析不参与本次计算"));
        dynamicNode->setToolTip(0, tooltip.join(QLatin1Char('\n')));
    }

    if (dynamicPage)
    {
        dynamicPage->setAnalysisEnabled(dynamicEnabled);
        dynamicPage->setModalExtractionOrder(page->modalExtractionOrder());
    }

    const QVector<QPair<QString, QString>> seepageSources = seepageAnalysisSources();
    bool usesFace = false;
    if (fillPage)
    {
        fillPage->setAnalysisMode(calculationType);
        fillPage->setAvailableSeepageSources(seepageSources);
        if (dynamicPage)
            fillPage->applyDynamicAnalysisState(dynamicPage->solverState(), dynamicEnabled);
        else
            fillPage->applyDynamicAnalysisState(QJsonObject(), false);

        QVector<int> transientSteps;
        QMap<int, QString> transientLabels;
        bool hasTransientFinal = false;
        ProjectNode* sourceNode = findAnalysisNodeByUuid(fillPage->seepageSourceUuid());
        if (sourceNode && fillPage->needsSeepageAssociation())
        {
            const QString transientDir =
                seepageResultDirectory(analysisWorkspace(sourceNode), QStringLiteral("SEEPDYNA01"));
            const bool needHead = fillPage->usesImportedSeepageHead();
            const bool needBody = fillPage->usesImportedSeepageBody();
            if (!transientDir.isEmpty())
            {
                transientSteps = transientResultSteps(transientDir, needHead, needBody);
                transientLabels = transientResultLabels(
                    analysisWorkspace(sourceNode), transientDir, transientSteps);
                const QFileInfo finalHead(QDir(transientDir).filePath(QStringLiteral("IIDISP.TXT")));
                const QFileInfo finalBody(QDir(transientDir).filePath(QStringLiteral("INBODY.TXT")));
                hasTransientFinal =
                    (!needHead || (finalHead.isFile() && finalHead.size() > 0))
                    && (!needBody || (finalBody.isFile() && finalBody.size() > 0));
            }
        }
        if (fillPage->needsSeepageAssociation())
            fillPage->setAvailableTransientResults(
                transientSteps, hasTransientFinal, transientLabels);
        usesFace = staticEnabled && fillPage->usesDamFacePressure();
    }

    const bool usesModal = dynamicPage && dynamicPage->usesModalAnalysis();
    page->setModalAnalysisRequired(dynamicEnabled && usesModal);

    // 当前仅提供均质坝，坝基渗透荷载与折减参数均不参与计算。
    page->setUpliftReductionRequired(false);
    for (ProjectNode* child : analysisNode->getChildren())
    {
        if (auto* boundary = child ? dynamic_cast<StressBoundaryPage*>(child->getPage()) : nullptr)
        {
            const QString pageType = boundary->getPageType();
            const bool upstreamBoundary =
                pageType == QStringLiteral("StressBoundaryDamUpstream");
            const bool enabled = upstreamBoundary ? usesFace : false;

            QString reason;
            if (upstreamBoundary)
            {
                reason = !staticEnabled
                             ? QStringLiteral("当前为仅动力计算，坝体水面压力边界不参与本次计算")
                             : QStringLiteral("坝体水面压力未启用");
            }
            else
            {
                reason = QStringLiteral("均质坝不使用坝基扬压力边界");
            }

            // 土石坝工况的固定子节点始终保留在树中。
            // 条件不满足时仅置灰并禁用，不再通过隐藏/显示改变树结构。
            updateBoundaryNodeStatus(child, boundary->selectionCount(), enabled, reason);
        }
        else if (auto* region = child ? dynamic_cast<SeepageBoundaryPage*>(child->getPage()) : nullptr)
        {
            const bool enabled = region->getPageType() != QStringLiteral("DamIceRegion")
                                 || page->freezeThawEnabled();
            updateBoundaryNodeStatus(child, region->selectionCount(), enabled,
                                     QStringLiteral("结冰区域变形未启用"));
        }
    }
}

void Solver::syncCaveSupportConditionalNodes(ProjectNode* supportNode) const
{
    auto* page = supportNode
        ? dynamic_cast<CaveSupportComputationPage*>(supportNode->getPage()) : nullptr;
    if (!page)
        return;

    const bool liningEnabled = page->liningEnabled();
    const bool craneEnabled = page->craneBeamEnabled();
    for (ProjectNode* child : supportNode->getChildren())
    {
        if (!child || !child->getPage())
            continue;
        const QString type = child->getPage()->getPageType();
        if (auto* boundary = dynamic_cast<StressBoundaryPage*>(child->getPage()))
        {
            if (type == QStringLiteral("CaveLiningInnerBoundary")
                || type == QStringLiteral("CaveLiningOuterBoundary"))
            {
                updateBoundaryNodeStatus(
                    child, boundary->selectionCount(), liningEnabled,
                    QStringLiteral("衬砌内力计算未启用"));
            }
        }
        else if (auto* cranePage = dynamic_cast<CaveCraneBeamAreaPage*>(child->getPage()))
        {
            updateBoundaryNodeStatus(
                child, cranePage->selectionCount(), craneEnabled,
                QStringLiteral("吊车梁施工未启用"));
        }
    }
}

ProjectNode* Solver::topLevelAnalysisNode(ProjectNode* node) const
{
    ProjectNode* current = node;
    while (current && current != m_root)
    {
        auto* parent = dynamic_cast<ProjectNode*>(current->parent());
        if (parent == m_root)
            return current;
        current = parent;
    }
    return nullptr;
}

QString Solver::analysisWorkspace(ProjectNode* node) const
{
    ProjectNode* owner = topLevelAnalysisNode(node);
    if (!owner)
        return {};

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString().trimmed();
    const QString analysisUuid = owner->getUuid().trimmed();
    const QString key = workspaceKey(analysisUuid);
    if (projectDir.isEmpty() || analysisUuid.isEmpty() || key.isEmpty())
        return {};

    const QDir project(QDir(projectDir).absolutePath());
    const QString rootPath = project.absolutePath();
    const QString baseName = safeWorkspaceName(owner->text(0));
    QString desiredPath = QDir(rootPath).filePath(baseName);

    if (QDir(desiredPath).exists())
    {
        const QString existingUuid = workspaceManifestUuid(desiredPath);
        if (existingUuid.isEmpty() || existingUuid == analysisUuid)
        {
            migrateWorkspaceManifestName(desiredPath);
            removeCopiedProjectMetadata(desiredPath);
            return QDir(desiredPath).absolutePath();
        }
        int suffix = 2;
        do
        {
            desiredPath = QDir(rootPath).filePath(
                QStringLiteral("%1(%2)").arg(baseName).arg(suffix++));
        } while (QDir(desiredPath).exists() &&
                 workspaceManifestUuid(desiredPath) != analysisUuid);
    }

    QString existingPath;
    const QDir currentRoot(rootPath);
    if (currentRoot.exists())
    {
        const QFileInfoList candidates = currentRoot.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& candidate : candidates)
        {
            if (workspaceManifestUuid(candidate.absoluteFilePath()) == analysisUuid)
            {
                existingPath = candidate.absoluteFilePath();
                break;
            }
        }
    }

    const QStringList legacyRootNames = {
        QString::fromUtf8(kLegacyNamedAnalysisWorkspaceRoot),
        QString::fromLatin1(kLegacyAnalysisWorkspaceRoot)};
    for (const QString& legacyRootName : legacyRootNames)
    {
        if (!existingPath.isEmpty())
            break;
        const QString legacyRootPath = project.filePath(legacyRootName);
        const QDir legacyRoot(legacyRootPath);
        const QStringList directNames = {key, baseName};
        for (const QString& directName : directNames)
        {
            const QString directLegacyPath = legacyRoot.filePath(directName);
            if (QDir(directLegacyPath).exists() &&
                workspaceManifestUuid(directLegacyPath) == analysisUuid)
            {
                existingPath = directLegacyPath;
                break;
            }
        }
        if (!existingPath.isEmpty())
            break;

        const QFileInfoList candidates = legacyRoot.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo& candidate : candidates)
        {
            if (workspaceManifestUuid(candidate.absoluteFilePath()) == analysisUuid)
            {
                existingPath = candidate.absoluteFilePath();
                break;
            }
        }
    }

    if (!existingPath.isEmpty())
    {
        if (QDir().mkpath(rootPath) &&
            QDir::cleanPath(existingPath).compare(QDir::cleanPath(desiredPath),
                                                  Qt::CaseInsensitive) != 0 &&
            QDir().rename(existingPath, desiredPath))
            existingPath = desiredPath;

        migrateWorkspaceManifestName(existingPath);
        removeCopiedProjectMetadata(existingPath);
        for (const QString& legacyRootName : legacyRootNames)
        {
            QDir legacyRoot(project.filePath(legacyRootName));
            if (legacyRoot.exists() &&
                legacyRoot.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty())
                project.rmdir(legacyRootName);
        }
        return QDir(existingPath).absolutePath();
    }

    return QDir(desiredPath).absolutePath();
}

bool Solver::prepareAnalysisWorkspace(ProjectNode* node, bool caveBlock, QString* workspaceDir,
                                      QString* runDir) const
{
    ProjectNode* owner = topLevelAnalysisNode(node);
    const QString workspacePath = analysisWorkspace(owner);
    const QString projectPath =
        QDir(PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString())
            .absolutePath();
    if (!owner || workspacePath.isEmpty() || !QDir(projectPath).exists() ||
        !QDir().mkpath(workspacePath))
        return false;

    if (caveBlock)
    {
        const QString source = QDir(projectPath).filePath(QStringLiteral("SliderCase"));
        const QString target = QDir(workspacePath).filePath(QStringLiteral("SliderCase"));
        if (!QDir(source).exists() || !replaceDirectoryTree(source, target))
            return false;
        if (runDir)
            *runDir = QDir(target).absolutePath();
    }
    else
    {
        // Refresh staged inputs while preserving this analysis node's existing result directories.
        QDir workspace(workspacePath);
        const QString manifestName =
            QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName);
        const QString legacyManifestName =
            QString::fromLatin1(kLegacyAnalysisManifestFileName);
        const QFileInfoList staleRootFiles = workspace.entryInfoList(QDir::Files);
        for (const QFileInfo& file : staleRootFiles)
        {
            if (file.fileName().compare(manifestName, Qt::CaseInsensitive) != 0 &&
                file.fileName().compare(legacyManifestName, Qt::CaseInsensitive) != 0 &&
                !QFile::remove(file.absoluteFilePath()))
                return false;
        }

        const QDir project(projectPath);
        const QString sourceInput = SolverFilePaths::projectInputDirectory(projectPath);
        const QString targetInput = SolverFilePaths::analysisInputDirectory(workspacePath);
        if (!QDir(sourceInput).exists() || !replaceDirectoryTree(sourceInput, targetInput))
            return false;

        const QString ownerKind = owner->data(0, ProjectNode::RoleKind).toString();
        const QString sourceGeoStress = project.filePath(QStringLiteral("GEOSTR"));
        const QString targetGeoStress = workspace.filePath(QStringLiteral("GEOSTR"));
        if (ownerKind == QStringLiteral("CaveExcavationAnalysis") &&
            !QDir(targetGeoStress).exists() && QDir(sourceGeoStress).exists() &&
            !copyDirectoryTree(sourceGeoStress, targetGeoStress))
            return false;

        if (runDir)
            *runDir = workspace.absolutePath();
    }

    if (workspaceDir)
        *workspaceDir = QDir(workspacePath).absolutePath();
    return true;
}

bool Solver::writeAnalysisManifest(const QString& workspaceDir, const QString& analysisUuid,
                                   const QString& analysisName, const QString& analysisKind,
                                   const QString& analysisType, const QString& state) const
{
    if (workspaceDir.isEmpty() || !QDir().mkpath(workspaceDir))
        return false;

    const QString manifestPath = QDir(workspaceDir).filePath(
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName));
    const QString legacyManifestPath = QDir(workspaceDir).filePath(
        QString::fromLatin1(kLegacyAnalysisManifestFileName));
    QJsonObject root;
    QFile existing(QFileInfo::exists(manifestPath) ? manifestPath : legacyManifestPath);
    if (existing.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        const QJsonDocument document = QJsonDocument::fromJson(existing.readAll());
        if (document.isObject())
            root = document.object();
        existing.close();
    }

    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("analysisUuid"), analysisUuid);
    root.insert(QStringLiteral("analysisName"), analysisName);
    root.insert(QStringLiteral("analysisKind"), analysisKind);
    root.insert(QStringLiteral("lastAnalysisType"), analysisType);
    root.insert(QStringLiteral("state"), state);
    root.insert(QStringLiteral("updatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    if (state == QStringLiteral("completed"))
    {
        QJsonArray completedTypes = root.value(QStringLiteral("completedTypes")).toArray();
        bool found = false;
        for (const QJsonValue& value : completedTypes)
            found = found || value.toString() == analysisType;
        if (!found)
            completedTypes.append(analysisType);
        root.insert(QStringLiteral("completedTypes"), completedTypes);
        root.insert(QStringLiteral("completedAt"),
                    QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    }

    QSaveFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    const bool committed = file.commit();
    if (committed && QFileInfo::exists(legacyManifestPath))
        QFile::remove(legacyManifestPath);
    return committed;
}

void Solver::updateAnalysisManifestName(ProjectNode* node) const
{
    ProjectNode* owner = topLevelAnalysisNode(node);
    if (!owner || owner != node)
        return;

    const QString workspacePath = analysisWorkspace(owner);
    if (workspacePath.isEmpty())
        return;
    const QString manifestPath = QDir(workspacePath).filePath(
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName));
    const QString legacyManifestPath = QDir(workspacePath).filePath(
        QString::fromLatin1(kLegacyAnalysisManifestFileName));
    QFile existing(QFileInfo::exists(manifestPath) ? manifestPath : legacyManifestPath);
    if (!existing.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QJsonDocument document = QJsonDocument::fromJson(existing.readAll());
    existing.close();
    if (!document.isObject())
        return;
    QJsonObject root = document.object();
    root.insert(QStringLiteral("analysisName"), owner->text(0).trimmed());
    root.insert(QStringLiteral("analysisKind"),
                owner->data(0, ProjectNode::RoleKind).toString());
    root.insert(QStringLiteral("updatedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));

    QSaveFile file(manifestPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (file.commit() && QFileInfo::exists(legacyManifestPath))
        QFile::remove(legacyManifestPath);
}

void Solver::notifySolverFinished()
{
    if (m_solverFinishedNotified)
        return;

    m_solverFinishedNotified = true;

    if (!writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                               m_activeAnalysisName, m_activeAnalysisKind,
                               m_activeAnalysisType, QStringLiteral("completed")))
        qWarning() << "failed to update analysis result manifest";

    QVariantMap data;
    data.insert(QStringLiteral("analysisType"), m_activeAnalysisType);
    data.insert(QStringLiteral("analysisUuid"), m_activeAnalysisUuid);
    data.insert(QStringLiteral("analysisName"), m_activeAnalysisName);
    data.insert(QStringLiteral("analysisKind"), m_activeAnalysisKind);
    data.insert(QStringLiteral("projectDir"), QDir(m_activeProjectDir).absolutePath());
    data.insert(QStringLiteral("resultRoot"), QDir(m_activeAnalysisWorkspace).absolutePath());
    EventManager::instance().sendEvent(PublicConstants::kEventSolverFinished, data);
}

void Solver::stopSolverExecutable()
{
    if (m_batchActive)
    {
        if (m_root && !m_batchCurrentUuid.isEmpty())
        {
            if (auto* page = dynamic_cast<SolverSettingWidget*>(m_root->getPage()))
                page->setBatchItemStatus(m_batchCurrentUuid, QStringLiteral("已停止"));
        }
        m_batchQueue.clear();
        finishSolverProcess(true);
        finishBatchCompute(true);
        return;
    }
    finishSolverProcess(true);
}

bool Solver::beginCommonCompute(
    ProjectNode* analysisNode, const QString& analysisType,
    const QMap<int, int>& damDynamicSourceMappings)
{
    try
    {
    clearSolverRunDirectory();
    if (!analysisNode)
        return false;

    if (!analysisNode->getPage())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("%1设置未正确加载。请重新打开工程后再试。").arg(analysisType));
        return false;
    }

    if (m_solverProcess)
    {
        if (QMessageBox::question(nullptr, QStringLiteral("已有计算正在进行"),
                                  QStringLiteral("是否停止当前计算并开始“%1”？")
                                      .arg(analysisType),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return false;
        finishSolverProcess(true);
    }
    else
        terminateDeployedSolvers();

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    ProjectNode* owner = topLevelAnalysisNode(analysisNode);
    QString workspaceDir;
    QString runDir;
    if (!owner || !prepareAnalysisWorkspace(analysisNode, false, &workspaceDir, &runDir) ||
        !writeAnalysisManifest(workspaceDir, owner->getUuid(), owner->text(0).trimmed(),
                               owner->data(0, ProjectNode::RoleKind).toString(), analysisType,
                               QStringLiteral("preparing")) ||
        !PublicVariables::instance().set(PublicConstants::kSolverRunDirectoryKey, runDir,
                                         QStringLiteral("solver_runtime")))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("无法准备%1数据。请先保存工程，并确认工程所在位置可以正常写入。")
                .arg(analysisType));
        return false;
    }
    const auto fail = [this, owner, workspaceDir, analysisType]() -> bool
    {
        writeAnalysisManifest(workspaceDir, owner->getUuid(), owner->text(0).trimmed(),
                              owner->data(0, ProjectNode::RoleKind).toString(), analysisType,
                              QStringLiteral("failed"));
        clearSolverRunDirectory();
        return false;
    };

    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return fail();

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (meshSvc)
    {
        const QString targetIiin = inputDir.filePath(QStringLiteral("IIIN.DAT"));
        const QString sourceMeshPath = meshSvc->sourceMeshFilePath();
        const QFileInfo sourceInfo(sourceMeshPath);
        const bool sourceIsDat = sourceInfo.exists() && sourceInfo.suffix().compare("dat", Qt::CaseInsensitive) == 0;

        if (sourceIsDat)
        {
            // DAT 导入时已经保存公共副本，这里只在分析副本缺失时兜底复制。
            if (!QFile::exists(targetIiin) && !QFile::copy(sourceMeshPath, targetIiin))
            {
                QMessageBox::warning(
                    nullptr, QStringLiteral("请检查网格"),
                    QStringLiteral("%1缺少可用网格。请重新导入或重新生成网格后重试。")
                        .arg(analysisType));
                return fail();
            }
        }
        else
        {
            QString iiinErr;
            if (!meshSvc->exportIiinDatFile(targetIiin, &iiinErr))
            {
                qWarning().noquote() << analysisType << "网格计算数据生成失败:" << iiinErr;
                QMessageBox::warning(
                    nullptr, QStringLiteral("请检查网格"),
                    QStringLiteral("%1无法使用当前网格。请检查网格后重试。")
                        .arg(analysisType));
                return fail();
            }
        }
    }

    auto* physicalFieldService = ServiceRegistry::instance().getService<PhysicalFieldService>();
    if (physicalFieldService)
    {
        physicalFieldService->generateIIIDFile();
        physicalFieldService->generateIIWLFile();
    }

    auto* materialManagerService = ServiceRegistry::instance().getService<MaterialManagerService>();
    if (materialManagerService &&
        !materialManagerService->generateIMATFile(damDynamicSourceMappings))
    {
        if (materialManagerService->hasConfiguredMaterials())
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请检查材料设置"),
                QStringLiteral("当前材料设置无法用于计算。请根据材料页面中的提示修正后重试。"));
            return fail();
        }
        const QFileInfo existingImat(inputDir.filePath(QStringLiteral("IMAT.DAT")));
        if (!existingImat.exists() || existingImat.size() <= 0)
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("请完善材料设置"),
                QStringLiteral("当前工程还没有可用于计算的材料参数。"));
            return fail();
        }
        QMessageBox::warning(
            nullptr, QStringLiteral("材料设置未更新"),
            QStringLiteral("部分材料设置暂未更新，本次计算将使用上次保存的材料参数。"));
    }

    QString materialError;
    if (!validateSolverMaterials(inputDir.filePath(QStringLiteral("IIIN.DAT")),
                                 inputDir.filePath(QStringLiteral("IMAT.DAT")),
                                 &materialError))
    {
        QMessageBox::warning(nullptr, QStringLiteral("请检查材料设置"), materialError);
        return fail();
    }

    QStringList failedInputs;
    if (!exportInouseDatFile())
        failedInputs << QStringLiteral("模型区域设置");
    if (!exportMaMaskDatFile())
        failedInputs << QStringLiteral("材料分区设置");
    if (!exportKetraceDatFile() || !exportJdtraceDatFile())
        failedInputs << QStringLiteral("结果跟踪设置");
    if (!failedInputs.isEmpty())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("无法开始计算"),
            QStringLiteral("以下设置未能用于计算：\n%1\n请检查后重试。")
                .arg(failedInputs.join(QLatin1Char('\n'))));
        return fail();
    }
    return true;
    }
    catch (const std::exception& e)
    {
        clearSolverRunDirectory();
        qWarning() << "beginCommonCompute 失败:" << e.what();
        return false;
    }
}


bool Solver::launchCaveBlockSolverExecutable(ProjectNode* analysisNode)
{
    SolverRuntimeOptions runtimeOptions = solverRuntimeOptions();
    runtimeOptions.useGpu = false;
    runtimeOptions.deviceCode = 0;
    runtimeOptions.executableRelativePath = QStringLiteral("solver/PCCAE_CPU/PCCAE_CPU.exe");
    const QString exePath = deployedSolverExecutable(runtimeOptions);
    if (!QFileInfo::exists(exePath))
    {
        qWarning() << "solver executable missing:" << exePath;
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("计算程序安装不完整。请修复或重新安装软件。"));
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();

    if (m_solverProcess)
    {
        if (QMessageBox::question(nullptr, QStringLiteral("已有计算正在进行"),
                                  QStringLiteral("是否停止当前计算并开始“%1”？")
                                      .arg(QStringLiteral("块体稳定分析")),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return false;
        finishSolverProcess(true);
    }
    else
        terminateDeployedSolvers();

    ProjectNode* owner = topLevelAnalysisNode(analysisNode);
    QString workspaceDir;
    QString sliderDir;
    if (!owner || !prepareAnalysisWorkspace(analysisNode, true, &workspaceDir, &sliderDir))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("当前分析的数据准备失败。请保存工程后重试。"));
        return false;
    }

    if (!updateCaveBlockIiiDirDat(sliderDir))
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("块体稳定计算准备失败。请保存工程后重试。"));
        return false;
    }

    m_activeComputeWidget = m_pendingComputeWidget;
    m_pendingComputeWidget = nullptr;
    m_solverFinishedNotified = false;
    m_activeProjectDir = QDir(projectDir).absolutePath();
    m_activeAnalysisWorkspace = workspaceDir;
    m_activeAnalysisUuid = owner->getUuid();
    m_activeAnalysisName = owner->text(0).trimmed();
    m_activeAnalysisKind = owner->data(0, ProjectNode::RoleKind).toString();
    m_activeAnalysisType = QStringLiteral("块体稳定分析");
    m_solverFailureDetail.clear();
    m_solverStartedAtMs = QDateTime::currentMSecsSinceEpoch();
    writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                          m_activeAnalysisName, m_activeAnalysisKind,
                          m_activeAnalysisType, QStringLiteral("running"));

    m_solverProcess = new QProcess(this);
    // 滑块计算仍使用 PCCAE_CPU.exe；运行前把 D:/IIIDIR.DAT 指向 SliderCase
    // 并写入滑块需要的 6 工况，使求解器进入滑块流程而不是 MESG_IAUTO 自动分发流程。
    m_solverProcess->setWorkingDirectory(QDir(sliderDir).absolutePath());
    m_solverProcess->setProgram(exePath);
    m_solverProcess->setArguments(QStringList());
    m_solverProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_solverProcess, &QProcess::readyReadStandardOutput,
            this, &Solver::consumeSolverOutput);

    connect(m_solverProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error != QProcess::UnknownError && m_solverProcess
                    && m_solverFailureDetail.isEmpty())
                    m_solverFailureDetail = m_solverProcess->errorString();
            });

    connect(m_solverProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                handleSolverProcessFinished(exitCode, exitStatus);
            });

    m_solverProcess->start();
    if (!m_solverProcess->waitForStarted(5000))
    {
        qWarning() << "块体稳定计算程序启动失败:" << m_solverProcess->errorString();
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("块体稳定计算程序无法启动。请检查软件安装后重试。"));

        writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                              m_activeAnalysisName, m_activeAnalysisKind,
                              m_activeAnalysisType, QStringLiteral("failed"));
        finishSolverProcess(false);
        return false;
    }

    setComputeWidgetRunning(m_activeComputeWidget, true);
    return true;
}


bool Solver::launchSolverExecutable(ProjectNode* analysisNode, const QString& analysisType)
{
    const SolverRuntimeOptions runtimeOptions = solverRuntimeOptions();
    const QString exePath = deployedSolverExecutable(runtimeOptions);
    if (!QFileInfo::exists(exePath))
    {
        qWarning() << "solver executable missing:" << exePath;
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("%1所需的%2计算程序未安装完整。请修复或重新安装软件。")
                .arg(analysisType,
                     runtimeOptions.useGpu ? QStringLiteral("GPU")
                                           : QStringLiteral("CPU")));
        clearSolverRunDirectory();
        return false;
    }

    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (m_solverProcess)
    {
        if (QMessageBox::question(nullptr, QStringLiteral("已有计算正在进行"),
                                  QStringLiteral("是否停止当前计算并开始“%1”？")
                                      .arg(analysisType),
                                  QMessageBox::Yes | QMessageBox::No,
                                  QMessageBox::No) != QMessageBox::Yes)
            return false;
        finishSolverProcess(true);
    }
    else
        terminateDeployedSolvers();

    ProjectNode* owner = topLevelAnalysisNode(analysisNode);
    const QString runDir = SolverFilePaths::runDirectory();
    const QString workspaceDir = runDir;
    if (!owner || runDir.isEmpty() || !QDir(runDir).exists() ||
        workspaceManifestUuid(runDir) != owner->getUuid())
    {
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("当前分析的数据准备失败。请保存工程后重试。"));
        clearSolverRunDirectory();
        return false;
    }

    const int solverMode = solverModeFromAnalysisType(analysisType);
    if (solverMode > 0)
    {
        if (!updateIiiDirDat(runDir, solverMode, runtimeOptions.deviceCode))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("计算程序未启动"),
                QStringLiteral("%1计算准备失败。请保存工程后重试。").arg(analysisType));
            clearSolverRunDirectory();
            return false;
        }

        int verticalAxis = 2;
        if (auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>())
            verticalAxis = meshSvc->verticalAxis();

        if (!updatePrepDatFiles(runDir, solverMode, verticalAxis))
        {
            QMessageBox::warning(
                nullptr, QStringLiteral("计算程序未启动"),
                QStringLiteral("%1无法使用当前网格。请检查网格后重试。").arg(analysisType));
            clearSolverRunDirectory();
            return false;
        }
    }

    m_activeComputeWidget = m_pendingComputeWidget;
    m_pendingComputeWidget = nullptr;
    m_solverFinishedNotified = false;
    m_activeProjectDir = QDir(projectDir).absolutePath();
    m_activeAnalysisWorkspace = workspaceDir;
    m_activeAnalysisUuid = owner->getUuid();
    m_activeAnalysisName = owner->text(0).trimmed();
    m_activeAnalysisKind = owner->data(0, ProjectNode::RoleKind).toString();
    m_activeAnalysisType = analysisType;
    m_solverFailureDetail.clear();
    m_solverStartedAtMs = QDateTime::currentMSecsSinceEpoch();
    writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                          m_activeAnalysisName, m_activeAnalysisKind,
                          m_activeAnalysisType, QStringLiteral("running"));

    m_solverProcess = new QProcess(this);
    qInfo().noquote()
        << QStringLiteral("启动%1求解器：%2；IDEVICE=%3")
               .arg(runtimeOptions.useGpu ? QStringLiteral("GPU") : QStringLiteral("CPU"),
                    exePath)
               .arg(runtimeOptions.deviceCode);
    m_solverProcess->setWorkingDirectory(QDir(runDir).absolutePath());
    m_solverProcess->setProgram(exePath);
    m_solverProcess->setArguments(QStringList());
    m_solverProcess->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_solverProcess, &QProcess::readyReadStandardOutput,
            this, &Solver::consumeSolverOutput);

    connect(m_solverProcess, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
                if (error != QProcess::UnknownError && m_solverProcess
                    && m_solverFailureDetail.isEmpty())
                    m_solverFailureDetail = m_solverProcess->errorString();
            });

    connect(m_solverProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                handleSolverProcessFinished(exitCode, exitStatus);
            });

    m_solverProcess->start();
    if (!m_solverProcess->waitForStarted(5000))
    {
        qWarning() << analysisType << "计算程序启动失败:" << m_solverProcess->errorString();
        QMessageBox::warning(
            nullptr, QStringLiteral("计算程序未启动"),
            QStringLiteral("%1计算程序无法启动。请检查软件安装后重试。")
                .arg(analysisType));

        writeAnalysisManifest(m_activeAnalysisWorkspace, m_activeAnalysisUuid,
                              m_activeAnalysisName, m_activeAnalysisKind,
                              m_activeAnalysisType, QStringLiteral("failed"));
        finishSolverProcess(false);
        return false;
    }

    setComputeWidgetRunning(m_activeComputeWidget, true);
    clearSolverRunDirectory();
    return true;
}

int Solver::solverModeFromAnalysisType(const QString& analysisType) const
{
    if (analysisType.contains("渗流") || analysisType.contains("土石坝"))
        return 8;
    if (analysisType.contains("洞室") ||
        analysisType.contains("地应力") ||
        analysisType.contains("开挖")) {
        return 6;
    }
    if (analysisType.contains("边坡"))
        return 7;
    return 0;
}


bool Solver::updateCaveBlockIiiDirDat(const QString& sliderDir) const
{
    QFile file("D:/IIIDIR.DAT");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 9)
        return false;

    QString nativeSliderDir = QDir::toNativeSeparators(QDir(sliderDir).absolutePath());
    if (!nativeSliderDir.endsWith('\\'))
        nativeSliderDir += "\\";

    lines[0] = QString("'%1'").arg(nativeSliderDir);
    lines[4] = QString("%1  IDEVICE").arg(0, 9);
    lines[5] = QString("%1  ILOG_GZKZR").arg(0, 9);
    lines[6] = QString("%1  ILOG_SOLVE").arg(0, 9);

    bool updatedModeLine = false;
    for (int i = lines.size() - 1; i >= 1; --i)
    {
        const QString trimmed = lines[i].trimmed();
        const bool quotedPath = trimmed.startsWith("'") && trimmed.endsWith("'") &&
                                (trimmed.contains('\\') || trimmed.contains('/'));
        if (quotedPath)
        {
            lines[i] = QString("'%1'").arg(nativeSliderDir);
            if (i + 1 < lines.size())
            {
                bool ok = false;
                lines[i + 1].trimmed().toInt(&ok);
                if (ok)
                {
                    lines[i + 1] = QString("%1   ").arg(6, 12);
                    updatedModeLine = true;
                }
            }
            break;
        }
    }

    if (!updatedModeLine)
        return false;

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::updateIiiDirDat(const QString& projectDir, int solverMode, int deviceCode) const
{
    QFile file("D:/IIIDIR.DAT");
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    QString projectWithSlash = QDir::toNativeSeparators(QDir(projectDir).absolutePath());
    if (!projectWithSlash.endsWith(QDir::separator()))
        projectWithSlash += QDir::separator();
    QString inputDir = QDir::toNativeSeparators(
        SolverFilePaths::analysisInputDirectory(projectDir));
    if (!inputDir.endsWith(QDir::separator()))
        inputDir += QDir::separator();

    lines[0] = QString("'%1'").arg(projectWithSlash);
    if (lines.size() < 9)
        return false;
    lines[4] = QString("%1  IDEVICE").arg(qBound(0, deviceCode, 2), 9);
    lines[5] = QString("%1  ILOG_GZKZR").arg(0, 9);
    lines[6] = QString("%1  ILOG_SOLVE").arg(0, 9);
    lines[7] = QString("'%1'").arg(inputDir);
    lines[8] = QString("%1   ").arg(solverMode, 12);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::updatePrepDatFiles(const QString& projectDir, int solverMode, int verticalAxis) const
{
    QFile file(QDir(SolverFilePaths::analysisInputDirectory(projectDir))
                   .filePath(QStringLiteral("IIIN.DAT")));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();
    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto integerMatches = [](const QString& line)
    {
        QVector<QRegularExpressionMatch> matches;
        QRegularExpressionMatchIterator it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());
        return matches;
    };

    const QVector<QRegularExpressionMatch> header = integerMatches(lines.at(0));
    if (header.size() < 4)
        return false;
    const int numnp = header.at(0).captured().toInt();
    const int madim = header.at(1).captured().toInt();
    const int nelem = header.at(2).captured().toInt();
    const int manod = header.at(3).captured().toInt();
    const int firstElemLine = 2 + numnp;
    if (numnp < 0 || nelem < 0 || manod <= 0 || firstElemLine < 0 ||
        lines.size() < firstElemLine + nelem)
        return false;

    // The legacy reinforcement interpolation always reads four nodes in 2D.
    // Older project snapshots padded three-node triangles with node 0, which
    // becomes a negative displacement index in MODDIS. Use a degenerate fourth
    // node, matching the solver's original sample files.
    if (madim == 2 && manod > 3)
    {
        const int ketypeTokenIndex = manod + 1;
        for (int i = 0; i < nelem; ++i)
        {
            QString& line = lines[firstElemLine + i];
            const QVector<QRegularExpressionMatch> matches = integerMatches(line);
            if (matches.size() <= ketypeTokenIndex)
                return false;
            if (matches.at(ketypeTokenIndex).captured().toInt() != 31)
                continue;

            const QString repeatedNode = matches.at(3).captured();
            for (int tokenIndex = manod; tokenIndex >= 4; --tokenIndex)
            {
                const QRegularExpressionMatch& nodeMatch = matches.at(tokenIndex);
                if (nodeMatch.captured().toInt() != 0)
                    continue;
                QString replacement = repeatedNode;
                if (nodeMatch.capturedLength() > replacement.size())
                    replacement = replacement.rightJustified(nodeMatch.capturedLength(),
                                                              QLatin1Char(' '));
                line.replace(nodeMatch.capturedStart(), nodeMatch.capturedLength(), replacement);
            }
        }
    }

    const int safeVerticalAxis = (verticalAxis >= 1 && verticalAxis <= 3) ? verticalAxis : 2;
    const QRegularExpression headerRe(R"(^\s*\d+\s+\d+)");
    const QRegularExpressionMatch match = headerRe.match(lines[1]);
    if (match.hasMatch())
    {
        const QString replacement = QString("%1%2").arg(solverMode, 8).arg(safeVerticalAxis, 8);
        lines[1].replace(0, match.capturedLength(0), replacement);
    }
    else
    {
        lines[1].replace(QRegularExpression(R"(^\s*\d+)"), QString("%8").arg(solverMode));
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::forceIiinLayersToOne(const QString& analysisDir) const
{
    QFile file(QDir(SolverFilePaths::analysisInputDirectory(analysisDir))
                   .filePath(QStringLiteral("IIIN.DAT")));
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QString content = QString::fromLocal8Bit(file.readAll());
    file.close();

    QStringList lines = content.split(QRegularExpression("\\r?\\n"), Qt::KeepEmptyParts);
    if (lines.size() < 2)
        return false;

    static const QRegularExpression intRe(QStringLiteral("[-+]?\\d+"));
    auto intsInLine = [](const QString& line) -> QVector<int>
    {
        QVector<int> values;
        QRegularExpressionMatchIterator it = intRe.globalMatch(line);
        while (it.hasNext())
        {
            bool ok = false;
            const int value = it.next().captured(0).toInt(&ok);
            if (ok)
                values.push_back(value);
            if (values.size() >= 5)
                break;
        }
        return values;
    };

    const QVector<int> header = intsInLine(lines.at(0));
    if (header.size() < 4)
        return false;

    const int numnp = header.at(0);
    const int nelem = header.at(2);
    const int manod = header.at(3);
    if (numnp < 0 || nelem < 0 || manod <= 0)
        return false;

    const int firstElemLine = 2 + numnp;
    if (firstElemLine < 0 || lines.size() < firstElemLine + nelem)
        return false;

    const int layerTokenIndex = manod + 3; // elem id + manod nodes + KETYPE + MATYPE + LAYER
    for (int i = 0; i < nelem; ++i)
    {
        QString& line = lines[firstElemLine + i];

        QVector<QRegularExpressionMatch> matches;
        QRegularExpressionMatchIterator it = intRe.globalMatch(line);
        while (it.hasNext())
            matches.push_back(it.next());

        if (matches.size() <= layerTokenIndex)
            return false;

        const QRegularExpressionMatch& layerMatch = matches.at(layerTokenIndex);
        QString replacement = QStringLiteral("1");
        if (layerMatch.capturedLength(0) > replacement.size())
            replacement = replacement.rightJustified(layerMatch.capturedLength(0), QLatin1Char(' '));
        line.replace(layerMatch.capturedStart(0), layerMatch.capturedLength(0), replacement);
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(lines.join("\n").toLocal8Bit());
    return true;
}

bool Solver::exportInouseDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(QStringLiteral("INOUSE.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    const int configuredNdim =
        qMax(2, PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt());

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    auto grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : vtkSmartPointer<vtkUnstructuredGrid>{};
    if (!grid || grid->GetNumberOfPoints() <= 0)
    {
        os << "           0    NUM OF NO USE NODE\n";
        os << "           0    NUM OF NO USE ELEMENT\n";
        os << "       0    rod-num\n";
        os << QString("%1%2    NEBAR  NDIM\n").arg(0, 12).arg(configuredNdim, 12);
        return true;
    }

    const vtkIdType nPts = grid->GetNumberOfPoints();
    const vtkIdType nCells = grid->GetNumberOfCells();
    int meshMaxDim = 0;
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell)
            continue;
        meshMaxDim = qMax(meshMaxDim, cell->GetCellDimension());
    }
    const int ndim = qMax(2, qMin(configuredNdim, meshMaxDim > 0 ? meshMaxDim : configuredNdim));

    // IIIN 只导出当前研究维度的实体单元和一维杆单元。INOUSE 中所有单元号
    // 必须使用同一导出顺序，不能直接使用包含边界面单元的 VTK cellId + 1。
    QHash<vtkIdType, int> calculationElementByCell;
    QVector<vtkIdType> cellByCalculationElement(1, -1);
    calculationElementByCell.reserve(static_cast<int>(nCells));
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell || (cell->GetCellDimension() != ndim && cell->GetCellDimension() != 1))
            continue;
        const int calculationElement = cellByCalculationElement.size();
        calculationElementByCell.insert(cid, calculationElement);
        cellByCalculationElement.push_back(cid);
    }

    const auto cellOfCalculationElement = [&](int calculationElement) -> vtkCell*
    {
        if (calculationElement <= 0 || calculationElement >= cellByCalculationElement.size())
            return nullptr;
        return grid->GetCell(cellByCalculationElement.at(calculationElement));
    };

    QSet<vtkIdType> usedPointIds;
    usedPointIds.reserve(static_cast<int>(nPts));
    QVector<int> noUseElems;
    noUseElems.reserve(static_cast<int>(nCells / 10 + 1));

    QVector<QVector<int>> pointToSolidElems(static_cast<int>(nPts));
    QVector<vtkIdType> solidCellIds;
    solidCellIds.reserve(static_cast<int>(nCells));

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell || !calculationElementByCell.contains(cid))
            continue;
        const int calculationElement = calculationElementByCell.value(cid);
        vtkIdList* ptIds = cell->GetPointIds();
        const vtkIdType np = ptIds ? ptIds->GetNumberOfIds() : 0;
        if (np <= 0)
        {
            noUseElems.push_back(calculationElement);
            continue;
        }

        const bool isSolid = (cell->GetCellDimension() >= ndim);
        if (isSolid)
            solidCellIds.push_back(cid);
        for (vtkIdType i = 0; i < np; ++i)
        {
            const vtkIdType pid = ptIds->GetId(i);
            usedPointIds.insert(pid);
            if (isSolid && pid >= 0 && pid < nPts)
            {
                pointToSolidElems[static_cast<int>(pid)].push_back(calculationElement);
            }
        }
    }

    vtkSmartPointer<vtkUnstructuredGrid> solidGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    solidGrid->SetPoints(grid->GetPoints());
    solidGrid->Allocate(static_cast<vtkIdType>(solidCellIds.size()));
    QVector<int> solidLocalToGlobal;
    solidLocalToGlobal.reserve(solidCellIds.size());
    for (vtkIdType sourceCellId : std::as_const(solidCellIds))
    {
        vtkCell* solid = grid->GetCell(sourceCellId);
        vtkIdList* pointIds = solid ? solid->GetPointIds() : nullptr;
        const vtkIdType pointCount = pointIds ? pointIds->GetNumberOfIds() : 0;
        if (!solid || !pointIds || pointCount <= 0)
            continue;

        vtkIdType insertedCellId = -1;
        if (solid->GetCellType() == VTK_POLYHEDRON)
        {
            // vtkPolyhedron 不能只复制 GetPointIds()：多面体的拓扑还依赖独立的 face stream。
            // 若使用普通 InsertNextCell(type, pointIds) 重建，会丢失面连接信息，并可能在
            // VTK 9.2 内部访问无效的多面体面数据。这里完整复制“点 + 面流”。
            vtkIdType faceCount = 0;
            const vtkIdType* faceStream = nullptr;
            grid->GetFaceStream(sourceCellId, faceCount, faceStream);
            if (faceCount > 0 && faceStream)
            {
                insertedCellId = solidGrid->InsertNextCell(
                    VTK_POLYHEDRON,
                    pointCount,
                    pointIds->GetPointer(0),
                    faceCount,
                    faceStream);
            }
            else
            {
                qWarning() << "INOUSE: polyhedron cell has no valid face stream, source cell id ="
                           << sourceCellId;
            }
        }
        else
        {
            insertedCellId = solidGrid->InsertNextCell(solid->GetCellType(), pointIds);
        }

        // solidLocalToGlobal 必须和 solidGrid 的本地 cellId 严格一一对应。
        // 插入失败时不追加映射，避免 locator 返回的本地编号映射到错误计算单元。
        if (insertedCellId >= 0)
            solidLocalToGlobal.push_back(calculationElementByCell.value(sourceCellId));
    }
    auto solidLocator = vtkSmartPointer<vtkStaticCellLocator>::New();
    if (solidGrid->GetNumberOfCells() > 0)
    {
        solidLocator->SetDataSet(solidGrid);
        solidLocator->BuildLocator();
    }

    QVector<int> noUseNodes;
    noUseNodes.reserve(static_cast<int>(nPts / 10 + 1));
    for (vtkIdType pid = 0; pid < nPts; ++pid)
    {
        if (!usedPointIds.contains(pid))
            noUseNodes.push_back(static_cast<int>(pid + 1));
    }

    struct BarRow
    {
        int ke = 0;
        int ke1 = 0;
        int ke2 = 0;
        int lx = 4;
        QVector<double> rst1;
        QVector<double> rst2;
    };
    QVector<BarRow> barRows;
    QVector<int> suspendNodeIds;

    auto findContainingSolid = [&](const QVector<int>& candidates, const double xyz[3]) -> QVector<int>
    {
        QVector<int> inside;
        inside.reserve(candidates.size());
        for (int ke : candidates)
        {
            vtkCell* solid = cellOfCalculationElement(ke);
            if (!solid)
                continue;
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = 0.0;
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            const int evalOk = solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId,
                                                       pcoords.data(), dist2, weights.data());
            constexpr double kInsideTol2 = 1.0e-14;
            if (evalOk == 1 || dist2 <= kInsideTol2)
                inside.push_back(ke);
        }
        return inside;
    };

    auto pickRelatedSolid = [&](vtkIdType pid, const double xyzSelf[3], const double xyzOther[3]) -> int
    {
        auto distanceToCell = [&](int ke, const double xyz[3]) -> double
        {
            vtkCell* solid = cellOfCalculationElement(ke);
            if (!solid)
                return std::numeric_limits<double>::max();
            std::array<double, 3> closestPt{0.0, 0.0, 0.0};
            std::array<double, 3> pcoords{0.0, 0.0, 0.0};
            int subId = 0;
            double dist2 = std::numeric_limits<double>::max();
            const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
            std::vector<double> weights(static_cast<size_t>(wn), 0.0);
            solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                    weights.data());
            return dist2;
        };

        QVector<int> candidates;
        QVector<int> related;
        if (pid >= 0 && pid < nPts)
            related = pointToSolidElems[static_cast<int>(pid)];
        candidates = related;
        if (candidates.isEmpty())
        {
            if (solidLocator && solidGrid->GetNumberOfCells() > 0)
            {
                const vtkIdType localCell = solidLocator->FindCell(const_cast<double*>(xyzSelf));
                if (localCell >= 0 && localCell < solidLocalToGlobal.size())
                    return solidLocalToGlobal[static_cast<int>(localCell)];
            }
            return 0;
        }
        if (candidates.isEmpty())
            return 0;

        QVector<int> selfInside = findContainingSolid(candidates, xyzSelf);
        if (selfInside.isEmpty() && !related.isEmpty())
            selfInside = related;
        if (selfInside.isEmpty())
            return 0;
        if (selfInside.size() == 1)
            return selfInside.front();

        double bestDist2 = std::numeric_limits<double>::max();
        int bestKe = selfInside.front();
        for (int ke : selfInside)
        {
            const double dist2 = distanceToCell(ke, xyzOther);
            if (dist2 < bestDist2)
            {
                bestDist2 = dist2;
                bestKe = ke;
            }
        }
        return bestKe;
    };

    auto evaluateRst = [&](int ke, const double xyz[3], QVector<double>& rst)
    {
        rst.fill(0.0, ndim);
        if (ke <= 0)
            return;
        vtkCell* solid = cellOfCalculationElement(ke);
        if (!solid)
            return;

        std::array<double, 3> closestPt{0.0, 0.0, 0.0};
        std::array<double, 3> pcoords{0.0, 0.0, 0.0};
        int subId = 0;
        double dist2 = 0.0;
        const vtkIdType wn = (solid->GetNumberOfPoints() > 0) ? solid->GetNumberOfPoints() : 1;
        std::vector<double> weights(static_cast<size_t>(wn), 0.0);
        solid->EvaluatePosition(const_cast<double*>(xyz), closestPt.data(), subId, pcoords.data(), dist2,
                                weights.data());
        for (int d = 0; d < ndim && d < 3; ++d)
            rst[d] = 2.0 * pcoords[d] - 1.0;
    };

    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        vtkCell* cell = grid->GetCell(cid);
        if (!cell || cell->GetCellDimension() != 1 ||
            !calculationElementByCell.contains(cid))
            continue;
        vtkIdList* ptIds = cell->GetPointIds();
        if (!ptIds || ptIds->GetNumberOfIds() < 2)
            continue;

        const vtkIdType p0 = ptIds->GetId(0);
        const vtkIdType p1 = ptIds->GetId(1);
        if (p0 < 0 || p0 >= nPts || p1 < 0 || p1 >= nPts)
            continue;

        const auto& rel0 = pointToSolidElems[static_cast<int>(p0)];
        const auto& rel1 = pointToSolidElems[static_cast<int>(p1)];
        const bool s0 = rel0.isEmpty();
        const bool s1 = rel1.isEmpty();

        double xyz0[3] = {0.0, 0.0, 0.0};
        double xyz1[3] = {0.0, 0.0, 0.0};
        grid->GetPoint(p0, xyz0);
        grid->GetPoint(p1, xyz1);

        BarRow row;
        row.ke = calculationElementByCell.value(cid);
        row.ke1 = pickRelatedSolid(p0, xyz0, xyz1);
        row.ke2 = pickRelatedSolid(p1, xyz1, xyz0);
        if (!s0 && !s1)
            row.lx = 1;
        else if (s0 && !s1)
            row.lx = 2;
        else if (!s0 && s1)
            row.lx = 3;
        else
            row.lx = 4;
        evaluateRst(row.ke1, xyz0, row.rst1);
        evaluateRst(row.ke2, xyz1, row.rst2);
        barRows.push_back(row);

        if (s0)
            suspendNodeIds.push_back(static_cast<int>(p0 + 1));
        if (s1)
            suspendNodeIds.push_back(static_cast<int>(p1 + 1));
    }

    std::sort(noUseNodes.begin(), noUseNodes.end());
    std::sort(noUseElems.begin(), noUseElems.end());
    std::sort(suspendNodeIds.begin(), suspendNodeIds.end());
    suspendNodeIds.erase(std::unique(suspendNodeIds.begin(), suspendNodeIds.end()), suspendNodeIds.end());

    os << QString("%1    NUM OF NO USE NODE\n").arg(noUseNodes.size(), 12);
    for (int i = 0; i < noUseNodes.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseNodes.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    NUM OF NO USE ELEMENT\n").arg(noUseElems.size(), 12);
    for (int i = 0; i < noUseElems.size(); ++i)
    {
        os << QString("%1%2\n").arg(noUseElems.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1    杆单元悬浮节点数\n").arg(suspendNodeIds.size(), 8);
    for (int i = 0; i < suspendNodeIds.size(); ++i)
    {
        os << QString("%1%2\n").arg(suspendNodeIds.at(i), 9).arg(i + 1, 10);
    }

    os << QString("%1%2    NEBAR  NDIM\n").arg(barRows.size(), 12).arg(ndim, 12);
    for (int i = 0; i < barRows.size(); ++i)
    {
        const auto& r = barRows.at(i);
        os << QString("%1%2%3%4").arg(r.ke, 7).arg(r.ke1, 8).arg(r.ke2, 8).arg(r.lx, 8);
        for (double v : r.rst1)
            os << QString("%1").arg(v, 16, 'E', 6);
        for (double v : r.rst2)
            os << QString("%1").arg(v, 16, 'E', 6);
        os << QString("%1\n").arg(i + 1, 8);
    }
    return true;
}

bool Solver::exportMaMaskDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

           // 共同的文件内容
    const QString fileContent = "   0   NUM\n";

           // 1. 生成 MA_MASK.DAT
    QFile file(inputDir.filePath(QStringLiteral("MA_MASK.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream os(&file);
    os << fileContent;
    file.close(); // 显式关闭文件是个好习惯

           // 2. 生成 MA_MASK_CMM.DAT
    QFile fileCmm(inputDir.filePath(QStringLiteral("MA_MASK_CMM.DAT")));
    if (!fileCmm.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream osCmm(&fileCmm);
    osCmm << fileContent;
    fileCmm.close();

    return true;
}
bool Solver::exportKetraceDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(QStringLiteral("KETRACE.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}

bool Solver::exportJdtraceDatFile()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QDir inputDir(SolverFilePaths::outputInputDirectory(projectDir));
    if (!QDir().mkpath(inputDir.absolutePath()))
        return false;

    QFile file(inputDir.filePath(QStringLiteral("JDTRACE.DAT")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream os(&file);
    os << "           0   NUM\n";
    return true;
}
