#include "MsgJzDamIO.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

QString MsgJzDamIO::filePath(const QString& projectDir)
{
    return QDir(projectDir).filePath("MSGJZDAM.DAT");
}

QString MsgJzDamIO::cleanLine(QString line)
{
    line = line.trimmed();

    if (line.startsWith("'") && line.endsWith("'") && line.size() >= 2)
        line = line.mid(1, line.size() - 2);

    return line.trimmed();
}

bool MsgJzDamIO::ensureExists(const QString& projectDir, QString* error)
{
    const QString path = filePath(projectDir);

    if (QFile::exists(path))
        return true;

    const MsgJzDamParams params = MsgJzDamParams::makeDefault();
    return write(projectDir, params, error);
}

bool MsgJzDamIO::read(const QString& projectDir, MsgJzDamParams& params, QString* error)
{
    const QString path = filePath(projectDir);

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QString("无法读取 MSGJZDAM.DAT：%1").arg(path);
        return false;
    }

    QTextStream in(&file);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    in.setCodec("GBK");
#endif

    for (int i = 1; i <= 35; ++i)
    {
        if (in.atEnd())
        {
            if (error)
                *error = QString("MSGJZDAM.DAT 内容不足，缺少 P%1").arg(i, 3, 10, QChar('0'));
            return false;
        }

        params.P(i) = cleanLine(in.readLine());
    }

    return true;
}

bool MsgJzDamIO::write(const QString& projectDir, const MsgJzDamParams& params, QString* error)
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
            *error = QString("无法写入 MSGJZDAM.DAT：%1").arg(path);
        return false;
    }

    QTextStream out(&file);

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("GBK");
#endif

    for (int i = 1; i <= 35; ++i)
    {
        out << "'" << params.P(i).trimmed() << "'\n";
    }

    return true;
}
