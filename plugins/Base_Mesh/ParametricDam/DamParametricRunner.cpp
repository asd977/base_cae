#include "DamParametricRunner.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTextStream>

bool DamParametricRunner::writeTaskFile(const QString& projectDir, QString* error)
{
    const QString path = QDir(projectDir).filePath("MESG_ITASK.DAT");

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
    {
        if (error)
            *error = QString("无法写入 MESG_ITASK.DAT：%1").arg(path);
        return false;
    }

    QTextStream out(&file);
    out << "39\n";

    return true;
}

QString DamParametricRunner::executablePath(const DamParametricContext& ctx)
{
    if (ctx.useDebugExe)
    {
        return QDir(ctx.solverRoot).filePath("A02_CKJACOBI/X64/DEBUG/A02_CKJACOBI.EXE");
    }

    return QDir(ctx.solverRoot).filePath("A02_CKJACOBI/X64/Release/A02_CKJACOBI.EXE");
}

bool DamParametricRunner::run(const DamParametricContext& ctx, QString* error)
{
    QDir projectDir(ctx.projectDir);
    if (!projectDir.exists())
    {
        if (error)
            *error = QString("工程目录不存在：%1").arg(ctx.projectDir);
        return false;
    }

    if (!writeTaskFile(ctx.projectDir, error))
        return false;

    const QString exe = executablePath(ctx);

    if (!QFile::exists(exe))
    {
        if (error)
        {
            *error =
                QString("未找到 A02_CKJACOBI.EXE：\n%1\n\n"
                        "请检查 solverRoot 是否正确。")
                    .arg(exe);
        }
        return false;
    }

    QProcess process;
    process.setWorkingDirectory(ctx.projectDir);

    process.start(exe);

    if (!process.waitForStarted(5000))
    {
        if (error)
            *error = QString("A02_CKJACOBI.EXE 启动失败：%1").arg(process.errorString());
        return false;
    }

    process.waitForFinished(-1);

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());

    if (process.exitStatus() != QProcess::NormalExit)
    {
        if (error)
        {
            *error =
                QString("A02_CKJACOBI.EXE 异常退出。\n\nstdout:\n%1\n\nstderr:\n%2")
                    .arg(stdoutText, stderrText);
        }
        return false;
    }

    if (process.exitCode() != 0)
    {
        if (error)
        {
            *error = QString("A02_CKJACOBI.EXE 返回非 0 退出码：%1\n\nstdout:\n%2\n\nstderr:\n%3")
                         .arg(process.exitCode())
                         .arg(stdoutText, stderrText);
        }
        return false;
    }

    const QString iiinPath = projectDir.filePath("IIIN.DAT");
    if (!QFile::exists(iiinPath))
    {
        if (error)
        {
            *error =
                QString("A02_CKJACOBI.EXE 执行完成，但没有生成 IIIN.DAT。\n\n"
                        "请重点检查：\n"
                        "1. 工作目录是否是案例目录 / PREP 目录；\n"
                        "2. MSGJZDAM.DAT 是否存在；\n"
                        "3. MESG_ITASK.DAT 是否写入 39；\n"
                        "4. IIIDIR.DAT 是否仍然指向正确的 DIR / DIRSET；\n"
                        "5. EXE 是否要求从固定目录启动。\n\n"
                        "stdout:\n%1\n\nstderr:\n%2")
                    .arg(stdoutText, stderrText);
        }
        return false;
    }

    return true;
}
