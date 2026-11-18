#include "Output.h"

#include <ServiceRegistry.h>

#include <QAction>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QThread>
#include <PublicVariables.h>
#include <PublicConstants.h>

namespace
{
QMutex g_outputMutex;
Output* g_outputInstance = nullptr;
QtMessageHandler g_prevHandler = nullptr;
bool g_handlerInstalled = false;
}

Output::Output()
{
    ServiceRegistry::instance().registerService<OutputService>(this);
    m_logOutputWidget = new OutputWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    QAction* toggleAction = m_uiFrameService->addWidget(m_logOutputWidget, ("日志"));
    Q_UNUSED(toggleAction);
    installQtLogBridge();

    QMutexLocker locker(&g_outputMutex);
    g_outputInstance = this;
}

void Output::outputLog(QString str)
{
    if (!m_logOutputWidget)
        return;
    const QString project = PublicVariables::instance().get(PublicConstants::kProjectDirectoryKey).toString();
    QMetaObject::invokeMethod(
        m_logOutputWidget, [w = m_logOutputWidget, str, project]() {
            w->outputMessage(str);
            if (project.isEmpty()) return;
            const QDir directory(QDir(project).filePath(QStringLiteral("日志")));
            QFile file(directory.filePath(QStringLiteral("运行日志_%1.log").arg(QDate::currentDate().toString("yyyyMMdd"))));
            if (QDir().mkpath(directory.absolutePath()) && file.open(QIODevice::WriteOnly | QIODevice::Append))
                file.write((QDateTime::currentDateTime().toString("[yyyy-MM-dd HH:mm:ss.zzz] ")+str+QLatin1Char('\n')).toUtf8());
        },
        QThread::currentThread()==m_logOutputWidget->thread()?Qt::DirectConnection:Qt::QueuedConnection);
}

Output::~Output()
{
    QMutexLocker locker(&g_outputMutex);
    if (g_outputInstance == this) g_outputInstance = nullptr;
    if (g_handlerInstalled)
    {
        const auto current = qInstallMessageHandler(g_prevHandler);
        if (current != &Output::qtMessageHandler) qInstallMessageHandler(current);
        g_handlerInstalled = false;
    }
}

void Output::installQtLogBridge()
{
    QMutexLocker locker(&g_outputMutex);
    if (!g_handlerInstalled)
    {
        g_prevHandler = qInstallMessageHandler(&Output::qtMessageHandler);
        g_handlerInstalled = true;
    }
}

void Output::qtMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    const bool raw = msg.startsWith("__RAW__");
    const bool error = QByteArray(context.category) == "cae.solver"
        && (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);

    Output* instance = nullptr;
    {
        QMutexLocker locker(&g_outputMutex);
        instance = g_outputInstance;
    }
    if (instance && (raw || error))
        instance->outputLog(raw ? msg.mid(QString("__RAW__").size())
                               : QStringLiteral("[%1] %2").arg(type == QtWarningMsg ? QStringLiteral("警告") : QStringLiteral("错误"),msg));
    if (g_prevHandler) g_prevHandler(type, context, msg);
}
