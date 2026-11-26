#include "PostTecplotUtils.h"
#include "PostResultAutoPrepare.h"

#include "PostResultGenerator.h"
#include <PublicConstants.h>

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace
{
constexpr char kLegacyPostResultRoot[] = "post";
constexpr char kManifestName[] = "manifest.json";
constexpr char kLegacyAnalysisManifestFileName[] = "analysis.json";
// v28: 规范化退化三棱柱/四面体/金字塔单元的外法线朝向，消除特征线提取的内部伪轮廓。
constexpr int kManifestVersion = 28;

bool resultPackageExists(const QFileInfo& info)
{
    if (!info.exists() || !info.isFile() || info.size() <= 0)
        return false;
    if (info.suffix().compare(QStringLiteral("vtm"), Qt::CaseInsensitive) != 0)
        return true;
    return QFileInfo(info.dir().filePath(info.completeBaseName())).isDir();
}

struct GeneratedFileInfo
{
    QString displayName;
    QString kind;
    QString frameKey;
    QString frameLabel;
    double frameValue = 0.0;
    QString frameUnit;
    int frameOrder = -1;
};

struct AnalysisIdentity
{
    QString uuid;
    QString name;
    QString kind;
    QString analysisType;
    QString workspacePath;
};

struct RecentlyFreshManifest
{
    qint64 size = -1;
    qint64 modifiedMs = -1;
    qint64 validUntilMs = 0;
};

QMutex& preparationMutex()
{
    // 结果生成会删除/重写 prepared 目录中的 Tecplot 与 manifest。
    // UI 查询与求解完成后的后台准备必须串行，绝不能同时改同一套缓存。
    static QMutex mutex;
    return mutex;
}

QMutex& recentFreshMutex()
{
    static QMutex mutex;
    return mutex;
}

QHash<QString, RecentlyFreshManifest>& recentFreshManifests()
{
    static QHash<QString, RecentlyFreshManifest> cache;
    return cache;
}

QString manifestCacheKey(const QDir& preparedDir)
{
    return QDir::fromNativeSeparators(
               preparedDir.filePath(QString::fromLatin1(kManifestName)))
        .toCaseFolded();
}

bool isRecentlyValidatedFresh(const QDir& preparedDir)
{
    const QFileInfo info(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!info.exists() || !info.isFile())
        return false;

    const QString key = manifestCacheKey(preparedDir);
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker locker(&recentFreshMutex());
    const auto it = recentFreshManifests().constFind(key);
    return it != recentFreshManifests().constEnd() &&
           it->validUntilMs >= now &&
           it->size == info.size() &&
           it->modifiedMs == info.lastModified().toMSecsSinceEpoch();
}

void markRecentlyValidatedFresh(const QDir& preparedDir)
{
    const QFileInfo info(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!info.exists() || !info.isFile())
        return;

    RecentlyFreshManifest entry;
    entry.size = info.size();
    entry.modifiedMs = info.lastModified().toMSecsSinceEpoch();
    entry.validUntilMs = QDateTime::currentMSecsSinceEpoch() + 300;

    QMutexLocker locker(&recentFreshMutex());
    recentFreshManifests().insert(manifestCacheKey(preparedDir), entry);
}

void clearRecentlyValidatedFresh(const QDir& preparedDir)
{
    QMutexLocker locker(&recentFreshMutex());
    recentFreshManifests().remove(manifestCacheKey(preparedDir));
}

QString normalizeRelativePath(QString path)
{
    path = QDir::fromNativeSeparators(path.trimmed());
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

QString sourceRelativePath(const QDir& projectDir, const QString& sourceDir)
{
    const QString relative = normalizeRelativePath(
        projectDir.relativeFilePath(QDir(sourceDir).absolutePath()));
    return relative.startsWith(QStringLiteral("..")) ? QString() : relative;
}

AnalysisIdentity analysisIdentityForSource(const QDir& projectDir, const QDir& sourceDir)
{
    const QStringList manifestNames = {
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName),
        QString::fromLatin1(kLegacyAnalysisManifestFileName)};

    const QString projectPath = QDir::fromNativeSeparators(
        projectDir.absolutePath()).toCaseFolded();
    QString currentPath = QDir(sourceDir.absolutePath()).absolutePath();
    while (!currentPath.isEmpty())
    {
        const QString folded = QDir::fromNativeSeparators(currentPath).toCaseFolded();
        if (folded != projectPath && !folded.startsWith(projectPath + QLatin1Char('/')))
            break;

        const QDir workspace(currentPath);
        for (const QString& manifestName : manifestNames)
        {
            QFile file(workspace.filePath(manifestName));
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
                continue;

            const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
            if (!document.isObject())
                continue;

            const QJsonObject object = document.object();
            AnalysisIdentity identity;
            identity.uuid = object.value(QStringLiteral("analysisUuid")).toString().trimmed();
            identity.name = object.value(QStringLiteral("analysisName")).toString().trimmed();
            identity.kind = object.value(QStringLiteral("analysisKind")).toString().trimmed();
            identity.analysisType =
                object.value(QStringLiteral("lastAnalysisType")).toString().trimmed();
            if (identity.analysisType.isEmpty())
                identity.analysisType =
                    object.value(QStringLiteral("analysisType")).toString().trimmed();
            identity.workspacePath = workspace.absolutePath();
            return identity;
        }

        if (folded == projectPath)
            break;
        QDir parent(currentPath);
        if (!parent.cdUp())
            break;
        const QString parentPath = parent.absolutePath();
        if (parentPath.compare(currentPath, Qt::CaseInsensitive) == 0)
            break;
        currentPath = parentPath;
    }
    return {};
}

QDir generationProjectDir(const QDir& projectDir, const QDir& sourceDir)
{
    const AnalysisIdentity identity = analysisIdentityForSource(projectDir, sourceDir);
    return identity.workspacePath.isEmpty() ? projectDir : QDir(identity.workspacePath);
}

QString cacheDirName(const QString& relativePath)
{
    QString name = normalizeRelativePath(relativePath);
    name.replace(QLatin1Char('/'), QStringLiteral("__"));
    name.replace(QLatin1Char('\\'), QStringLiteral("__"));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("root") : name;
}

bool pathIsWithin(const QString& rootPath, const QString& candidatePath)
{
    const QString root = QDir::fromNativeSeparators(
        QDir(rootPath).absolutePath()).toCaseFolded();
    const QString candidate = QDir::fromNativeSeparators(
        QDir(candidatePath).absolutePath()).toCaseFolded();
    return candidate == root || candidate.startsWith(root + QLatin1Char('/'));
}

QDir postRootDirForSource(const QDir& projectDir, const QDir& sourceDir)
{
    const AnalysisIdentity identity = analysisIdentityForSource(projectDir, sourceDir);
    const QDir owner(identity.workspacePath.isEmpty() ? projectDir.absolutePath()
                                                      : identity.workspacePath);
    return QDir(owner.filePath(
        QString::fromUtf8(PublicConstants::kPostResultDirectoryName)));
}

bool isPreparedResultPath(const QDir& projectDir, const QString& path)
{
    const QString legacyPostRoot = projectDir.filePath(
        QString::fromLatin1(kLegacyPostResultRoot));
    if (pathIsWithin(legacyPostRoot, path))
        return true;

    const AnalysisIdentity identity = analysisIdentityForSource(projectDir, QDir(path));
    if (identity.workspacePath.isEmpty())
        return false;
    const QString resultRoot = QDir(identity.workspacePath).filePath(
        QString::fromUtf8(PublicConstants::kPostResultDirectoryName));
    return pathIsWithin(resultRoot, path);
}

QStringList analysisWorkspaceDirectories(const QDir& projectDir)
{
    QStringList result;
    QSet<QString> seen;
    const QStringList manifestNames = {
        QString::fromUtf8(PublicConstants::kSolverAnalysisManifestFileName),
        QString::fromLatin1(kLegacyAnalysisManifestFileName)};
    for (const QString& manifestName : manifestNames)
    {
        QDirIterator iterator(projectDir.absolutePath(), {manifestName}, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString workspacePath = QFileInfo(iterator.next()).absolutePath();
            if (isPreparedResultPath(projectDir, workspacePath))
                continue;
            const QString key = QDir::fromNativeSeparators(workspacePath).toCaseFolded();
            if (!seen.contains(key))
            {
                seen.insert(key);
                result.push_back(workspacePath);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const QString& left, const QString& right)
    {
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool findFileInfo(const QDir& dir, const QString& name, QFileInfo* out = nullptr)
{
    const QFileInfo exact(dir.filePath(name));
    if (exact.exists() && exact.isFile())
    {
        if (out)
            *out = exact;
        return true;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::Files);
    for (const QFileInfo& info : entries)
    {
        if (QString::compare(info.fileName(), name, Qt::CaseInsensitive) == 0)
        {
            if (out)
                *out = info;
            return true;
        }
    }
    return false;
}

bool hasMeshFile(const QString& absolutePath)
{
    const QDir dir(absolutePath);
    return findFileInfo(dir, QStringLiteral("IIIN.TXT")) ||
           findFileInfo(dir, QStringLiteral("IIIN.DAT"));
}

bool isSupportedFieldName(const QString& fileName)
{
    // Keep this list aligned with PostResultGenerator::generate().  Otherwise a
    // directory containing only a plastic-strain or failure-zone result is skipped,
    // and changes to those files do not invalidate the prepared-result cache.
    static const QRegularExpression expression(
        QStringLiteral(
            "^(?:STEP\\d+[-_])?"
            "(?:INVELO|IIDISP|IIVELO|IIACCE|IIP1P3|MODPRI|MODRAI_PLS|MODRAP_PLS|NODCMM|IIICMM)"
            "(?:[-_](?:METHOD\\d+|CZ[^.]*|DYNA|L\\d+))?\\.TXT$"),
        QRegularExpression::CaseInsensitiveOption);
    return expression.match(fileName).hasMatch();
}

bool hasSupportedFieldData(const QDir& dir)
{
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    return std::any_of(files.cbegin(), files.cend(), [](const QFileInfo& info)
    {
        return isSupportedFieldName(info.fileName());
    });
}

bool hasFile(const QDir& dir, const QString& name)
{
    return findFileInfo(dir, name);
}

bool hasFilePrefix(const QDir& dir, const QString& prefix)
{
    const QFileInfoList files = dir.entryInfoList(QDir::Files);
    return std::any_of(files.cbegin(), files.cend(), [&](const QFileInfo& info)
    {
        return info.fileName().startsWith(prefix, Qt::CaseInsensitive);
    });
}

bool usesStrengthReduction(const QDir& sourceDir)
{
    // NASGE uses the CZ/CB filename pair for both ordinary/overload steps and
    // strength-reduction steps.  IKEY.LX_CUBEI is the solver's discriminator.
    QFileInfo keyInfo;
    if (!findFileInfo(sourceDir, QStringLiteral("IKEY.DAT"), &keyInfo) &&
        !findFileInfo(sourceDir, QStringLiteral("IKEY.TXT"), &keyInfo))
        return false;

    QFile file(keyInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd())
    {
        const QString header = in.readLine();
        if (!header.contains(QStringLiteral("LX_CUBEI"), Qt::CaseInsensitive))
            continue;

        while (!in.atEnd())
        {
            const QString valuesLine = in.readLine().trimmed();
            if (valuesLine.isEmpty())
                continue;
            const QStringList values = valuesLine.simplified().split(
                QLatin1Char(' '), Qt::SkipEmptyParts);
            bool ok = false;
            const int mode = values.value(2).toInt(&ok);
            return ok && mode != 0;
        }
    }
    return false;
}

void appendUnique(QStringList& paths, QSet<QString>& seen, const QString& relativePath)
{
    const QString normalized = normalizeRelativePath(relativePath);
    const QString key = normalized.toLower();
    if (normalized.isEmpty() || seen.contains(key))
        return;
    seen.insert(key);
    paths.push_back(normalized);
}

QList<QFileInfo> sourceFiles(const QDir& sourceDir)
{
    QList<QFileInfo> files;
    const QFileInfoList entries = sourceDir.entryInfoList(QDir::Files);
    for (const QFileInfo& info : entries)
    {
        const QString name = info.fileName();
        if (isSupportedFieldName(name) ||
            name.compare(QStringLiteral("IIIN.TXT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("IIIN.DAT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("MAFILESET.DAT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("MA_MASK.DAT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("MA_MASK_CMM.DAT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("IKEY.DAT"), Qt::CaseInsensitive) == 0 ||
            name.compare(QStringLiteral("IKEY.TXT"), Qt::CaseInsensitive) == 0 ||
            (name.startsWith(QStringLiteral("ZONE"), Qt::CaseInsensitive) &&
             name.endsWith(QStringLiteral(".DAT"), Qt::CaseInsensitive)))
            files.push_back(info);
    }
    return files;
}

QList<QFileInfo> generationInputFiles(const QDir& projectDir, const QDir& sourceDir)
{
    QList<QFileInfo> files = sourceFiles(sourceDir);
    const QDir inputProject = generationProjectDir(projectDir, sourceDir);
    const QStringList inputDirectories = {
        inputProject.filePath(QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)),
        inputProject.filePath(QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)),
        inputProject.filePath(QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName))};
    for (const QString& inputDirectory : inputDirectories)
    {
        const QDir inputDir(inputDirectory);
        if (!inputDir.exists())
            continue;
        const QFileInfoList inputFiles = inputDir.entryInfoList(QDir::Files);
        for (const QFileInfo& info : inputFiles)
        {
            const QString name = info.fileName();
            if (name.compare(QStringLiteral("IIIN.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("MAFILESET.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("MA_MASK.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("MA_MASK_CMM.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("HWDROP_UP.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("HWDROP_DW.DAT"), Qt::CaseInsensitive) == 0 ||
                (name.startsWith(QStringLiteral("ZONE"), Qt::CaseInsensitive) &&
                 name.endsWith(QStringLiteral(".DAT"), Qt::CaseInsensitive)))
                files.push_back(info);
        }
    }

    std::sort(files.begin(), files.end(), [&](const QFileInfo& left, const QFileInfo& right)
    {
        const QString leftPath = projectDir.relativeFilePath(left.absoluteFilePath());
        const QString rightPath = projectDir.relativeFilePath(right.absoluteFilePath());
        return leftPath.compare(rightPath, Qt::CaseInsensitive) < 0;
    });
    return files;
}

QJsonArray inputSnapshot(const QDir& projectDir, const QDir& sourceDir)
{
    QJsonArray inputs;
    for (const QFileInfo& info : generationInputFiles(projectDir, sourceDir))
    {
        QJsonObject input;
        input.insert(QStringLiteral("path"), normalizeRelativePath(
                         projectDir.relativeFilePath(info.absoluteFilePath())));
        input.insert(QStringLiteral("size"), static_cast<double>(info.size()));
        input.insert(QStringLiteral("modified"), static_cast<double>(
                         info.lastModified().toMSecsSinceEpoch()));
        inputs.append(input);
    }
    return inputs;
}

QString baseDisplayName(const QString& kind)
{
    const QString normalized = kind.toLower();
    if (normalized == QStringLiteral("head_pressure"))
        return QStringLiteral("水头渗压");
    if (normalized == QStringLiteral("velocity"))
        return QStringLiteral("渗流速度");
    if (normalized == QStringLiteral("displacement"))
        return QStringLiteral("位移");
    if (normalized == QStringLiteral("principal_stress"))
        return QStringLiteral("主应力");
    if (normalized == QStringLiteral("plastic_zone_stress"))
        return QStringLiteral("最大塑性应力");
    if (normalized == QStringLiteral("plastic_strain"))
        return QStringLiteral("塑性应变");
    if (normalized == QStringLiteral("principal_plastic_strain"))
        return QStringLiteral("主塑性应变");
    if (normalized == QStringLiteral("stress_level"))
        return QStringLiteral("应力水平");
    if (normalized == QStringLiteral("material_function"))
        return QStringLiteral("材料函数值（FUN）");
    if (normalized == QStringLiteral("failure_zone"))
        return QStringLiteral("破坏区");
    if (normalized == QStringLiteral("dynamic_velocity"))
        return QStringLiteral("动速度");
    if (normalized == QStringLiteral("dynamic_acceleration"))
        return QStringLiteral("动加速度");
    if (normalized == QStringLiteral("principal_stress_envelope"))
        return QStringLiteral("动主应力极值");
    if (normalized == QStringLiteral("vtu") || normalized == QStringLiteral("vtm"))
        return QStringLiteral("网格结果");
    return QStringLiteral("其他结果");
}

QString suffixDisplayName(const QString& suffix, bool strengthReduction)
{
    if (suffix.isEmpty())
        return QString();

    static const QRegularExpression methodExpression(
        QStringLiteral("^method(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch methodMatch = methodExpression.match(suffix);
    if (methodMatch.hasMatch())
        return QStringLiteral("方法 %1").arg(methodMatch.captured(1));

    static const QRegularExpression czCbExpression(
        QStringLiteral("^cz(\\d+)cb(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch czCbMatch = czCbExpression.match(suffix);
    if (czCbMatch.hasMatch())
    {
        const int capture = strengthReduction ? 2 : 1;
        const double factor = czCbMatch.captured(capture).toDouble() / 100.0;
        if (strengthReduction)
            return QStringLiteral("强度储备 %1").arg(factor, 0, 'f', 2);
        const QString factorName = factor > 1.000001 ? QStringLiteral("超载系数")
                                                     : QStringLiteral("荷载系数");
        return QStringLiteral("%1 %2").arg(factorName).arg(factor, 0, 'f', 2);
    }
    if (suffix.compare(QStringLiteral("dyna"), Qt::CaseInsensitive) == 0)
        return QStringLiteral("模态/动力");
    static const QRegularExpression layerExpression(
        QStringLiteral("^l(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch layerMatch = layerExpression.match(suffix);
    if (layerMatch.hasMatch())
        return QStringLiteral("第 %1 层").arg(layerMatch.captured(1).toInt());
    return QStringLiteral("派生工况");
}


struct TimeSequenceInfo
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

QString displayNumber(double value)
{
    const double rounded = std::round(value);
    return std::abs(value - rounded) < 1.0e-9
               ? QString::number(static_cast<qint64>(rounded))
               : QString::number(value, 'g', 12);
}

WaterLevelCurve readWaterLevelCurve(const QDir& projectDir, const QDir& sourceDir,
                                    const QString& fileName)
{
    const QDir inputProject = generationProjectDir(projectDir, sourceDir);
    const QStringList inputDirectories = {
        inputProject.filePath(QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName)),
        inputProject.filePath(QString::fromUtf8(PublicConstants::kProjectInputDirectoryName)),
        inputProject.filePath(QString::fromLatin1(PublicConstants::kLegacyInputDirectoryName))};

    QFileInfo curveInfo;
    for (const QString& inputDirectory : inputDirectories)
    {
        if (findFileInfo(QDir(inputDirectory), fileName, &curveInfo))
            break;
    }
    if (!curveInfo.isFile())
        return {};

    QFile file(curveInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
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

bool waterLevelAt(const WaterLevelCurve& curve, double time, double* level)
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
        if (duration <= tolerance)
            *level = right.second;
        else
            *level = left.second + (right.second - left.second) *
                                      ((time - left.first) / duration);
        return std::isfinite(*level);
    }
    return false;
}

TimeSequenceInfo readTimeSequenceInfo(const QDir& sourceDir)
{
    QFileInfo keyInfo;
    if (!findFileInfo(sourceDir, QStringLiteral("IKEY.DAT"), &keyInfo) &&
        !findFileInfo(sourceDir, QStringLiteral("IKEY.TXT"), &keyInfo))
        return {};

    QFile file(keyInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
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
            const QString name = names.at(i).trimmed();
            if (name.compare(QStringLiteral("TSTA"), Qt::CaseInsensitive) == 0)
                startIndex = i;
            else if (name.compare(QStringLiteral("DT"), Qt::CaseInsensitive) == 0)
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

            TimeSequenceInfo info;
            info.valid = true;
            info.start = start;
            info.increment = increment;
            return info;
        }
    }
    return {};
}

QString sequenceFrameLabel(int step, const TimeSequenceInfo& timeInfo,
                           const WaterLevelCurve& upstream,
                           const WaterLevelCurve& downstream)
{
    if (step < 0)
        return QStringLiteral("当前状态");
    if (!timeInfo.valid)
        return QStringLiteral("计算时刻索引 %1").arg(step);

    // SOLVER step index JD = INT(TDT/DT) already includes TSTA.
    const double value = timeInfo.increment * step;
    QString label = QStringLiteral("计算时刻 %1 %2").arg(displayNumber(value), timeInfo.unit);
    QStringList waterLevels;
    double level = 0.0;
    if (waterLevelAt(upstream, value, &level))
        waterLevels.push_back(QStringLiteral("上游水位 %1 m").arg(displayNumber(level)));
    if (waterLevelAt(downstream, value, &level))
        waterLevels.push_back(QStringLiteral("下游水位 %1 m").arg(displayNumber(level)));
    if (!waterLevels.isEmpty())
        label += QStringLiteral("（%1）").arg(waterLevels.join(QStringLiteral("，")));
    return label;
}

GeneratedFileInfo generatedFileInfo(const QString& fileName, bool strengthReduction)
{
    const QString base = QFileInfo(fileName).completeBaseName().toLower();
    struct KindAlias
    {
        const char* prefix;
        const char* canonicalKind;
    };
    static const KindAlias aliases[] = {
        {"principal_plastic_strain", "principal_plastic_strain"},
        {"plastic_strain", "plastic_strain"},
        {"principal_stress", "principal_stress"},
        {"plastic_zone_stress", "plastic_zone_stress"},
        {"head_pressure", "head_pressure"},
        {"stress_level", "stress_level"},
        {"material_function", "material_function"},
        {"failure_zone", "failure_zone"},
        {"dynamic_acceleration", "dynamic_acceleration"},
        {"dynamic_velocity", "dynamic_velocity"},
        {"principal_stress_envelope", "principal_stress_envelope"},
        {"displacement", "displacement"},
        {"velocity", "velocity"},
        {"tec_rap_pls", "principal_plastic_strain"},
        {"tec_rai_pls", "plastic_strain"},
        {"tec_rupt", "failure_zone"},
        {"tec_dvelo", "dynamic_velocity"},
        {"tec_dacce", "dynamic_acceleration"},
        {"tec_p1p3", "principal_stress_envelope"},
        {"tec_velo", "velocity"},
        {"tec_disp", "displacement"},
        {"tec_prs", "principal_stress"},
        {"tec_pls_stress", "plastic_zone_stress"},
        {"tec_cmm", "stress_level"},
        {"tec_sl", "stress_level"},
        {"tec_fun", "material_function"},
        {"tec_hp", "head_pressure"},
    };

    for (const KindAlias& alias : aliases)
    {
        const QString prefix = QString::fromLatin1(alias.prefix);
        if (base != prefix && !base.startsWith(prefix + QLatin1Char('_')))
            continue;

        QString suffix = base == prefix ? QString() : base.mid(prefix.size() + 1);
        const QString excavatedMarker = QStringLiteral("with_excavated");
        bool includesExcavated = false;
        if (suffix == excavatedMarker)
        {
            suffix.clear();
            includesExcavated = true;
        }
        else if (suffix.endsWith(QStringLiteral("_") + excavatedMarker))
        {
            suffix.chop(excavatedMarker.size() + 1);
            includesExcavated = true;
        }

        QString frameKey;
        int frameOrder = -1;
        static const QRegularExpression stepExpression(
            QStringLiteral("^step(\\d+)(?:_(.*))?$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch stepMatch = stepExpression.match(suffix);
        if (stepMatch.hasMatch())
        {
            frameOrder = stepMatch.captured(1).toInt();
            frameKey = QStringLiteral("step:%1").arg(frameOrder);
            suffix = stepMatch.captured(2);
        }

        const QString kind = QString::fromLatin1(alias.canonicalKind);
        const QString condition = suffixDisplayName(suffix, strengthReduction);
        QString displayName = baseDisplayName(kind);

        GeneratedFileInfo info;
        info.kind = kind;
        info.frameKey = frameKey;
        info.frameOrder = frameOrder;

        // 强度折减属于同一计算阶段下的多个结果工况，不应该把“强度储备 1.00”
        // 重复塞进“结果文件”名称。静态强度折减结果借用第三层选择器承载折减系数，
        // UI 会把“结果时刻”动态改成“折减系数”。真正的时序结果仍保持原逻辑。
        if (strengthReduction && info.frameKey.isEmpty() && !condition.isEmpty())
        {
            static const QRegularExpression strengthConditionExpression(
                QStringLiteral("^强度储备\\s+([-+]?\\d+(?:\\.\\d+)?)$"));
            const QRegularExpressionMatch strengthMatch =
                strengthConditionExpression.match(condition);
            if (strengthMatch.hasMatch())
            {
                const double factor = strengthMatch.captured(1).toDouble();
                info.frameKey = QStringLiteral("condition:strength:%1")
                                    .arg(QString::number(factor, 'f', 6));
                info.frameLabel = QString::number(factor, 'f', 2);
                info.frameValue = factor;
                info.frameUnit = QStringLiteral("strength_reduction");
                info.frameOrder = qRound(factor * 10000.0);
            }
        }

        // 非强度折减派生工况仍沿用旧的“工况 - 结果类型”命名；
        // 强度折减已经被提到折减系数选择器后，结果文件只显示“位移/主应力/…” 。
        if (!condition.isEmpty() && info.frameKey.isEmpty())
            displayName = QStringLiteral("%1 - %2").arg(condition, displayName);
        if (includesExcavated)
            displayName += QStringLiteral("（含已开挖体）");

        info.displayName = displayName;
        return info;
    }

    GeneratedFileInfo info;
    info.displayName = QStringLiteral("其他结果");
    info.kind = base;
    return info;
}

bool manifestMissingOrStale(const QDir& projectDir, const QDir& sourceDir,
                            const QDir& preparedDir)
{
    const QFileInfo manifestInfo(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!manifestInfo.exists() || !manifestInfo.isFile())
        return true;

    QFile manifestFile(manifestInfo.absoluteFilePath());
    if (!manifestFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return true;
    const QJsonDocument document = QJsonDocument::fromJson(manifestFile.readAll());
    if (!document.isObject() ||
        document.object().value(QStringLiteral("version")).toInt() != kManifestVersion)
        return true;

    const QJsonArray storedInputs = document.object().value(QStringLiteral("inputs")).toArray();
    const QJsonArray currentInputs = inputSnapshot(projectDir, sourceDir);
    if (QJsonDocument(storedInputs).toJson(QJsonDocument::Compact) !=
        QJsonDocument(currentInputs).toJson(QJsonDocument::Compact))
        return true;

    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    if (files.isEmpty())
        return true;
    for (const QJsonValue& value : files)
    {
        const QString fileName = value.toObject().value(QStringLiteral("fileName")).toString();
        if (fileName.isEmpty() ||
            !resultPackageExists(QFileInfo(preparedDir.filePath(fileName))))
            return true;
    }
    return false;
}

bool writeManifest(const QDir& projectDir, const QDir& sourceDir, const QDir& preparedDir,
                   const PostResultGenerator::Result& generated)
{
    const bool strengthReduction = usesStrengthReduction(sourceDir);
    const TimeSequenceInfo timeInfo = readTimeSequenceInfo(sourceDir);
    const WaterLevelCurve upstream =
        readWaterLevelCurve(projectDir, sourceDir, QStringLiteral("HWDROP_UP.DAT"));
    const WaterLevelCurve downstream =
        readWaterLevelCurve(projectDir, sourceDir, QStringLiteral("HWDROP_DW.DAT"));
    QJsonObject root;
    root.insert(QStringLiteral("version"), kManifestVersion);
    root.insert(QStringLiteral("sourceDir"),
                sourceRelativePath(projectDir, sourceDir.absolutePath()));
    root.insert(QStringLiteral("preparedAt"),
                QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    root.insert(QStringLiteral("inputs"), inputSnapshot(projectDir, sourceDir));

    QJsonArray files;
    for (const QString& generatedPath : generated.generatedFiles)
    {
        const QFileInfo info(generatedPath);
        if (info.dir().absolutePath().compare(preparedDir.absolutePath(), Qt::CaseInsensitive) != 0)
            continue;

        const QString suffix = info.suffix().toLower();
        if (suffix != QStringLiteral("tec") && suffix != QStringLiteral("vtu") &&
            suffix != QStringLiteral("vtm"))
            continue;

        const GeneratedFileInfo metadata = generatedFileInfo(info.fileName(), strengthReduction);
        QJsonObject object;
        object.insert(QStringLiteral("displayName"), metadata.displayName);
        object.insert(QStringLiteral("fileName"), info.fileName());
        object.insert(QStringLiteral("kind"), metadata.kind);
        object.insert(QStringLiteral("format"),
                      suffix == QStringLiteral("vtm")
                          ? QStringLiteral("vtm")
                          : (suffix == QStringLiteral("vtu")
                                 ? QStringLiteral("vtu")
                                 : QStringLiteral("tecplot")));
        if (!metadata.frameKey.isEmpty())
        {
            const bool conditionFrame =
                metadata.frameKey.startsWith(QStringLiteral("condition:"));
            const double frameValue = conditionFrame
                ? metadata.frameValue
                : (timeInfo.valid
                       ? timeInfo.increment * metadata.frameOrder
                       : static_cast<double>(metadata.frameOrder));
            object.insert(QStringLiteral("frameKey"), metadata.frameKey);
            object.insert(QStringLiteral("frameLabel"),
                          conditionFrame
                              ? metadata.frameLabel
                              : sequenceFrameLabel(metadata.frameOrder, timeInfo,
                                                   upstream, downstream));
            object.insert(QStringLiteral("frameValue"), frameValue);
            object.insert(QStringLiteral("frameUnit"),
                          conditionFrame
                              ? metadata.frameUnit
                              : (timeInfo.valid ? timeInfo.unit : QString()));
            object.insert(QStringLiteral("frameOrder"), metadata.frameOrder);
            if (!conditionFrame)
            {
                double waterLevel = 0.0;
                if (waterLevelAt(upstream, frameValue, &waterLevel))
                    object.insert(QStringLiteral("upstreamWaterLevel"), waterLevel);
                if (waterLevelAt(downstream, frameValue, &waterLevel))
                    object.insert(QStringLiteral("downstreamWaterLevel"), waterLevel);
            }
        }
        object.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
        object.insert(QStringLiteral("modified"),
                      info.lastModified().toUTC().toString(Qt::ISODate));
        files.append(object);
    }
    root.insert(QStringLiteral("files"), files);

    QJsonArray warnings;
    for (const QString& warning : generated.warnings)
        warnings.append(warning);
    root.insert(QStringLiteral("warnings"), warnings);

    QFile file(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void sortResultFiles(QVector<PostResultAutoPrepare::ResultFile>& files);

QVector<PostResultAutoPrepare::ResultFile> readManifestFiles(const QDir& preparedDir)
{
    QFile file(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return {};

    // 结果拓扑格式升级时不能继续直接复用旧网格缓存。这里只读取很小的
    // manifest 版本号，不做 inputSnapshot/目录扫描，因此不会拖慢正常加载。
    if (document.object().value(QStringLiteral("version")).toInt() != kManifestVersion)
        return {};

    QVector<PostResultAutoPrepare::ResultFile> result;
    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : files)
    {
        const QJsonObject object = value.toObject();
        const QString fileName = object.value(QStringLiteral("fileName")).toString();
        const QFileInfo info(preparedDir.filePath(fileName));
        if (fileName.isEmpty() || !resultPackageExists(info))
            continue;

        PostResultAutoPrepare::ResultFile entry;
        entry.kind = object.value(QStringLiteral("kind")).toString();
        entry.displayName = object.value(QStringLiteral("displayName")).toString();
        if (entry.displayName.isEmpty())
            entry.displayName = baseDisplayName(entry.kind);
        entry.filePath = info.absoluteFilePath();
        entry.format = object.value(QStringLiteral("format"))
                           .toString(QStringLiteral("tecplot"));
        entry.frameKey = object.value(QStringLiteral("frameKey")).toString();
        entry.frameLabel = object.value(QStringLiteral("frameLabel")).toString();
        entry.frameValue = object.value(QStringLiteral("frameValue")).toDouble();
        entry.frameUnit = object.value(QStringLiteral("frameUnit")).toString();
        entry.frameOrder = object.value(QStringLiteral("frameOrder")).toInt(-1);
        result.push_back(entry);
    }
    sortResultFiles(result);
    return result;
}

QVector<PostResultAutoPrepare::ResultFile> fallbackSourceFiles(const QDir& projectDir,
                                                               const QDir& sourceDir,
                                                               bool includeVtu)
{
    QVector<PostResultAutoPrepare::ResultFile> result;
    const bool strengthReduction = usesStrengthReduction(sourceDir);
    const TimeSequenceInfo timeInfo = readTimeSequenceInfo(sourceDir);
    const WaterLevelCurve upstream =
        readWaterLevelCurve(projectDir, sourceDir, QStringLiteral("HWDROP_UP.DAT"));
    const WaterLevelCurve downstream =
        readWaterLevelCurve(projectDir, sourceDir, QStringLiteral("HWDROP_DW.DAT"));
    const QStringList names = {QStringLiteral("TEC_HP.DAT"), QStringLiteral("TEC_VELO.DAT"),
                               QStringLiteral("TEC_DISP.DAT"), QStringLiteral("TEC_PRS.DAT"),
                               QStringLiteral("TEC_RAI_PLS.DAT"),
                               QStringLiteral("TEC_RAP_PLS.DAT"),
                               QStringLiteral("TEC_SL.DAT"), QStringLiteral("TEC_CMM.DAT"),
                               QStringLiteral("TEC_RUPT.DAT"), QStringLiteral("TEC_DVELO.DAT"),
                               QStringLiteral("TEC_DACCE.DAT"), QStringLiteral("TEC_P1P3.DAT"),
                               QStringLiteral("TEC_FUN.DAT"), QStringLiteral("TEC_PLS_STRESS.DAT"),
                               QStringLiteral("*.tec")};
    QSet<QString> seenPaths;
    for (const QString& pattern : names)
    {
        const bool wildcard = pattern.contains(QLatin1Char('*'));
        const QStringList matched = wildcard
                                        ? sourceDir.entryList({pattern}, QDir::Files, QDir::Name)
                                        : (QFileInfo::exists(sourceDir.filePath(pattern))
                                               ? QStringList{pattern} : QStringList{});
        for (const QString& fileName : matched)
        {
            const QFileInfo info(sourceDir.filePath(fileName));
            const QString pathKey = info.absoluteFilePath().toLower();
            if (seenPaths.contains(pathKey))
                continue;
            seenPaths.insert(pathKey);

            const GeneratedFileInfo metadata = generatedFileInfo(fileName, strengthReduction);
            PostResultAutoPrepare::ResultFile entry;
            entry.displayName = metadata.displayName;
            entry.filePath = info.absoluteFilePath();
            entry.kind = metadata.kind;
            entry.format = QStringLiteral("tecplot");
            entry.frameKey = metadata.frameKey;
            entry.frameOrder = metadata.frameOrder;
            if (!entry.frameKey.isEmpty())
            {
                if (entry.frameKey.startsWith(QStringLiteral("condition:")))
                {
                    entry.frameLabel = metadata.frameLabel;
                    entry.frameValue = metadata.frameValue;
                    entry.frameUnit = metadata.frameUnit;
                }
                else if (entry.frameOrder >= 0)
                {
                    entry.frameLabel = sequenceFrameLabel(entry.frameOrder, timeInfo,
                                                          upstream, downstream);
                    if (timeInfo.valid)
                    {
                        entry.frameValue = timeInfo.increment * entry.frameOrder;
                        entry.frameUnit = timeInfo.unit;
                    }
                }
            }
            result.push_back(entry);
        }
    }

    if (includeVtu)
    {
        const QStringList files =
            sourceDir.entryList({QStringLiteral("*.vtu"), QStringLiteral("*.vtm")},
                                QDir::Files, QDir::Name);
        for (int i = 0; i < files.size(); ++i)
        {
            PostResultAutoPrepare::ResultFile entry;
            entry.displayName = files.size() == 1 ? QStringLiteral("网格结果")
                                                   : QStringLiteral("网格结果 %1").arg(i + 1);
            entry.filePath = QFileInfo(sourceDir.filePath(files.at(i))).absoluteFilePath();
            const QString format = QFileInfo(files.at(i)).suffix().toLower();
            entry.kind = format;
            entry.format = format;
            result.push_back(entry);
        }
    }
    sortResultFiles(result);
    return result;
}

int resultTypeOrder(const QString& displayName)
{
    const QStringList orderedTypes = {
        QStringLiteral("水头渗压"), QStringLiteral("渗流速度"),
        QStringLiteral("位移"), QStringLiteral("主应力"),
        QStringLiteral("最大塑性应力"), QStringLiteral("塑性应变"), QStringLiteral("主塑性应变"),
        QStringLiteral("应力水平"), QStringLiteral("材料函数值（FUN）"), QStringLiteral("破坏区"),
        QStringLiteral("网格结果"), QStringLiteral("其他结果")};
    for (int index = 0; index < orderedTypes.size(); ++index)
    {
        if (displayName.contains(orderedTypes.at(index)))
            return index;
    }
    return orderedTypes.size();
}

struct ResultSortKey
{
    int conditionGroup = 0;
    double conditionValue = 0.0;
    int typeOrder = 0;
    QString text;
};

ResultSortKey resultSortKey(const QString& displayName)
{
    ResultSortKey key;
    key.typeOrder = resultTypeOrder(displayName);
    key.text = displayName;

    static const QRegularExpression factorExpression(
        QStringLiteral("^(强度储备|荷载系数|超载系数)\\s+([-+]?\\d+(?:\\.\\d+)?)\\s+-\\s+"));
    const QRegularExpressionMatch factorMatch = factorExpression.match(displayName);
    if (factorMatch.hasMatch())
    {
        const QString kind = factorMatch.captured(1);
        key.conditionGroup = kind == QStringLiteral("强度储备") ? 1 : 2;
        key.conditionValue = factorMatch.captured(2).toDouble();
        return key;
    }

    static const QRegularExpression methodExpression(
        QStringLiteral("^方法\\s+(\\d+)\\s+-\\s+"));
    const QRegularExpressionMatch methodMatch = methodExpression.match(displayName);
    if (methodMatch.hasMatch())
    {
        key.conditionGroup = 3;
        key.conditionValue = methodMatch.captured(1).toDouble();
        return key;
    }

    if (displayName.startsWith(QStringLiteral("派生工况 - ")))
        key.conditionGroup = 4;
    return key;
}

void sortResultFiles(QVector<PostResultAutoPrepare::ResultFile>& files)
{
    std::stable_sort(files.begin(), files.end(), [](const auto& left, const auto& right)
    {
        const bool leftSequence = left.frameOrder >= 0;
        const bool rightSequence = right.frameOrder >= 0;
        if (leftSequence != rightSequence)
            return !leftSequence;
        if (leftSequence && left.frameOrder != right.frameOrder)
            return left.frameOrder < right.frameOrder;

        const ResultSortKey leftKey = resultSortKey(left.displayName);
        const ResultSortKey rightKey = resultSortKey(right.displayName);
        if (leftKey.conditionGroup != rightKey.conditionGroup)
            return leftKey.conditionGroup < rightKey.conditionGroup;
        if (std::abs(leftKey.conditionValue - rightKey.conditionValue) > 1.0e-12)
            return leftKey.conditionValue < rightKey.conditionValue;
        if (leftKey.typeOrder != rightKey.typeOrder)
            return leftKey.typeOrder < rightKey.typeOrder;
        return QString::localeAwareCompare(leftKey.text, rightKey.text) < 0;
    });
}
} // namespace

namespace PostResultAutoPrepare
{
QStringList defaultResultDirectories()
{
    return {};
}

QStringList discoverResultDirectories(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return {};

    QStringList result;
    QSet<QString> seen;
    QStringList searchRoots = analysisWorkspaceDirectories(project);
    if (searchRoots.isEmpty())
        searchRoots.push_back(project.absolutePath());

    for (const QString& searchRoot : searchRoots)
    {
        const QDir root(searchRoot);
        if (!isPreparedResultPath(project, root.absolutePath()) &&
            hasMeshFile(root.absolutePath()) && hasSupportedFieldData(root))
            appendUnique(result, seen, project.relativeFilePath(root.absolutePath()));

        QDirIterator iterator(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
        {
            const QString absolutePath = QDir(iterator.next()).absolutePath();
            if (isPreparedResultPath(project, absolutePath))
                continue;
            const QDir source(absolutePath);
            if (hasMeshFile(absolutePath) && hasSupportedFieldData(source))
                appendUnique(result, seen, project.relativeFilePath(absolutePath));
        }
    }

    auto physicalSortKey = [](const QString& relPath) -> QVector<int>
    {
        const QString upper = relPath.toUpper();
        const QString leaf = QFileInfo(upper).fileName();
        // Key format: {domainGroup, caseNo, cycleOrStage, phase, subPhase, layer}
        if (leaf == QStringLiteral("GEOSTR"))
            return {0, 0, 0, 0, 0, 0};
        if (leaf == QStringLiteral("DISK00"))
            return {1, 0, 0, 0, 0, 0};
        if (leaf == QStringLiteral("DISK01"))
            return {1, 0, 0, 1, 0, 0};
        if (leaf == QStringLiteral("SEEPFREE"))
            return {2, 0, 0, 0, 0, 0};
        static const QRegularExpression seepStatRe(QStringLiteral("^SEEPCASE(\\d+)$"));
        if (const auto m = seepStatRe.match(leaf); m.hasMatch())
            return {2, m.captured(1).toInt(), 1, 0, 0, 0};
        static const QRegularExpression seepDynaRe(QStringLiteral("^SEEPDYNA(\\d+)$"));
        if (const auto m = seepDynaRe.match(leaf); m.hasMatch())
            return {2, m.captured(1).toInt(), 2, 0, 0, 0};
        static const QRegularExpression damCycleRe(
            QStringLiteral("^CASE(\\d+)(PACK|PLIU|FILL|FLIU|WETT|DROP|DICE)(\\d{1,2})?(?:L?(\\d+))?$"));
        if (const auto m = damCycleRe.match(leaf); m.hasMatch())
        {
            const int c = m.captured(1).toInt();
            const QString tag = m.captured(2);
            const int cyc = m.captured(3).isEmpty() ? 99 : m.captured(3).toInt();
            const int lay = m.captured(4).isEmpty() ? 9999 : m.captured(4).toInt();
            int phase = 50;
            if (tag == QStringLiteral("PACK")) phase = 10;
            else if (tag == QStringLiteral("PLIU")) phase = 20;
            else if (tag == QStringLiteral("FILL")) phase = 30;
            else if (tag == QStringLiteral("FLIU")) phase = 40;
            else if (tag == QStringLiteral("WETT")) return {3, c, 90, 10, 0, 0};
            else if (tag == QStringLiteral("DROP")) return {3, c, 90, 20, cyc, 0};
            else if (tag == QStringLiteral("DICE")) return {3, c, 90, 30, cyc, 0};
            return {3, c, cyc, phase, lay, 0};
        }
        static const QRegularExpression caveStageRe(
            QStringLiteral("^(E|J)(\\d+)(PART|DCL)?$"));
        if (const auto m = caveStageRe.match(leaf); m.hasMatch())
        {
            const int isJaca = (m.captured(1) == QStringLiteral("J")) ? 1 : 0;
            const int stage = m.captured(2).toInt();
            const QString suf = m.captured(3);
            const int sub = suf == QStringLiteral("PART") ? 0 : (suf == QStringLiteral("DCL") ? 1 : 2);
            return {4, isJaca, stage, sub, 0, 0};
        }
        static const QRegularExpression casePostRe(
            QStringLiteral("(?:^|/)CASE(\\d+)/?(EALL|JALL|LINE|CZ|ZH|CB|MODE|WAVE|SETT)$"));
        if (const auto m = casePostRe.match(upper); m.hasMatch())
        {
            const int c = m.captured(1).toInt();
            const QString tag = m.captured(2);
            int ord = 95;
            if (tag == QStringLiteral("EALL")) ord = 60;
            else if (tag == QStringLiteral("JALL")) ord = 62;
            else if (tag == QStringLiteral("LINE")) ord = 65;
            else if (tag == QStringLiteral("CZ")) ord = 70;
            else if (tag == QStringLiteral("ZH")) ord = 72;
            else if (tag == QStringLiteral("CB")) ord = 74;
            else if (tag == QStringLiteral("MODE")) ord = 80;
            else if (tag == QStringLiteral("WAVE")) ord = 85;
            else if (tag == QStringLiteral("SETT")) ord = 90;
            return {5, c, ord, 0, 0, 0};
        }
        static const QRegularExpression caseBaseRe(QStringLiteral("^CASE(\\d+)$"));
        if (const auto m = caseBaseRe.match(leaf); m.hasMatch())
            return {3, m.captured(1).toInt(), 0, 0, 0, 0};
        return {9, 0, 0, 0, 0, 0};
    };
    std::sort(result.begin(), result.end(), [&](const QString& left, const QString& right)
    {
        const QVector<int> lk = physicalSortKey(left);
        const QVector<int> rk = physicalSortKey(right);
        if (lk != rk)
            return lk < rk;
        return left.compare(right, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QString firstResultDirectory(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return projectDir;
    if (hasMeshFile(project.absolutePath()) && hasSupportedFieldData(project))
        return project.absolutePath();

    const QStringList directories = discoverResultDirectories(project.absolutePath());
    return directories.isEmpty() ? project.absolutePath() : project.filePath(directories.first());
}

QString damStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();

    // CPU_AUTOPACK writes each fill layer as CASE<case>PACK<cycle><layer>.
    // The cycle number occupies two characters and the remaining suffix is
    // the actual mesh layer number.
    static const QRegularExpression packLayerExpression(
        QStringLiteral("^CASE\\d+PACK(\\d{2})L?(\\d+)$"));
    QRegularExpressionMatch match = packLayerExpression.match(leaf);
    if (match.hasMatch())
    {
        const int cycle = match.captured(1).toInt();
        const int layer = match.captured(2).toInt();
        return cycle > 1
                   ? QStringLiteral("第 %1 轮 - 第 %2 层填筑").arg(cycle).arg(layer)
                   : QStringLiteral("第 %1 层填筑").arg(layer);
    }

    static const QRegularExpression packCycleExpression(
        QStringLiteral("^CASE\\d+PACK(\\d+)$"));
    match = packCycleExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 阶段填筑").arg(match.captured(1).toInt());

    static const QRegularExpression fillExpression(
        QStringLiteral("^CASE\\d+FILL(\\d+)$"));
    match = fillExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 级蓄水").arg(match.captured(1).toInt());

    static const QRegularExpression packCreepExpression(
        QStringLiteral("^CASE\\d+PLIU(\\d+)$"));
    match = packCreepExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 阶段填筑流变").arg(match.captured(1).toInt());

    static const QRegularExpression fillCreepExpression(
        QStringLiteral("^CASE\\d+FLIU(\\d+)$"));
    match = fillCreepExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 阶段蓄水流变").arg(match.captured(1).toInt());

    static const QRegularExpression dropExpression(
        QStringLiteral("^CASE\\d+DROP(\\d+)$"));
    match = dropExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 级降水").arg(match.captured(1).toInt());

    static const QRegularExpression iceExpression(
        QStringLiteral("^CASE\\d+DICE(\\d+)$"));
    match = iceExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("第 %1 阶段冻融结冰").arg(match.captured(1).toInt());

    if (leaf == QStringLiteral("DISK00"))
        return QStringLiteral("坝基自重状态");
    if (leaf == QStringLiteral("DISK01"))
        return QStringLiteral("坝体自重状态");
    if (leaf.contains(QStringLiteral("WETT")))
        return QStringLiteral("湿化变形");

    static const QRegularExpression caseSubStageExpression(
        QStringLiteral("(?:^|/)CASE(\\d+)/?(CZ|ZH|CB|MODE|WAVE|SETT)$"));
    match = caseSubStageExpression.match(relative);
    if (match.hasMatch())
    {
        const QString code = match.captured(2);
        QString stage;
        if (code == QStringLiteral("CZ"))
            stage = QStringLiteral("超载分析");
        else if (code == QStringLiteral("ZH"))
            stage = QStringLiteral("综合分析");
        else if (code == QStringLiteral("CB"))
            stage = QStringLiteral("强度储备分析");
        else if (code == QStringLiteral("MODE"))
            stage = QStringLiteral("模态分析");
        else if (code == QStringLiteral("WAVE"))
            stage = QStringLiteral("动力响应");
        else if (code == QStringLiteral("SETT"))
            stage = QStringLiteral("地震永久变形");
        return stage;
    }

    if (relative.contains(QStringLiteral("PACK")))
        return QStringLiteral("填筑过程");
    if (relative.contains(QStringLiteral("FILL")))
        return QStringLiteral("蓄水过程");
    if (relative.contains(QStringLiteral("PLIU")))
        return QStringLiteral("填筑流变");
    if (relative.contains(QStringLiteral("FLIU")))
        return QStringLiteral("蓄水流变");
    if (relative.contains(QStringLiteral("DROP")))
        return QStringLiteral("降水过程");
    if (relative.contains(QStringLiteral("DICE")))
        return QStringLiteral("冻融结冰");
    return QString();
}

QString seepageStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();

    if (leaf == QStringLiteral("SEEPFREE") || relative.endsWith(QStringLiteral("/SEEPFREE")))
        return QStringLiteral("天然渗流场");

    static const QRegularExpression steadyExpression(
        QStringLiteral("^SEEPCASE\\d+$"));
    if (steadyExpression.match(leaf).hasMatch())
        return QStringLiteral("稳态渗流");

    static const QRegularExpression transientExpression(
        QStringLiteral("^SEEPDYNA\\d+$"));
    if (transientExpression.match(leaf).hasMatch())
        return QStringLiteral("非稳态渗流");

    if (relative.contains(QStringLiteral("SEEPDYNA")))
        return QStringLiteral("非稳态渗流");
    if (relative.contains(QStringLiteral("SEEPCASE")))
        return QStringLiteral("稳态渗流");
    return QString();
}

QString caveStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();

    if (leaf == QStringLiteral("GEOSTR") || relative.endsWith(QStringLiteral("/GEOSTR")))
        return QStringLiteral("地应力回归结果");

    // 洞室求解器实际输出目录形如：
    //   CASE01EXCA/E01、CASE01JACA/J01、CASE01EALL、CASE01JALL。
    // CASE 后的编号是计算 CASE 编号，不是开挖级次；真正的开挖级次来自 E01/J01。
    static const QRegularExpression eallExpression(QStringLiteral("^CASE\\d+EALL$"));
    if (leaf == QStringLiteral("EALL") || eallExpression.match(leaf).hasMatch())
        return QStringLiteral("开挖完成状态");

    static const QRegularExpression jallExpression(QStringLiteral("^CASE\\d+JALL$"));
    if (leaf == QStringLiteral("JALL") || jallExpression.match(leaf).hasMatch())
        return QStringLiteral("开挖与支护完成状态");

    static const QRegularExpression lineExpression(QStringLiteral("^CASE\\d+LINE$"));
    if (leaf == QStringLiteral("LINE") || lineExpression.match(leaf).hasMatch() ||
        relative.endsWith(QStringLiteral("/LINE")))
        return QStringLiteral("衬砌计算");

    static const QRegularExpression excavationExpression(QStringLiteral("^E(\\d+)(PART|DCL)?$"));
    QRegularExpressionMatch match = excavationExpression.match(leaf);
    if (match.hasMatch())
    {
        static const QRegularExpression excavationPathExpression(
            QStringLiteral("(?:^|/)CASE\\d+EXCA/E\\d+(?:PART|DCL)?$"));
        if (excavationPathExpression.match(relative).hasMatch() ||
            relative.contains(QStringLiteral("/EXCA/")))
        {
            const QString suf = match.captured(2);
            if (suf == QStringLiteral("PART"))
                return QStringLiteral("第 %1 级开挖（半释放）").arg(match.captured(1).toInt());
            if (suf == QStringLiteral("DCL"))
                return QStringLiteral("第 %1 级吊车梁安装").arg(match.captured(1).toInt());
            return QStringLiteral("第 %1 级开挖").arg(match.captured(1).toInt());
        }
    }

    static const QRegularExpression supportExpression(QStringLiteral("^J(\\d+)(PART|DCL)?$"));
    match = supportExpression.match(leaf);
    if (match.hasMatch())
    {
        static const QRegularExpression supportPathExpression(
            QStringLiteral("(?:^|/)CASE\\d+JACA/J\\d+(?:PART|DCL)?$"));
        if (supportPathExpression.match(relative).hasMatch() ||
            relative.contains(QStringLiteral("/JACA/")))
        {
            const QString suf = match.captured(2);
            if (suf == QStringLiteral("PART"))
                return QStringLiteral("第 %1 级开挖与支护（半释放）").arg(match.captured(1).toInt());
            if (suf == QStringLiteral("DCL"))
                return QStringLiteral("第 %1 级吊车梁安装与支护").arg(match.captured(1).toInt());
            return QStringLiteral("第 %1 级开挖与支护").arg(match.captured(1).toInt());
        }
    }

    static const QRegularExpression excaRootExpression(QStringLiteral("^CASE\\d+EXCA$"));
    if (leaf == QStringLiteral("EXCA") || excaRootExpression.match(leaf).hasMatch())
        return QStringLiteral("分级开挖");

    static const QRegularExpression jacaRootExpression(QStringLiteral("^CASE\\d+JACA$"));
    if (leaf == QStringLiteral("JACA") || jacaRootExpression.match(leaf).hasMatch())
        return QStringLiteral("分级开挖与支护");

    // CASE01 是洞室开挖计算的结果根目录，不把 01 解释为第 1 级开挖/工况 1。
    static const QRegularExpression caveCaseExpression(QStringLiteral("^CASE\\d+$"));
    if (caveCaseExpression.match(leaf).hasMatch())
        return QStringLiteral("开挖结果");

    return QString();
}

QString slopeStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();

    static const QRegularExpression dynamicExpression(
        QStringLiteral("(?:^|/)CASE(\\d+)/?(MODE|WAVE)$"));
    QRegularExpressionMatch match = dynamicExpression.match(relative);
    if (match.hasMatch())
    {
        const QString stage = match.captured(2) == QStringLiteral("MODE")
                                  ? QStringLiteral("模态分析")
                                  : QStringLiteral("动力响应");
        return stage;
    }

    static const QRegularExpression caseExpression(QStringLiteral("^CASE(\\d+)$"));
    match = caseExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("稳定性计算");
    return QString();
}

QString blockStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();
    static const QRegularExpression caseExpression(QStringLiteral("^CASE(\\d+)$"));
    const QRegularExpressionMatch match = caseExpression.match(leaf);
    if (relative.contains(QStringLiteral("SLIDERCASE")) && match.hasMatch())
        return QStringLiteral("滑块稳定计算");
    if (leaf == QStringLiteral("SLIDERCASE") || relative.endsWith(QStringLiteral("/SLIDERCASE")))
        return QStringLiteral("块体稳定计算");
    return QString();
}

QString genericCaseStageDisplayName(const QString& relativePath, const QString& leafName)
{
    const QString relative = relativePath.toUpper();
    const QString leaf = leafName.toUpper();
    static const QRegularExpression subStageExpression(
        QStringLiteral("(?:^|/)CASE(\\d+)/?(CZ|ZH|CB|MODE|WAVE|SETT)$"));
    QRegularExpressionMatch match = subStageExpression.match(relative);
    if (match.hasMatch())
    {
        const QString code = match.captured(2);
        QString stage;
        if (code == QStringLiteral("CZ"))
            stage = QStringLiteral("超载分析");
        else if (code == QStringLiteral("ZH"))
            stage = QStringLiteral("综合分析");
        else if (code == QStringLiteral("CB"))
            stage = QStringLiteral("强度储备分析");
        else if (code == QStringLiteral("MODE"))
            stage = QStringLiteral("模态分析");
        else if (code == QStringLiteral("WAVE"))
            stage = QStringLiteral("动力响应");
        else
            stage = QStringLiteral("地震永久变形");
        return stage;
    }

    static const QRegularExpression caseExpression(QStringLiteral("^CASE(\\d+)$"));
    match = caseExpression.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("结构计算");
    return QString();
}

QString resultGroupDisplayName(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    const QString relative = sourceRelativePath(project, source.absolutePath()).toUpper();
    const QString leaf = QFileInfo(source.absolutePath()).fileName().toUpper();
    const AnalysisIdentity identity = analysisIdentityForSource(project, source);
    const QString kind = identity.kind.trimmed();

    const QString sourcePath = QDir::fromNativeSeparators(source.absolutePath());
    const QString workspacePath = identity.workspacePath.isEmpty()
                                      ? QString()
                                      : QDir::fromNativeSeparators(
                                            QDir(identity.workspacePath).absolutePath());
    const bool workspaceRoot = !workspacePath.isEmpty() &&
                               sourcePath.compare(workspacePath, Qt::CaseInsensitive) == 0;

    QString stage;
    if (kind == QStringLiteral("SeepageAnalysis") ||
        relative.contains(QStringLiteral("SEEP")) ||
        hasFilePrefix(source, QStringLiteral("INVELO")))
    {
        stage = seepageStageDisplayName(relative, leaf);
        if (!stage.isEmpty())
            return stage;
        if (workspaceRoot)
            return QStringLiteral("渗流计算结果");
    }

    if (kind == QStringLiteral("CaveExcavationAnalysis") ||
        kind == QStringLiteral("GeoStressRegressionAnalysis") ||
        relative.contains(QStringLiteral("GEOSTR")) ||
        relative.contains(QStringLiteral("/EXCA")) ||
        relative.contains(QStringLiteral("/JACA")) ||
        relative.contains(QStringLiteral("EXCA/")) ||
        relative.contains(QStringLiteral("JACA/")) ||
        QRegularExpression(QStringLiteral("^CASE\\d+(?:EALL|JALL|EXCA|JACA|LINE)$")).match(leaf).hasMatch() ||
        leaf == QStringLiteral("EALL") || leaf == QStringLiteral("JALL") ||
        leaf == QStringLiteral("LINE"))
    {
        stage = caveStageDisplayName(relative, leaf);
        if (!stage.isEmpty())
            return stage;
        if (workspaceRoot)
            return kind == QStringLiteral("GeoStressRegressionAnalysis")
                       ? QStringLiteral("地应力回归结果")
                       : QStringLiteral("洞室计算汇总");
    }

    if (kind == QStringLiteral("SlopeStabilityAnalysis"))
    {
        stage = slopeStageDisplayName(relative, leaf);
        if (!stage.isEmpty())
            return stage;
        if (workspaceRoot)
            return QStringLiteral("稳定性计算");
    }

    if (kind == QStringLiteral("CaveBlockStabilityAnalysis") ||
        relative.contains(QStringLiteral("SLIDERCASE")))
    {
        stage = blockStageDisplayName(relative, leaf);
        if (!stage.isEmpty())
            return stage;
        if (workspaceRoot)
            return QStringLiteral("块体稳定计算");
    }

    const bool damPath = relative.contains(QStringLiteral("PACK")) ||
                         relative.contains(QStringLiteral("FILL")) ||
                         relative.contains(QStringLiteral("PLIU")) ||
                         relative.contains(QStringLiteral("FLIU")) ||
                         relative.contains(QStringLiteral("WETT")) ||
                         relative.contains(QStringLiteral("DROP")) ||
                         relative.contains(QStringLiteral("DICE")) ||
                         leaf == QStringLiteral("DISK00") ||
                         leaf == QStringLiteral("DISK01");
    if (kind == QStringLiteral("DamAnalysis") || damPath)
    {
        stage = damStageDisplayName(relative, leaf);
        if (!stage.isEmpty())
            return stage;
        if (workspaceRoot)
            return QStringLiteral("完工状态");
    }

    stage = genericCaseStageDisplayName(relative, leaf);
    if (!stage.isEmpty())
        return stage;

    if (workspaceRoot)
    {
        if (!identity.analysisType.isEmpty())
            return QStringLiteral("%1结果").arg(identity.analysisType);
        return QStringLiteral("计算结果");
    }

    if (hasFile(source, QStringLiteral("IIDISP.TXT")) ||
        hasFilePrefix(source, QStringLiteral("MODPRI")) ||
        hasFilePrefix(source, QStringLiteral("NODCMM")) ||
        hasFilePrefix(source, QStringLiteral("IIICMM")))
        return QStringLiteral("结构计算");
    return QStringLiteral("计算结果");
}

QVector<ResultGroup> resultGroups(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return {};

    QStringList sources;
    if (hasMeshFile(project.absolutePath()) && hasSupportedFieldData(project))
        sources.push_back(project.absolutePath());
    for (const QString& relativePath : discoverResultDirectories(project.absolutePath()))
        sources.push_back(project.filePath(relativePath));

    QVector<ResultGroup> groups;
    QHash<QString, int> displayCounts;
    QSet<QString> seenPaths;
    for (const QString& sourcePath : sources)
    {
        const QString absolutePath = QDir(sourcePath).absolutePath();
        const QString pathKey = absolutePath.toLower();
        if (seenPaths.contains(pathKey))
            continue;
        seenPaths.insert(pathKey);

        const QString stageName =
            resultGroupDisplayName(project.absolutePath(), absolutePath);
        QString displayName = stageName;
        const AnalysisIdentity identity = analysisIdentityForSource(project, QDir(absolutePath));
        if (!identity.name.isEmpty())
            displayName = QStringLiteral("%1 - %2").arg(identity.name, stageName);
        const int count = displayCounts.value(displayName) + 1;
        displayCounts.insert(displayName, count);
        if (count > 1)
            displayName += QStringLiteral(" %1").arg(count);
        ResultGroup group;
        group.displayName = displayName;
        group.sourcePath = absolutePath;
        group.analysisName = identity.name;
        group.stageName = stageName;
        if (!identity.uuid.isEmpty())
            group.analysisKey = identity.uuid;
        else if (!identity.workspacePath.isEmpty())
            group.analysisKey = QDir::fromNativeSeparators(
                identity.workspacePath).toCaseFolded();
        else
            group.analysisKey = QDir::fromNativeSeparators(
                project.absolutePath()).toCaseFolded();
        group.order = groups.size();
        groups.push_back(group);
    }
    return groups;
}

QString preparedDirectory(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return QString();
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    const AnalysisIdentity identity = analysisIdentityForSource(project, source);
    const QDir owner(identity.workspacePath.isEmpty() ? project.absolutePath()
                                                      : identity.workspacePath);
    QString relative = normalizeRelativePath(owner.relativeFilePath(source.absolutePath()));
    if (relative == QStringLiteral("."))
        relative = QStringLiteral("综合结果");
    if (relative.isEmpty() || relative.startsWith(QStringLiteral("..")))
        return QString();
    return postRootDirForSource(project, source).filePath(cacheDirName(relative));
}

bool prepareDirectory(const QString& projectDir, const QString& sourceDir)
{
    QMutexLocker preparationLocker(&preparationMutex());

    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    if (!project.exists() || !source.exists() || !hasMeshFile(source.absolutePath()) ||
        !hasSupportedFieldData(source))
        return false;

    QDir prepared(preparedDirectory(project.absolutePath(), source.absolutePath()));
    if (prepared.path().isEmpty() || (!prepared.exists() && !prepared.mkpath(QStringLiteral("."))))
        return false;

    // 同一次页面初始化会连续查询“结果时刻”和“结果文件”，旧代码会完整做两遍
    // manifestMissingOrStale()/inputSnapshot()。这里只复用刚刚完整验证过的 300 ms，
    // 不跳过首次正确性检查，也不长期记住“新鲜”状态。
    if (isRecentlyValidatedFresh(prepared))
        return true;

    if (!manifestMissingOrStale(project, source, prepared))
    {
        markRecentlyValidatedFresh(prepared);
        return true;
    }

    clearRecentlyValidatedFresh(prepared);
    PostTecplotUtils::clearCache(prepared.absolutePath());
    const QStringList staleFiles =
        prepared.entryList({QStringLiteral("*.tec"), QStringLiteral("*.vtu"),
                            QStringLiteral("*.vtm"), QStringLiteral("*.vtp")}, QDir::Files);
    for (const QString& staleFile : staleFiles)
    {
        const QFileInfo staleInfo(prepared.filePath(staleFile));
        if (staleInfo.suffix().compare(QStringLiteral("vtm"), Qt::CaseInsensitive) == 0)
        {
            const QString leafDirectory =
                prepared.filePath(staleInfo.completeBaseName());
            if (QDir(leafDirectory).exists())
                QDir(leafDirectory).removeRecursively();
        }
        prepared.remove(staleFile);
    }
    prepared.remove(QString::fromLatin1(kManifestName));

    PostResultGenerator::Options options;
    options.projectDir = generationProjectDir(project, source).absolutePath();
    options.resultDir = source.absolutePath();
    options.outputDir = prepared.absolutePath();
    options.overwrite = true;
    options.writeLegacyAuxiliaryFiles = false;
    const PostResultGenerator::Result generated = PostResultGenerator::generate(options);
    if (!generated.ok)
    {
        qWarning().noquote()
            << QStringLiteral("__RAW__后处理结果生成失败：%1\n源目录：%2")
                   .arg(generated.error)
                   .arg(QDir::toNativeSeparators(source.absolutePath()));
        return false;
    }
    if (!writeManifest(project, source, prepared, generated))
        return false;
    markRecentlyValidatedFresh(prepared);
    for (const QString& warning : generated.warnings)
        qWarning() << "Post result preparation warning:" << warning;
    return true;
}

bool prepareResultRoot(const QString& projectDir, const QString& resultRoot)
{
    {
        QMutexLocker locker(&recentFreshMutex());
        recentFreshManifests().clear();
    }
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir root(QDir::fromNativeSeparators(resultRoot.trimmed()));
    if (!project.exists() || !root.exists())
        return false;

    const QString projectPath = QDir::fromNativeSeparators(project.absolutePath()).toCaseFolded();
    const QString rootPath = QDir::fromNativeSeparators(root.absolutePath()).toCaseFolded();
    if (rootPath != projectPath && !rootPath.startsWith(projectPath + QLatin1Char('/')))
        return false;

    bool preparedAny = false;
    if (hasMeshFile(root.absolutePath()) && hasSupportedFieldData(root))
        preparedAny = prepareDirectory(project.absolutePath(), root.absolutePath());

    QDirIterator iterator(root.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QDir source(iterator.next());
        if (isPreparedResultPath(project, source.absolutePath()))
            continue;
        if (hasMeshFile(source.absolutePath()) && hasSupportedFieldData(source))
            preparedAny = prepareDirectory(project.absolutePath(), source.absolutePath()) ||
                          preparedAny;
    }
    return preparedAny;
}

bool prepareProject(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return false;

    bool preparedAny = false;
    for (const QString& relativePath : discoverResultDirectories(project.absolutePath()))
    {
        preparedAny = prepareDirectory(project.absolutePath(), project.filePath(relativePath)) ||
                      preparedAny;
    }
    return preparedAny;
}

PreparedResultSummary preparedResultSummary(const QString& projectDir, const QString& resultRoot)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir root(QDir::fromNativeSeparators(resultRoot.trimmed()));
    if (!project.exists() || !root.exists())
        return {};

    PreparedResultSummary summary;
    QSet<QString> seenResultTypes;
    QSet<QString> seenResultConditions;
    const QRegularExpression legacyConditionPattern(QStringLiteral(R"(^(.+?)（(.+)）$)"));
    const QRegularExpression conditionFirstPattern(
        QStringLiteral(R"(^((?:强度储备|荷载系数|超载系数)\s+[-+]?\d+(?:\.\d+)?|方法\s+\d+|派生工况)\s+-\s+(.+)$)"));
    for (const ResultGroup& group : resultGroups(project.absolutePath()))
    {
        if (!pathIsWithin(root.absolutePath(), group.sourcePath))
            continue;

        const QString preparedPath = preparedDirectory(project.absolutePath(), group.sourcePath);
        bool hasResults = false;
        QSet<QString> seenInGroup;
        for (const ResultFile& file : readManifestFiles(QDir(preparedPath)))
        {
            QString resultType = file.displayName.trimmed();
            if (resultType.isEmpty() || seenInGroup.contains(resultType))
                continue;
            seenInGroup.insert(resultType);
            hasResults = true;

            QString resultCondition;
            const QRegularExpressionMatch conditionFirstMatch =
                conditionFirstPattern.match(resultType);
            if (conditionFirstMatch.hasMatch())
            {
                resultCondition = conditionFirstMatch.captured(1).trimmed();
                resultType = conditionFirstMatch.captured(2).trimmed();
            }
            else
            {
                const QRegularExpressionMatch legacyMatch =
                    legacyConditionPattern.match(resultType);
                if (legacyMatch.hasMatch())
                {
                    resultType = legacyMatch.captured(1).trimmed();
                    resultCondition = legacyMatch.captured(2).trimmed();
                }
                else
                {
                    summary.hasRegularResults = true;
                }
            }

            if (!resultCondition.isEmpty() &&
                !seenResultConditions.contains(resultCondition))
            {
                seenResultConditions.insert(resultCondition);
                summary.resultConditions.push_back(resultCondition);
            }

            if (!resultType.isEmpty() && !seenResultTypes.contains(resultType))
            {
                seenResultTypes.insert(resultType);
                summary.resultTypes.push_back(resultType);
            }
        }
        if (hasResults)
            ++summary.groupCount;
    }
    return summary;
}

QVector<ResultFile> resultFiles(const QString& projectDir, const QString& sourceDir, bool includeVtu)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    if (!project.exists() || !source.exists())
        return {};

    auto filterBareGrid = [includeVtu](QVector<ResultFile> files)
    {
        if (!includeVtu)
        {
            // 自动准备目录内部的标准 VTM/VTU（kind=displacement/stress/...）仍保留；
            // 只过滤用户额外导入、kind 本身就是格式名的裸网格文件。
            files.erase(std::remove_if(files.begin(), files.end(), [](const ResultFile& file)
            {
                const bool bareVtu =
                    file.format.compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0 &&
                    file.kind.compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0;
                const bool bareVtm =
                    file.format.compare(QStringLiteral("vtm"), Qt::CaseInsensitive) == 0 &&
                    file.kind.compare(QStringLiteral("vtm"), Qt::CaseInsensitive) == 0;
                return bareVtu || bareVtm;
            }), files.end());
        }
        return files;
    };

    // 结果选择/页面加载属于纯读取路径。只要已有 manifest 对应的结果文件仍存在，
    // 立即返回，不在 GUI 线程同步做 inputSnapshot + stale 检查 + 结果重建。
    // 求解完成后由 prepareResultRootAsync() 负责刷新；没有任何已准备结果时，
    // 才保留一次同步 prepare 作为旧工程兼容兜底。
    const QString preparedPath = preparedDirectory(project.absolutePath(), source.absolutePath());
    if (!preparedPath.isEmpty())
    {
        const QDir preparedDir(preparedPath);
        if (isRecentlyValidatedFresh(preparedDir) ||
            !manifestMissingOrStale(project, source, preparedDir))
        {
            markRecentlyValidatedFresh(preparedDir);
            QVector<ResultFile> files = filterBareGrid(readManifestFiles(preparedDir));
            if (!files.isEmpty())
                return files;
        }

        if (QFileInfo::exists(preparedDir.filePath(QString::fromLatin1(kManifestName))))
        {
            prepareDirectory(project.absolutePath(), source.absolutePath());
            QVector<ResultFile> files = filterBareGrid(readManifestFiles(preparedDir));
            if (!files.isEmpty())
                return files;
        }
    }

    // 如果求解器目录本身已经有可直接读取的结果，也优先显示，避免为了填下拉框
    // 先同步生成缓存。
    QVector<ResultFile> directFiles =
        filterBareGrid(fallbackSourceFiles(project, source, includeVtu));
    if (!directFiles.isEmpty())
        return directFiles;

    // 只有“既没有准备缓存，也没有直接结果”时才尝试同步生成。
    prepareDirectory(project.absolutePath(), source.absolutePath());
    if (!preparedPath.isEmpty())
    {
        QVector<ResultFile> files = filterBareGrid(readManifestFiles(QDir(preparedPath)));
        if (!files.isEmpty())
            return files;
    }
    return {};
}

QVector<ResultFrame> resultFrames(const QString& projectDir, const QString& sourceDir,
                                  bool includeVtu)
{
    const QVector<ResultFile> files = resultFiles(projectDir, sourceDir, includeVtu);
    QVector<ResultFrame> frames;
    QSet<QString> seen;
    bool hasStatic = false;

    for (const ResultFile& file : files)
    {
        if (file.frameKey.isEmpty())
        {
            hasStatic = true;
            continue;
        }
        if (seen.contains(file.frameKey))
            continue;
        seen.insert(file.frameKey);

        ResultFrame frame;
        frame.key = file.frameKey;
        frame.displayName = file.frameLabel.isEmpty()
                                ? QStringLiteral("计算时刻索引 %1").arg(file.frameOrder)
                                : file.frameLabel;
        frame.value = file.frameValue;
        frame.unit = file.frameUnit;
        frame.order = file.frameOrder;
        frames.push_back(frame);
    }

    std::sort(frames.begin(), frames.end(), [](const ResultFrame& left,
                                               const ResultFrame& right)
    {
        if (left.order != right.order)
            return left.order < right.order;
        return QString::localeAwareCompare(left.displayName, right.displayName) < 0;
    });

    if (hasStatic || frames.isEmpty())
    {
        bool hasStrengthReductionFrames = false;
        for (const ResultFrame& frame : frames)
        {
            if (frame.key.startsWith(QStringLiteral("condition:strength:")))
            {
                hasStrengthReductionFrames = true;
                break;
            }
        }

        ResultFrame current;
        current.displayName = hasStrengthReductionFrames
                                  ? QStringLiteral("基准状态")
                                  : QStringLiteral("当前状态");
        current.order = -1;
        frames.prepend(current);
    }
    return frames;
}

QVector<ResultFile> resultFilesForFrame(const QString& projectDir, const QString& sourceDir,
                                        const QString& frameKey, bool includeVtu)
{
    QVector<ResultFile> files = resultFiles(projectDir, sourceDir, includeVtu);
    files.erase(std::remove_if(files.begin(), files.end(),
                               [&](const ResultFile& file)
    {
        return file.frameKey != frameKey;
    }), files.end());
    return files;
}
} // namespace PostResultAutoPrepare
