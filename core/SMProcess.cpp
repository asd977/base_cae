#include "SMProcess.h"

#include <Logger.h>
namespace SMProcessTool
{
SMProcess::SMProcess(const QString& program)
{
    setProgram(program);
    m_programPath = program;
    initializeConnections();
}

SMProcess::SMProcess(const QString& program, const QStringList& args)
{
    setProgram(program);
    m_programPath = program;
    m_arguments = args;
    initializeConnections();
}

SMProcess::~SMProcess()
{
    close();
    this->waitForFinished();
}

void SMProcess::startProcess(bool async)
{
    qInfo().noquote() << "开始求解器计算:" << m_programPath << m_arguments.join(" ");
    const bool hasArguments = !m_arguments.isEmpty();

    if (async)
    {
        setProcessChannelMode(QProcess::ForwardedChannels);
        if (hasArguments)
        {
            QProcess::startDetached(m_programPath, m_arguments);
        }
        else
        {
            QProcess::startDetached(m_programPath, {});
        }
        return;
    }

    if (hasArguments)
    {
        start(m_programPath, m_arguments);
    }
    else
    {
        //        start(m_programPath, {});
    }
}

void SMProcess::onFinished(int code, ExitStatus status)
{
    if ((code == 0 || code == 1) && status == QProcess::NormalExit)
    {
        qInfo().noquote() << "求解器运行完成，退出码:" << code;
        emit onCompleted(true);
    }
    else
    {
        qCritical().noquote() << "求解器异常退出，退出码:" << code;
        emit onCompleted(false);
    }
}

void SMProcess::onError(ProcessError error)
{
    QString errorStr = "";
    switch (error)
    {
    case FailedToStart:
        errorStr = "求解器启动失败";
        break;
    case Crashed:
        errorStr = "求解器运行崩溃";
        break;
    case Timedout:
        errorStr = "求解器运行超时";
        break;
    case ReadError:
        errorStr = "求解器读取输出失败";
        break;
    case WriteError:
        errorStr = "求解器写入输入失败";
        break;
    default:
        errorStr = "求解器发生未知错误";
        break;
    }
    qCritical().noquote() << errorStr;
    emit onErrorSignal(errorStr);
}

void SMProcess::onOutput()
{
    QByteArray outputData = this->readAllStandardOutput();
    const QString text = QString::fromLocal8Bit(outputData).trimmed();
    if (!text.isEmpty())
        qInfo().noquote() << "[求解器输出]" << text;
}

void SMProcess::onErrorOutput()
{
    const QByteArray errorData = this->readAllStandardError();
    const QString text = QString::fromLocal8Bit(errorData).trimmed();
    if (!text.isEmpty())
        qWarning().noquote() << "[求解器警告]" << text;
}

void SMProcess::initializeConnections()
{
    connect(this, &QProcess::readyReadStandardOutput, this, &SMProcess::onOutput);
    connect(this, &QProcess::readyReadStandardError, this, &SMProcess::onErrorOutput);
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &SMProcess::onFinished);
    connect(this, &QProcess::errorOccurred, this, &SMProcess::onError);
}
} // namespace SMProcessTool
