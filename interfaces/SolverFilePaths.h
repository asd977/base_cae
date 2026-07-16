#ifndef SOLVERFILEPATHS_H
#define SOLVERFILEPATHS_H

#include <PublicConstants.h>
#include <PublicVariables.h>

#include <QDir>
#include <QString>

namespace SolverFilePaths
{

inline QString projectInputDirectory(const QString& projectDir)
{
    return QDir(projectDir).filePath(
        QString::fromUtf8(PublicConstants::kProjectInputDirectoryName));
}

inline QString analysisInputDirectory(const QString& analysisDir)
{
    return QDir(analysisDir).filePath(
        QString::fromUtf8(PublicConstants::kAnalysisInputDirectoryName));
}

inline QString runDirectory()
{
    return PublicVariables::instance()
        .get(PublicConstants::kSolverRunDirectoryKey)
        .toString()
        .trimmed();
}

inline QString outputRoot(const QString& projectDir)
{
    const QString runDir = runDirectory();
    return runDir.isEmpty() ? QDir(projectDir).absolutePath()
                            : QDir(runDir).absolutePath();
}

inline QString outputInputDirectory(const QString& projectDir)
{
    const QString runDir = runDirectory();
    return runDir.isEmpty() ? projectInputDirectory(projectDir)
                            : analysisInputDirectory(runDir);
}

} // namespace SolverFilePaths

#endif // SOLVERFILEPATHS_H
