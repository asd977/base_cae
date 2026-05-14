#include "Output.h"

#include <ServiceRegistry.h>

#include <QAction>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QDebug>

namespace
{
QMutex g_outputMutex;
Output* g_outputInstance = nullptr;
QtMessageHandler g_prevHandler = nullptr;
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
    QMetaObject::invokeMethod(
        m_logOutputWidget, [w = m_logOutputWidget, str]() { w->outputMessage(str); },
        Qt::QueuedConnection);
}

void Output::installQtLogBridge()
{
    QMutexLocker locker(&g_outputMutex);
    if (!g_prevHandler)
        g_prevHandler = qInstallMessageHandler(&Output::qtMessageHandler);
}

void Output::qtMessageHandler(QtMsgType type, const QMessageLogContext& context,
                              const QString& msg)
{
    if (msg.startsWith("__RAW__"))
    {
        Output* instance = nullptr;
        {
            QMutexLocker locker(&g_outputMutex);
            instance = g_outputInstance;
        }
        if (instance)
            instance->outputLog(msg.mid(QString("__RAW__").size()));
        if (g_prevHandler)
            g_prevHandler(type, context, msg.mid(QString("__RAW__").size()));
        return;
    }
    QString level = "[信息]";
    if (type == QtWarningMsg)
        level = "[警告]";
    else if (type == QtCriticalMsg || type == QtFatalMsg)
        level = "[错误]";
    else if (type == QtDebugMsg)
        return;

    QString location;
    if (context.file && context.line > 0)
        location = QString(" (%1:%2)").arg(context.file).arg(context.line);

    const QString text = QString("%1 %2%3").arg(level, msg, location);

    Output* instance = nullptr;
    {
        QMutexLocker locker(&g_outputMutex);
        instance = g_outputInstance;
    }
    if (instance)
        instance->outputLog(text);

    if (g_prevHandler)
        g_prevHandler(type, context, msg);
}
