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
    qCInfo(lcProcess).noquote() << "Start process:" << m_programPath << m_arguments.join(" ");
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
        emit onCompleted(true);
    }
    else
    {
        qCWarning(lcProcess).noquote() << "Exit code:" << code;
        emit onCompleted(false);
    }
}

void SMProcess::onError(ProcessError error)
{
    QString errorStr = "";
    switch (error)
    {
    case FailedToStart:
        errorStr = "Error: Failed to start";
        break;
    case Crashed:
        errorStr = "Error: Program crashed";
        break;
    case Timedout:
        errorStr = "Error: Run timeout";
        break;
    case ReadError:
        errorStr = "Error: ReadError";
        break;
    case WriteError:
        errorStr = "Error: WriteError";
        break;
    default:
        errorStr = "Error: Unknown error";
        break;
    }
    qCCritical(lcProcess).noquote() << errorStr;
    emit onErrorSignal(errorStr);
}

void SMProcess::onOutput()
{
    QByteArray outputData = this->readAllStandardOutput();
    qCInfo(lcProcess).noquote() << QString::fromLocal8Bit(outputData).trimmed();
}

void SMProcess::onErrorOutput()
{
    const QByteArray errorData = this->readAllStandardError();
    const QString text = QString::fromLocal8Bit(errorData).trimmed();
    if (!text.isEmpty())
        qCWarning(lcProcess).noquote() << text;
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
