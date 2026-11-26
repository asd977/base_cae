#pragma once

// CAE-side validation/import for SOLVER's optional quiet-boundary face set.
// Do not enable IFUSHE until a real FA_DYNA.DAT exists in the solver input directory.
#include <PublicConstants.h>
#include <PublicVariables.h>
#include <SolverFilePaths.h>
#include <QDir>
#include <QFile>
#include <QByteArray>
#include <QFileDialog>
#include <QSaveFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTextStream>
#include <QStringList>
#include <QString>
#include <QWidget>

namespace SolverQuietBoundary
{
inline QString destinationPath()
{
    const QString projectDir =
        PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    if (projectDir.trimmed().isEmpty())
        return {};
    return QDir(SolverFilePaths::outputInputDirectory(projectDir))
        .filePath(QStringLiteral("FA_DYNA.DAT"));
}

inline bool hasValidFileAtPath(const QString& path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0)
        return false;
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    QTextStream reader(&file);
    const QStringList header = reader.readLine().simplified().replace(QLatin1Char(','), QLatin1Char(' ')).split(
        QLatin1Char(' '), Qt::SkipEmptyParts);
    if (header.size() < 2)
        return false;
    bool faceCountOk = false;
    bool nodeCountOk = false;
    const int count = header.at(0).toInt(&faceCountOk);
    const int makod = header.at(1).toInt(&nodeCountOk);
    // SOLVER MAFAJD = MAKOD - 2 (2D edge >= 4, 3D face >= 5).
    if (!faceCountOk || !nodeCountOk || count <= 0 || count > 2000000
        || makod < 4
        || makod > 34 || reader.atEnd())
        return false;
    reader.readLine(); // solver's second header row
    for (int index = 0; index < count; ++index)
    {
        if (reader.atEnd())
            return false;
        const QStringList cols = reader.readLine().trimmed().split(
            QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        bool idxOk = false;
        if (cols.size() < makod + 2 || cols.at(0).toInt(&idxOk) != index + 1 || !idxOk)
            return false;
    }
    return true;
}

inline bool hasValidFile()
{
    return hasValidFileAtPath(destinationPath());
}

inline bool importFile(QWidget* parent)
{
    const QString selected = QFileDialog::getOpenFileName(
        parent, QStringLiteral("选择粘弹性边界面集"), QString(),
        QStringLiteral("面集文件 (*.dat *.DAT);;所有文件 (*)"));
    if (selected.isEmpty())
        return false;
    if (!hasValidFileAtPath(selected))
    {
        QMessageBox::warning(parent, QStringLiteral("面集文件"),
                             QStringLiteral("面集格式无效，需要面数、每面节点数和完整的面数据。"));
        return false;
    }
    const QString output = destinationPath();
    if (output.isEmpty())
    {
        QMessageBox::warning(parent, QStringLiteral("面集文件"),
                             QStringLiteral("请先保存工程，再导入边界面集。"));
        return false;
    }
    const QFileInfo outputInfo(output);
    if (!QDir().mkpath(outputInfo.absolutePath()))
    {
        QMessageBox::warning(parent, QStringLiteral("面集文件"),
                             QStringLiteral("无法创建计算输入目录。"));
        return false;
    }
    if (QFileInfo(selected).absoluteFilePath() == outputInfo.absoluteFilePath())
        return true;
    QFile source(selected);
    QSaveFile target(output);
    if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly))
    {
        QMessageBox::warning(parent, QStringLiteral("面集文件"),
                             QStringLiteral("无法打开面集文件。"));
        return false;
    }
    while (!source.atEnd())
    {
        const QByteArray chunk = source.read(64 * 1024);
        if (chunk.isEmpty() || target.write(chunk) != chunk.size())
        {
            target.cancelWriting();
            QMessageBox::warning(parent, QStringLiteral("面集文件"),
                                 QStringLiteral("导入面集文件失败。"));
            return false;
        }
    }
    if (!target.commit())
    {
        QMessageBox::warning(parent, QStringLiteral("面集文件"),
                             QStringLiteral("无法保存面集文件。"));
        return false;
    }
    return true;
}
}
