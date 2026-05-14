#include "ResultRuleManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ResultRuleManager& ResultRuleManager::instance()
{
    static ResultRuleManager manager;
    return manager;
}

void ResultRuleManager::ensureLoaded()
{
    if (m_loaded)
        return;

    if (!load())
    {
        m_directoryRules = defaultDirectoryRules();
        m_fileRules = defaultFileRules();
        m_loaded = true;
        save();
    }
}

QVector<ResultDirectoryRule> ResultRuleManager::directoryRules() const
{
    return m_directoryRules;
}

QVector<ResultFileRule> ResultRuleManager::fileRules() const
{
    return m_fileRules;
}

void ResultRuleManager::setDirectoryRules(const QVector<ResultDirectoryRule>& rules)
{
    m_directoryRules = rules;
    m_loaded = true;
}

void ResultRuleManager::setFileRules(const QVector<ResultFileRule>& rules)
{
    m_fileRules = rules;
    m_loaded = true;
}

QString ResultRuleManager::configFilePath() const
{
    QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    if (baseDir.trimmed().isEmpty())
    {
        const QString appName = QCoreApplication::applicationName().trimmed().isEmpty()
                                    ? QStringLiteral("BaseCAE")
                                    : QCoreApplication::applicationName().trimmed();
        baseDir = QDir::home().filePath(QStringLiteral(".%1").arg(appName));
    }

    QDir dir(baseDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    return dir.filePath(QStringLiteral("post_result_rules.json"));
}

bool ResultRuleManager::load()
{
    const QString path = configFilePath();
    QFile file(path);

    if (!file.exists())
        return false;

    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray bytes = file.readAll();
    file.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);

    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();

    QVector<ResultDirectoryRule> dirs;
    const QJsonArray dirArray = root.value(QStringLiteral("directories")).toArray();

    for (const QJsonValue& v : dirArray)
    {
        const QJsonObject obj = v.toObject();

        ResultDirectoryRule rule;
        rule.dirName = obj.value(QStringLiteral("dirName")).toString().trimmed();
        rule.displayName = obj.value(QStringLiteral("displayName")).toString().trimmed();
        rule.description = obj.value(QStringLiteral("description")).toString().trimmed();

        if (rule.dirName.isEmpty())
            continue;

        if (rule.displayName.isEmpty())
            rule.displayName = rule.dirName;

        dirs.push_back(rule);
    }

    QVector<ResultFileRule> files;
    const QJsonArray fileArray = root.value(QStringLiteral("files")).toArray();

    for (const QJsonValue& v : fileArray)
    {
        const QJsonObject obj = v.toObject();

        ResultFileRule rule;
        rule.fileName = obj.value(QStringLiteral("fileName")).toString().trimmed();
        rule.displayName = obj.value(QStringLiteral("displayName")).toString().trimmed();
        rule.description = obj.value(QStringLiteral("description")).toString().trimmed();
        rule.format = obj.value(QStringLiteral("format")).toString().trimmed().toLower();

        if (rule.fileName.isEmpty())
            continue;

        if (rule.displayName.isEmpty())
            rule.displayName = rule.fileName;

        if (rule.format.isEmpty())
        {
            rule.format = rule.fileName.endsWith(QStringLiteral(".vtu"), Qt::CaseInsensitive)
                              ? QStringLiteral("vtu")
                              : QStringLiteral("tecplot");
        }

        files.push_back(rule);
    }

    if (dirs.isEmpty())
        dirs = defaultDirectoryRules();

    if (files.isEmpty())
        files = defaultFileRules();

    m_directoryRules = dirs;
    m_fileRules = files;
    m_loaded = true;

    return true;
}

bool ResultRuleManager::save() const
{
    const QString path = configFilePath();

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return false;

    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);

    QJsonArray dirArray;
    for (const ResultDirectoryRule& rule : m_directoryRules)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("dirName"), rule.dirName);
        obj.insert(QStringLiteral("displayName"), rule.displayName);
        obj.insert(QStringLiteral("description"), rule.description);
        dirArray.append(obj);
    }
    root.insert(QStringLiteral("directories"), dirArray);

    QJsonArray fileArray;
    for (const ResultFileRule& rule : m_fileRules)
    {
        QJsonObject obj;
        obj.insert(QStringLiteral("fileName"), rule.fileName);
        obj.insert(QStringLiteral("displayName"), rule.displayName);
        obj.insert(QStringLiteral("description"), rule.description);
        obj.insert(QStringLiteral("format"), rule.format);
        fileArray.append(obj);
    }
    root.insert(QStringLiteral("files"), fileArray);

    const QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    return true;
}

bool ResultRuleManager::resetToDefault()
{
    m_directoryRules = defaultDirectoryRules();
    m_fileRules = defaultFileRules();
    m_loaded = true;
    return save();
}

QVector<ResultDirectoryRule> ResultRuleManager::defaultDirectoryRules() const
{
    return {
        {QStringLiteral("SEEPCASE01"), QStringLiteral("渗流结果"), QStringLiteral("渗流计算结果目录")},
        {QStringLiteral("CASE01FILL01"), QStringLiteral("土石坝结果"), QStringLiteral("土石坝填筑/坝体计算结果目录")},
        {QStringLiteral("GEOSTR"), QStringLiteral("地应力回归"), QStringLiteral("地应力回归计算结果目录")},
        {QStringLiteral("CASE01JACA/J03"), QStringLiteral("开挖结果J03"), QStringLiteral("开挖结果J03目录")},
        {QStringLiteral("CASE01"), QStringLiteral("边坡稳定"), QStringLiteral("边坡稳定结果目录")}
    };
}

QVector<ResultFileRule> ResultRuleManager::defaultFileRules() const
{
    return {
        {QStringLiteral("TEC_HP.DAT"), QStringLiteral("水头渗压"), QStringLiteral("水头、渗压结果"), QStringLiteral("tecplot")},
        {QStringLiteral("TEC_VELO.DAT"), QStringLiteral("渗流速度"), QStringLiteral("渗流速度分量"), QStringLiteral("tecplot")},
        {QStringLiteral("TEC_DISP.DAT"), QStringLiteral("位移"), QStringLiteral("位移结果"), QStringLiteral("tecplot")},
        {QStringLiteral("TEC_PRS.DAT"), QStringLiteral("主应力"), QStringLiteral("主应力分量"), QStringLiteral("tecplot")},
        {QStringLiteral("TEC_CMM.DAT"), QStringLiteral("应力水平"), QStringLiteral("应力水平结果"), QStringLiteral("tecplot")},
        {QStringLiteral("TEC_SL.DAT"), QStringLiteral("应力水平"), QStringLiteral("应力水平结果"), QStringLiteral("tecplot")},
        {QStringLiteral("*.vtu"), QStringLiteral("VTU结果"), QStringLiteral("VTK 非结构网格结果"), QStringLiteral("vtu")}
    };
}
