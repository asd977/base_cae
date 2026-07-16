#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace PostResultAutoPrepare
{
struct ResultFile
{
    QString displayName;
    QString filePath;
    QString kind;
    QString format;
};

struct ResultGroup
{
    QString displayName;
    QString sourcePath;
};

QStringList defaultResultDirectories();
QStringList discoverResultDirectories(const QString& projectDir);
QString firstResultDirectory(const QString& projectDir);
QString resultGroupDisplayName(const QString& projectDir, const QString& sourceDir);
QVector<ResultGroup> resultGroups(const QString& projectDir);
QString preparedDirectory(const QString& projectDir, const QString& sourceDir);
bool prepareDirectory(const QString& projectDir, const QString& sourceDir);
bool prepareResultRoot(const QString& projectDir, const QString& resultRoot);
bool prepareProject(const QString& projectDir);
QStringList preparedResultDescriptions(const QString& projectDir, const QString& resultRoot);
QVector<ResultFile> resultFiles(const QString& projectDir, const QString& sourceDir,
                                bool includeVtu = true);
}
