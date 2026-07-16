#include "PostResultAutoPrepare.h"

#include "PostResultGenerator.h"

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
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace
{
constexpr char kPostResultRoot[] = "post/results";
constexpr char kManifestName[] = "manifest.json";
constexpr int kManifestVersion = 3;

struct GeneratedFileInfo
{
    QString displayName;
    QString kind;
};

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

QString cacheDirName(const QString& relativePath)
{
    QString name = normalizeRelativePath(relativePath);
    name.replace(QLatin1Char('/'), QStringLiteral("__"));
    name.replace(QLatin1Char('\\'), QStringLiteral("__"));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("root") : name;
}

QDir postRootDir(const QDir& projectDir)
{
    return QDir(projectDir.filePath(QString::fromLatin1(kPostResultRoot)));
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
    static const QRegularExpression expression(
        QStringLiteral("^(?:INVELO|IIDISP|MODPRI|NODCMM)(?:[-_](?:METHOD\\d+|CZ[^.]*))?\\.TXT$"),
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
            (name.startsWith(QStringLiteral("ZONE"), Qt::CaseInsensitive) &&
             name.endsWith(QStringLiteral(".DAT"), Qt::CaseInsensitive)))
            files.push_back(info);
    }
    return files;
}

QList<QFileInfo> generationInputFiles(const QDir& projectDir, const QDir& sourceDir)
{
    QList<QFileInfo> files = sourceFiles(sourceDir);
    const QDir prepDir(projectDir.filePath(QStringLiteral("PREP")));
    if (prepDir.exists())
    {
        const QFileInfoList prepFiles = prepDir.entryInfoList(QDir::Files);
        for (const QFileInfo& info : prepFiles)
        {
            const QString name = info.fileName();
            if (name.compare(QStringLiteral("MAFILESET.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("MA_MASK.DAT"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("MA_MASK_CMM.DAT"), Qt::CaseInsensitive) == 0 ||
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
    if (normalized == QStringLiteral("head_pressure") || normalized == QStringLiteral("tec_hp"))
        return QStringLiteral("水头渗压");
    if (normalized == QStringLiteral("velocity") || normalized == QStringLiteral("tec_velo"))
        return QStringLiteral("渗流速度");
    if (normalized == QStringLiteral("displacement") || normalized == QStringLiteral("tec_disp"))
        return QStringLiteral("位移");
    if (normalized == QStringLiteral("principal_stress") || normalized == QStringLiteral("tec_prs"))
        return QStringLiteral("主应力");
    if (normalized == QStringLiteral("stress_level") || normalized == QStringLiteral("tec_sl") ||
        normalized == QStringLiteral("tec_cmm"))
        return QStringLiteral("应力水平");
    if (normalized == QStringLiteral("vtu"))
        return QStringLiteral("网格结果");
    return QStringLiteral("结果数据");
}

QString suffixDisplayName(const QString& suffix)
{
    if (suffix.isEmpty())
        return QString();

    static const QRegularExpression methodExpression(
        QStringLiteral("^method(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch methodMatch = methodExpression.match(suffix);
    if (methodMatch.hasMatch())
        return QStringLiteral("（方法 %1）").arg(methodMatch.captured(1));

    static const QRegularExpression reserveExpression(
        QStringLiteral("^cz\\d+cb(\\d+)$"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch reserveMatch = reserveExpression.match(suffix);
    if (reserveMatch.hasMatch())
    {
        const double factor = reserveMatch.captured(1).toDouble() / 100.0;
        return QStringLiteral("（强度储备 %1）").arg(factor, 0, 'f', 2);
    }
    return QStringLiteral("（派生工况）");
}

GeneratedFileInfo generatedFileInfo(const QString& fileName)
{
    const QString base = QFileInfo(fileName).completeBaseName().toLower();
    const QStringList kinds = {QStringLiteral("head_pressure"), QStringLiteral("velocity"),
                               QStringLiteral("displacement"), QStringLiteral("principal_stress"),
                               QStringLiteral("stress_level")};
    for (const QString& kind : kinds)
    {
        if (base == kind || base.startsWith(kind + QLatin1Char('_')))
        {
            const QString suffix = base == kind ? QString() : base.mid(kind.size() + 1);
            return {baseDisplayName(kind) + suffixDisplayName(suffix), kind};
        }
    }
    return {QStringLiteral("结果数据"), base};
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
        if (fileName.isEmpty() || !QFileInfo::exists(preparedDir.filePath(fileName)))
            return true;
    }
    return false;
}

bool writeManifest(const QDir& projectDir, const QDir& sourceDir, const QDir& preparedDir,
                   const PostResultGenerator::Result& generated)
{
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
        if (info.dir().absolutePath().compare(preparedDir.absolutePath(), Qt::CaseInsensitive) != 0 ||
            info.suffix().compare(QStringLiteral("tec"), Qt::CaseInsensitive) != 0)
            continue;

        const GeneratedFileInfo metadata = generatedFileInfo(info.fileName());
        QJsonObject object;
        object.insert(QStringLiteral("displayName"), metadata.displayName);
        object.insert(QStringLiteral("fileName"), info.fileName());
        object.insert(QStringLiteral("kind"), metadata.kind);
        object.insert(QStringLiteral("format"), QStringLiteral("tecplot"));
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

QVector<PostResultAutoPrepare::ResultFile> readManifestFiles(const QDir& preparedDir)
{
    QFile file(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject())
        return {};

    QVector<PostResultAutoPrepare::ResultFile> result;
    const QJsonArray files = document.object().value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : files)
    {
        const QJsonObject object = value.toObject();
        const QString fileName = object.value(QStringLiteral("fileName")).toString();
        const QFileInfo info(preparedDir.filePath(fileName));
        if (fileName.isEmpty() || !info.exists() || !info.isFile())
            continue;

        PostResultAutoPrepare::ResultFile entry;
        entry.kind = object.value(QStringLiteral("kind")).toString();
        entry.displayName = object.value(QStringLiteral("displayName")).toString();
        if (entry.displayName.isEmpty())
            entry.displayName = baseDisplayName(entry.kind);
        entry.filePath = info.absoluteFilePath();
        entry.format = object.value(QStringLiteral("format")).toString(QStringLiteral("tecplot"));
        result.push_back(entry);
    }
    return result;
}

QVector<PostResultAutoPrepare::ResultFile> fallbackSourceFiles(const QDir& sourceDir,
                                                               bool includeVtu)
{
    QVector<PostResultAutoPrepare::ResultFile> result;
    const QStringList names = {QStringLiteral("TEC_HP.DAT"), QStringLiteral("TEC_VELO.DAT"),
                               QStringLiteral("TEC_DISP.DAT"), QStringLiteral("TEC_PRS.DAT"),
                               QStringLiteral("TEC_SL.DAT"), QStringLiteral("TEC_CMM.DAT"),
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

            const GeneratedFileInfo metadata = generatedFileInfo(fileName);
            PostResultAutoPrepare::ResultFile entry;
            entry.displayName = metadata.displayName;
            entry.filePath = info.absoluteFilePath();
            entry.kind = metadata.kind;
            entry.format = QStringLiteral("tecplot");
            result.push_back(entry);
        }
    }

    if (includeVtu)
    {
        const QStringList files = sourceDir.entryList({QStringLiteral("*.vtu")}, QDir::Files,
                                                      QDir::Name);
        for (int i = 0; i < files.size(); ++i)
        {
            PostResultAutoPrepare::ResultFile entry;
            entry.displayName = files.size() == 1 ? QStringLiteral("网格结果")
                                                   : QStringLiteral("网格结果 %1").arg(i + 1);
            entry.filePath = QFileInfo(sourceDir.filePath(files.at(i))).absoluteFilePath();
            entry.kind = QStringLiteral("vtu");
            entry.format = QStringLiteral("vtu");
            result.push_back(entry);
        }
    }
    return result;
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
    const QString postRoot = postRootDir(project).absolutePath().toLower();

    QDirIterator iterator(project.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext())
    {
        const QString absolutePath = QDir(iterator.next()).absolutePath();
        if (absolutePath.toLower().startsWith(postRoot))
            continue;
        const QDir source(absolutePath);
        if (hasMeshFile(absolutePath) && hasSupportedFieldData(source))
            appendUnique(result, seen, project.relativeFilePath(absolutePath));
    }

    std::sort(result.begin(), result.end(), [](const QString& left, const QString& right)
    {
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

QString resultGroupDisplayName(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    const QString relative = sourceRelativePath(project, source.absolutePath()).toUpper();
    const QString leaf = QFileInfo(source.absolutePath()).fileName().toUpper();

    static const QRegularExpression supportStage(QStringLiteral("^J(\\d+)$"));
    static const QRegularExpression excavationStage(QStringLiteral("^E(\\d+)$"));
    QRegularExpressionMatch match = supportStage.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("开挖加固第 %1 阶段").arg(match.captured(1).toInt());
    match = excavationStage.match(leaf);
    if (match.hasMatch())
        return QStringLiteral("开挖第 %1 阶段").arg(match.captured(1).toInt());

    if (hasFilePrefix(source, QStringLiteral("INVELO")) || relative.contains(QStringLiteral("SEEP")))
        return QStringLiteral("渗流结果");
    if (relative.contains(QStringLiteral("GEOSTR")))
        return QStringLiteral("初始地应力");
    if (relative.contains(QStringLiteral("FILL")))
        return QStringLiteral("填筑蓄水结果");
    if (relative.contains(QStringLiteral("PACK")))
        return QStringLiteral("分层填筑结果");
    if (relative.contains(QStringLiteral("JALL")))
        return QStringLiteral("开挖加固汇总结果");
    if (relative.contains(QStringLiteral("EALL")))
        return QStringLiteral("开挖汇总结果");

    const QString projectHint = project.absolutePath().toUpper();
    if (projectHint.contains(QStringLiteral("SLOP")))
        return QStringLiteral("边坡结果");
    if (projectHint.contains(QStringLiteral("CAVE")))
        return QStringLiteral("洞室结果");
    if (hasFile(source, QStringLiteral("IIDISP.TXT")) ||
        hasFilePrefix(source, QStringLiteral("MODPRI")) ||
        hasFilePrefix(source, QStringLiteral("NODCMM")))
        return QStringLiteral("结构计算结果");
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

        QString displayName = resultGroupDisplayName(project.absolutePath(), absolutePath);
        const int count = displayCounts.value(displayName) + 1;
        displayCounts.insert(displayName, count);
        if (count > 1)
            displayName += QStringLiteral(" %1").arg(count);
        groups.push_back({displayName, absolutePath});
    }
    return groups;
}

QString preparedDirectory(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return QString();
    const QString relative = sourceRelativePath(project, sourceDir);
    return relative.isEmpty() ? QString() : postRootDir(project).filePath(cacheDirName(relative));
}

bool prepareDirectory(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    if (!project.exists() || !source.exists() || !hasMeshFile(source.absolutePath()) ||
        !hasSupportedFieldData(source))
        return false;

    QDir prepared(preparedDirectory(project.absolutePath(), source.absolutePath()));
    if (prepared.path().isEmpty() || (!prepared.exists() && !prepared.mkpath(QStringLiteral("."))))
        return false;
    if (!manifestMissingOrStale(project, source, prepared))
        return true;

    const QStringList staleFiles = prepared.entryList({QStringLiteral("*.tec")}, QDir::Files);
    for (const QString& staleFile : staleFiles)
        prepared.remove(staleFile);
    prepared.remove(QString::fromLatin1(kManifestName));

    PostResultGenerator::Options options;
    options.projectDir = project.absolutePath();
    options.resultDir = source.absolutePath();
    options.outputDir = prepared.absolutePath();
    options.overwrite = true;
    options.writeLegacyAuxiliaryFiles = false;
    const PostResultGenerator::Result generated = PostResultGenerator::generate(options);
    if (!generated.ok)
    {
        qWarning() << "Post result preparation failed:" << generated.error;
        return false;
    }
    if (!writeManifest(project, source, prepared, generated))
        return false;
    for (const QString& warning : generated.warnings)
        qWarning() << "Post result preparation warning:" << warning;
    return true;
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

QVector<ResultFile> resultFiles(const QString& projectDir, const QString& sourceDir, bool includeVtu)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    if (!project.exists() || !source.exists())
        return {};

    prepareDirectory(project.absolutePath(), source.absolutePath());
    const QString preparedPath = preparedDirectory(project.absolutePath(), source.absolutePath());
    if (!preparedPath.isEmpty())
    {
        QVector<ResultFile> files = readManifestFiles(QDir(preparedPath));
        if (!includeVtu)
        {
            files.erase(std::remove_if(files.begin(), files.end(), [](const ResultFile& file)
            {
                return file.format.compare(QStringLiteral("vtu"), Qt::CaseInsensitive) == 0;
            }), files.end());
        }
        if (!files.isEmpty())
            return files;
    }
    return fallbackSourceFiles(source, includeVtu);
}
} // namespace PostResultAutoPrepare
