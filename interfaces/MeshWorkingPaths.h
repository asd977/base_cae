#pragma once

// Writable, per-project meshing workspace.  The installation directory contains
// binaries and must never be used as a scratch/output directory.
// The same deterministic paths are used by Base_Geometry and Base_Mesh,
// including their separately loaded plugin DLLs.
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QFile>
#include <QFileInfo>

namespace MeshWorkingPaths {
inline QString root()
{
    const QString project = PublicVariables::instance()
        .get(PublicConstants::kProjectDirectoryKey).toString().trimmed();
    // The active project is the authoritative owner of CAD snapshots and mesh
    // scratch files.  Do not send a saved project's data to AppData: another
    // install or machine should not silently reuse a stale CAD snapshot.
    if (!project.isEmpty()) {
        const QString runName = QStringLiteral("run-%1")
            .arg(QCoreApplication::applicationPid());
        return QDir(QDir(project).absolutePath())
            .filePath(QStringLiteral(".meshing-cache/%1").arg(runName));
    }

    // There is no project directory during startup/tests.  Use an isolated
    // unsaved-session scratch location only until a project is opened.
    QString local = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (local.isEmpty())
        local = QDir::tempPath();
    return QDir(local).filePath(QStringLiteral("CAE-unsaved-meshing/run-%1")
        .arg(QCoreApplication::applicationPid()));
}
inline QString dataDir() { return QDir(root()).filePath(QStringLiteral("data")); }
inline QString tempGridDir() { return QDir(root()).filePath(QStringLiteral("tempGrid")); }
inline QString brepPath() { return QDir(dataDir()).filePath(QStringLiteral("ExportedModel.brep")); }
inline QString stepPath() { return QDir(dataDir()).filePath(QStringLiteral("ExportedModel.step")); }
inline bool prepare()
{
    const QDir directory(root());
    if (QFileInfo(directory.absolutePath()).isSymLink()
        || QFileInfo(QFileInfo(directory.absolutePath()).dir().absolutePath()).isSymLink()
        || !QDir().mkpath(directory.absolutePath())) return false;
    QFile marker(directory.filePath(QStringLiteral(".cae-workspace")));
    if (!marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const auto owner=QByteArray::number(QCoreApplication::applicationPid());
    return marker.write(owner)==owner.size();
}
inline bool cleanup()
{
    const QFileInfo target(root());
    if (!target.exists()) return true;
    const QDir parent=target.dir();
    if (!target.isDir() || target.isSymLink() || QFileInfo(parent.absolutePath()).isSymLink()
        || target.fileName()!=QStringLiteral("run-%1").arg(QCoreApplication::applicationPid())
        || (parent.dirName()!=QLatin1String(".meshing-cache")
            && parent.dirName()!=QLatin1String("CAE-unsaved-meshing"))) return false;
    // Resolve and check the absolute target before recursively removing only
    // this process's explicitly marked scratch workspace.
    if (QDir::cleanPath(target.canonicalFilePath())!=QDir::cleanPath(target.absoluteFilePath())) return false;
    QFile marker(QDir(target.absoluteFilePath()).filePath(QStringLiteral(".cae-workspace")));
    if (!marker.open(QIODevice::ReadOnly)
        || marker.readAll()!=QByteArray::number(QCoreApplication::applicationPid())) return false;
    marker.close();
    return QDir(target.absoluteFilePath()).removeRecursively();
}
} // namespace MeshWorkingPaths
