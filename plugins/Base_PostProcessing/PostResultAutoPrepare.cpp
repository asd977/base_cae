#include "PostResultAutoPrepare.h"

#include "PostResultGenerator.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>

namespace
{
constexpr char kPostResultRoot[] = "post/results";
constexpr char kManifestName[] = "manifest.json";

struct OutputSpec
{
    QString fileName;
    QString displayName;
    QString kind;
    QString format;
    QStringList sourceNames;
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
    const QString rel = normalizeRelativePath(projectDir.relativeFilePath(QDir(sourceDir).absolutePath()));
    return rel.startsWith(QStringLiteral("..")) ? QString() : rel;
}

QString cacheDirName(const QString& relPath)
{
    QString name = normalizeRelativePath(relPath);
    name.replace(QLatin1Char('/'), QStringLiteral("__"));
    name.replace(QLatin1Char('\\'), QStringLiteral("__"));
    name.replace(QLatin1Char(':'), QLatin1Char('_'));
    return name.isEmpty() ? QStringLiteral("root") : name;
}

QDir postRootDir(const QDir& projectDir)
{
    return QDir(projectDir.filePath(QString::fromLatin1(kPostResultRoot)));
}

QVector<OutputSpec> outputSpecs(bool seepage)
{
    QVector<OutputSpec> specs;
    if (seepage)
    {
        specs.push_back({QStringLiteral("head_pressure.tec"), QStringLiteral("水头渗压"),
                         QStringLiteral("head_pressure"), QStringLiteral("tecplot"),
                         {QStringLiteral("IIDISP.TXT")}});
        specs.push_back({QStringLiteral("velocity.tec"), QStringLiteral("渗流速度"),
                         QStringLiteral("velocity"), QStringLiteral("tecplot"),
                         {QStringLiteral("INVELO.TXT")}});
    }
    else
    {
        specs.push_back({QStringLiteral("displacement.tec"), QStringLiteral("位移"),
                         QStringLiteral("displacement"), QStringLiteral("tecplot"),
                         {QStringLiteral("IIDISP.TXT")}});
    }

    specs.push_back({QStringLiteral("principal_stress.tec"), QStringLiteral("主应力"),
                     QStringLiteral("principal_stress"), QStringLiteral("tecplot"),
                     {QStringLiteral("MODPRI.TXT")}});
    specs.push_back({QStringLiteral("stress_level.tec"), QStringLiteral("应力水平"),
                     QStringLiteral("stress_level"), QStringLiteral("tecplot"),
                     {QStringLiteral("NODCMM.TXT")}});
    return specs;
}

QString displayNameForKind(const QString& kind)
{
    const QString normalized = kind.trimmed().toLower();
    if (normalized == QStringLiteral("head_pressure") || normalized == QStringLiteral("tec_hp"))
        return QStringLiteral("水头渗压");
    if (normalized == QStringLiteral("velocity") || normalized == QStringLiteral("tec_velo"))
        return QStringLiteral("渗流速度");
    if (normalized == QStringLiteral("displacement") || normalized == QStringLiteral("tec_disp"))
        return QStringLiteral("位移");
    if (normalized == QStringLiteral("principal_stress") || normalized == QStringLiteral("tec_prs"))
        return QStringLiteral("主应力");
    if (normalized == QStringLiteral("stress_level") ||
        normalized == QStringLiteral("stress_level_cmm") ||
        normalized == QStringLiteral("stress_level_sl") ||
        normalized == QStringLiteral("tec_cmm") || normalized == QStringLiteral("tec_sl"))
        return QStringLiteral("应力水平");
    if (normalized == QStringLiteral("vtu"))
        return QStringLiteral("网格结果");
    return QStringLiteral("结果数据");
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

bool hasMeshFile(const QString& absPath)
{
    return findFileInfo(QDir(absPath), QStringLiteral("IIIN.DAT"));
}

bool hasSupportedFieldData(const QDir& dir)
{
    return findFileInfo(dir, QStringLiteral("IIDISP.TXT")) ||
           findFileInfo(dir, QStringLiteral("INVELO.TXT")) ||
           findFileInfo(dir, QStringLiteral("MODPRI.TXT")) ||
           findFileInfo(dir, QStringLiteral("NODCMM.TXT"));
}

bool hasFile(const QDir& dir, const QString& name)
{
    return findFileInfo(dir, name);
}

void appendUnique(QStringList& out, QSet<QString>& seen, const QString& relPath)
{
    const QString normalized = normalizeRelativePath(relPath);
    if (normalized.isEmpty() || seen.contains(normalized))
        return;

    seen.insert(normalized);
    out << normalized;
}

QDateTime newestTime(const QList<QFileInfo>& sources)
{
    QDateTime newest;
    for (const QFileInfo& source : sources)
    {
        if (!source.exists())
            continue;
        const QDateTime modified = source.lastModified();
        if (!newest.isValid() || modified > newest)
            newest = modified;
    }
    return newest;
}

QList<QFileInfo> sourceInfos(const QDir& sourceDir, const QStringList& names)
{
    QList<QFileInfo> infos;
    QFileInfo mesh;
    if (findFileInfo(sourceDir, QStringLiteral("IIIN.DAT"), &mesh))
        infos << mesh;

    for (const QString& name : names)
    {
        QFileInfo info;
        if (findFileInfo(sourceDir, name, &info))
            infos << info;
    }
    return infos;
}

bool outputMissingOrStale(const QDir& sourceDir, const QDir& preparedDir, const OutputSpec& spec)
{
    bool hasRequiredSource = false;
    for (const QString& name : spec.sourceNames)
    {
        if (hasFile(sourceDir, name))
        {
            hasRequiredSource = true;
            break;
        }
    }
    if (!hasRequiredSource)
        return false;

    const QFileInfo output(preparedDir.filePath(spec.fileName));
    if (!output.exists() || !output.isFile())
        return true;

    const QDateTime newestSource = newestTime(sourceInfos(sourceDir, spec.sourceNames));
    return newestSource.isValid() && output.lastModified() < newestSource;
}

bool manifestMissingOrStale(const QDir& sourceDir, const QDir& preparedDir,
                            const QVector<OutputSpec>& specs)
{
    QFileInfo manifest(preparedDir.filePath(QString::fromLatin1(kManifestName)));
    if (!manifest.exists() || !manifest.isFile())
        return true;

    for (const OutputSpec& spec : specs)
    {
        if (outputMissingOrStale(sourceDir, preparedDir, spec))
            return true;
    }

    return false;
}

bool writeManifest(const QDir& projectDir, const QDir& sourceDir, const QDir& preparedDir,
                   const QVector<OutputSpec>& specs, const PostResultGenerator::Result& generated)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("sourceDir"), sourceRelativePath(projectDir, sourceDir.absolutePath()));
    root.insert(QStringLiteral("preparedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    QSet<QString> generatedFileNames;
    for (const QString& path : generated.generatedFiles)
        generatedFileNames.insert(QFileInfo(path).fileName());

    QJsonArray files;
    for (const OutputSpec& spec : specs)
    {
        const QFileInfo info(preparedDir.filePath(spec.fileName));
        if (!info.exists() || !info.isFile())
            continue;

        QJsonObject obj;
        obj.insert(QStringLiteral("displayName"), spec.displayName);
        obj.insert(QStringLiteral("fileName"), spec.fileName);
        obj.insert(QStringLiteral("kind"), spec.kind);
        obj.insert(QStringLiteral("format"), spec.format);
        obj.insert(QStringLiteral("size"), static_cast<qint64>(info.size()));
        obj.insert(QStringLiteral("modified"), info.lastModified().toUTC().toString(Qt::ISODate));
        files.append(obj);
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

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};

    QVector<PostResultAutoPrepare::ResultFile> result;
    const QJsonArray files = doc.object().value(QStringLiteral("files")).toArray();
    for (const QJsonValue& value : files)
    {
        const QJsonObject obj = value.toObject();
        const QString fileName = obj.value(QStringLiteral("fileName")).toString();
        if (fileName.isEmpty())
            continue;

        const QFileInfo info(preparedDir.filePath(fileName));
        if (!info.exists() || !info.isFile())
            continue;

        PostResultAutoPrepare::ResultFile entry;
        entry.kind = obj.value(QStringLiteral("kind")).toString();
        entry.displayName = displayNameForKind(entry.kind);
        entry.filePath = info.absoluteFilePath();
        entry.format = obj.value(QStringLiteral("format")).toString(QStringLiteral("tecplot"));
        result.push_back(entry);
    }
    return result;
}

QVector<PostResultAutoPrepare::ResultFile> fallbackSourceFiles(const QDir& sourceDir,
                                                               bool includeVtu)
{
    QVector<PostResultAutoPrepare::ResultFile> result;
    const QStringList names = {
        QStringLiteral("TEC_HP.DAT"),
        QStringLiteral("TEC_VELO.DAT"),
        QStringLiteral("TEC_DISP.DAT"),
        QStringLiteral("TEC_PRS.DAT"),
        QStringLiteral("TEC_CMM.DAT"),
        QStringLiteral("TEC_SL.DAT"),
        QStringLiteral("*.tec"),
    };

    int genericIndex = 0;
    QSet<QString> addedDisplayNames;
    for (const QString& name : names)
    {
        const bool wildcard = name.contains(QLatin1Char('*')) || name.contains(QLatin1Char('?'));
        const QStringList matched =
            wildcard ? sourceDir.entryList(QStringList{name}, QDir::Files, QDir::Name)
                     : (QFileInfo::exists(sourceDir.filePath(name)) ? QStringList{name}
                                                                    : QStringList{});
        for (const QString& fileName : matched)
        {
            QFileInfo info(sourceDir.filePath(fileName));
            PostResultAutoPrepare::ResultFile entry;
            entry.kind = info.completeBaseName().toLower();
            entry.displayName = displayNameForKind(entry.kind);
            if (entry.displayName == QStringLiteral("结果数据"))
                entry.displayName = QStringLiteral("结果数据 %1").arg(++genericIndex);
            else if (addedDisplayNames.contains(entry.displayName))
                continue;
            addedDisplayNames.insert(entry.displayName);
            entry.filePath = info.absoluteFilePath();
            entry.format = QStringLiteral("tecplot");
            result.push_back(entry);
        }
    }

    if (includeVtu)
    {
        const QStringList vtus = sourceDir.entryList(QStringList{QStringLiteral("*.vtu")},
                                                     QDir::Files, QDir::Name);
        for (int i = 0; i < vtus.size(); ++i)
        {
            const QString& fileName = vtus.at(i);
            QFileInfo info(sourceDir.filePath(fileName));
            PostResultAutoPrepare::ResultFile entry;
            entry.displayName = vtus.size() == 1
                                    ? QStringLiteral("网格结果")
                                    : QStringLiteral("网格结果 %1").arg(i + 1);
            entry.filePath = info.absoluteFilePath();
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
    return {
        QStringLiteral("SEEPCASE01"),
        QStringLiteral("CASE01"),
        QStringLiteral("GEOSTR"),
        QStringLiteral("CASE01EALL"),
        QStringLiteral("CASE01JALL"),
        QStringLiteral("CASE01FILL01"),
        QStringLiteral("CASE01JACA/J03"),
    };
}

QStringList discoverResultDirectories(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return {};

    QStringList result;
    QSet<QString> seen;

    for (const QString& relPath : defaultResultDirectories())
    {
        const QString normalized = normalizeRelativePath(relPath);
        const QDir source(project.filePath(normalized));
        if (hasMeshFile(source.absolutePath()) && hasSupportedFieldData(source))
            appendUnique(result, seen, normalized);
    }
    return result;
}

QString firstResultDirectory(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return projectDir;

    if (hasMeshFile(project.absolutePath()) && hasSupportedFieldData(project))
        return project.absolutePath();

    const QStringList dirs = discoverResultDirectories(project.absolutePath());
    if (dirs.isEmpty())
        return project.absolutePath();

    return project.filePath(dirs.first());
}

QString resultGroupDisplayName(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    const QString rel = sourceRelativePath(project, source.absolutePath()).toUpper();
    const QString projectHint = project.absolutePath().toUpper();

    if (hasFile(source, QStringLiteral("INVELO.TXT")) || rel.contains(QStringLiteral("SEEP")))
        return QStringLiteral("渗流结果");
    if (rel.contains(QStringLiteral("GEOSTR")))
        return QStringLiteral("初始地应力");
    if (rel.contains(QStringLiteral("FILL")))
        return QStringLiteral("填筑结果");
    if (rel.contains(QStringLiteral("JACA")))
        return QStringLiteral("开挖阶段结果");
    if (rel.contains(QStringLiteral("JALL")))
        return QStringLiteral("开挖汇总结果");
    if (rel.contains(QStringLiteral("EALL")))
        return QStringLiteral("开挖结果");
    if (projectHint.contains(QStringLiteral("SLOP")))
        return QStringLiteral("边坡结果");
    if (projectHint.contains(QStringLiteral("CAVE")))
        return QStringLiteral("洞室结果");
    if (hasFile(source, QStringLiteral("IIDISP.TXT")) ||
        hasFile(source, QStringLiteral("MODPRI.TXT")) ||
        hasFile(source, QStringLiteral("NODCMM.TXT")))
        return QStringLiteral("结构结果");
    return QStringLiteral("计算结果");
}

QVector<ResultGroup> resultGroups(const QString& projectDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return {};

    QStringList sourcePaths;
    if (hasMeshFile(project.absolutePath()) && hasSupportedFieldData(project))
        sourcePaths << project.absolutePath();
    for (const QString& relPath : discoverResultDirectories(project.absolutePath()))
        sourcePaths << project.filePath(relPath);

    QVector<ResultGroup> groups;
    QHash<QString, int> displayCounts;
    QSet<QString> seenPaths;
    for (const QString& sourcePath : sourcePaths)
    {
        const QString absolutePath = QDir(sourcePath).absolutePath();
        const QString pathKey = absolutePath.toLower();
        if (seenPaths.contains(pathKey))
            continue;
        seenPaths.insert(pathKey);

        QString displayName = resultGroupDisplayName(project.absolutePath(), absolutePath);
        const int count = ++displayCounts[displayName];
        if (count > 1)
            displayName = QStringLiteral("%1（%2）").arg(displayName).arg(count);
        groups.push_back({displayName, absolutePath});
    }
    return groups;
}

QString preparedDirectory(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    if (!project.exists())
        return QString();

    const QString rel = sourceRelativePath(project, sourceDir);
    if (rel.isEmpty())
        return QString();

    return postRootDir(project).filePath(cacheDirName(rel));
}

bool prepareDirectory(const QString& projectDir, const QString& sourceDir)
{
    const QDir project(QDir::fromNativeSeparators(projectDir.trimmed()));
    const QDir source(QDir::fromNativeSeparators(sourceDir.trimmed()));
    if (!project.exists() || !source.exists())
        return false;

    if (!findFileInfo(source, QStringLiteral("IIIN.DAT")))
        return false;

    const bool seepage = hasFile(source, QStringLiteral("INVELO.TXT"));
    const QVector<OutputSpec> specs = outputSpecs(seepage);

    QDir prepared(preparedDirectory(project.absolutePath(), source.absolutePath()));
    if (prepared.path().isEmpty())
        return false;
    if (!prepared.exists() && !prepared.mkpath(QStringLiteral(".")))
        return false;

    if (!manifestMissingOrStale(source, prepared, specs))
        return true;

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

    if (!writeManifest(project, source, prepared, specs, generated))
    {
        qWarning() << "Post result manifest write failed:" << prepared.absolutePath();
        return false;
    }

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
    const QStringList dirs = discoverResultDirectories(project.absolutePath());
    for (const QString& relPath : dirs)
    {
        const QString source = project.filePath(relPath);
        preparedAny = prepareDirectory(project.absolutePath(), source) || preparedAny;
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
        QVector<ResultFile> preparedFiles = readManifestFiles(QDir(preparedPath));
        if (!includeVtu)
        {
            preparedFiles.erase(std::remove_if(preparedFiles.begin(), preparedFiles.end(),
                                               [](const ResultFile& file)
                                               {
                                                   return file.format.compare(QStringLiteral("vtu"),
                                                                              Qt::CaseInsensitive) == 0;
                                               }),
                                preparedFiles.end());
        }
        if (!preparedFiles.isEmpty())
            return preparedFiles;
    }

    return fallbackSourceFiles(source, includeVtu);
}
} // namespace PostResultAutoPrepare
