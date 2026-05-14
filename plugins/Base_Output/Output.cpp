#include "Output.h"

#include <ServiceRegistry.h>

#include <QAction>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>

namespace
{
QMutex g_outputMutex;
Output* g_outputInstance = nullptr;
}

Output::Output()
{
    ServiceRegistry::instance().registerService<OutputService>(this);
    m_logOutputWidget = new OutputWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    QAction* toggleAction = m_uiFrameService->addWidget(m_logOutputWidget, ("日志"));
    Q_UNUSED(toggleAction);

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

