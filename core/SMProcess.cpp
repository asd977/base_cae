#include "SMProcess.h"
#include <QDebug>
namespace SMProcessTool
{
SMProcess::SMProcess(const QString &program)
{
    this->setProgram(program);
	m_name = program;
    initConn();
}

SMProcess::SMProcess(const QString &program, const QStringList &args)
{
    m_name = program;
    m_args = args;
    initConn();
}

SMProcess::~SMProcess()
{
	close();
    this->waitForFinished();
}

void SMProcess::startProcess(bool async)
{
    qInfo().noquote() << m_name;
    if(m_args.empty()){
        if(async){
		    this->setProcessChannelMode(QProcess::ForwardedChannels);
            this->startDetached();
        }else{
            this->start();
        }
    }

    if(!m_args.empty()){
        qInfo().noquote() << m_args;
        if(async){
            this->setProcessChannelMode(QProcess::ForwardedChannels);
            this->startDetached(m_name, m_args);
        }else{
            this->start(m_name, m_args);
        }
    }
}

void SMProcess::onFinished(int code, ExitStatus status)
{
    if((code == 0||code == 1) && status == QProcess::NormalExit){
        emit onCompleted(true);
    }else{
        qInfo().noquote() << "Exit code:" << code;
        emit onCompleted(false);
    }
}

void SMProcess::onError(ProcessError error)
{
    QString errorStr = "";
    switch (error){
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
	qInfo().noquote() << errorStr;
    emit onErrorSignal(errorStr);
}

void SMProcess::onOutput()
{
	QByteArray outputData = this->readAllStandardOutput();
	QString text = outputData;
    qInfo().noquote()<< outputData;
}

void SMProcess::initConn()
{
    connect(this, &QProcess::readyReadStandardOutput, this, &SMProcess::onOutput);
    connect(this, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &SMProcess::onFinished);
    connect(this, &QProcess::errorOccurred, this, &SMProcess::onError);
}
}