#include "SMProcess.h"

#include <QDebug>
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
    qInfo().noquote() << m_programPath;
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
        qInfo().noquote() << "Exit code:" << code;
        emit onCompleted(false);
    }
}

void SMProcess::onError(ProcessError error)
{
    QString errorStr = "";
    switch (error)
    {
    case FailedToStart:
        errorStr = "错误：程序启动失败";
        break;
    case Crashed:
        errorStr = "错误：程序异常退出";
        break;
    case Timedout:
        errorStr = "错误：程序运行超时";
        break;
    case ReadError:
        errorStr = "错误：读取数据失败";
        break;
    case WriteError:
        errorStr = "错误：写入数据失败";
        break;
    default:
        errorStr = "错误：发生未知错误";
        break;
    }
    qInfo().noquote() << errorStr;
    emit onErrorSignal(errorStr);
}

void SMProcess::onOutput()
{
    QByteArray outputData = this->readAllStandardOutput();
    qInfo().noquote() << outputData;
}

void SMProcess::initializeConnections()
{
    connect(this, &QProcess::readyReadStandardOutput, this, &SMProcess::onOutput);
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &SMProcess::onFinished);
    connect(this, &QProcess::errorOccurred, this, &SMProcess::onError);
}
} // namespace SMProcessTool
