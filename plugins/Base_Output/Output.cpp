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

void Output::qtMessageHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    if (!msg.startsWith("__RAW__"))
        return;

    Output* instance = nullptr;
    {
        QMutexLocker locker(&g_outputMutex);
        instance = g_outputInstance;
    }
    if (instance)
        instance->outputLog(msg.mid(QString("__RAW__").size()));
}
