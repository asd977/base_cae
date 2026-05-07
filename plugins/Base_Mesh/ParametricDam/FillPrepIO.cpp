#include "FillPrepIO.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

QString FillPrepIO::filePath(const QString& projectDir)
{
    return QDir(projectDir).filePath("IMID_FILLPREP.DAT");
}

bool FillPrepIO::ensureExists(const QString& projectDir, QString* error)
{
    const QString path = filePath(projectDir);
    if (QFile::exists(path))
        return true;

    FillPrepParams params;
    return write(projectDir, params, error);
}

bool FillPrepIO::read(const QString& projectDir, FillPrepParams& params, QString* error)
{
    const QString path = filePath(projectDir);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QString("无法读取 IMID_FILLPREP.DAT：%1").arg(path);
        return false;
    }

    QTextStream in(&file);
    in >> params.xmid;
    in >> params.width;
    in >> params.xa >> params.ya;
    in >> params.xb >> params.yb;
    in >> params.tol2;

    if (in.status() != QTextStream::Ok)
    {
        if (error)
            *error = QString("IMID_FILLPREP.DAT 格式错误：%1").arg(path);
        return false;
    }

    return true;
}

bool FillPrepIO::write(const QString& projectDir, const FillPrepParams& params, QString* error)
{
    QDir dir(projectDir);
    if (!dir.exists())
    {
        if (error)
            *error = QString("工程目录不存在：%1").arg(projectDir);
        return false;
    }

    const QString path = filePath(projectDir);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = QString("无法写入 IMID_FILLPREP.DAT：%1").arg(path);
        return false;
    }

    QTextStream out(&file);
    out << QString("%1\n").arg(params.xmid, 10, 'f', 4);
    out << QString("%1\n").arg(params.width, 10, 'f', 4);
    out << QString("%1 %2\n").arg(params.xa, 10, 'f', 4).arg(params.ya, 10, 'f', 4);
    out << QString("%1 %2\n").arg(params.xb, 10, 'f', 4).arg(params.yb, 10, 'f', 4);
    out << QString("%1\n").arg(params.tol2, 10, 'f', 4);

    return true;
}
