#include "Output.h"

#include <Logger.h>
#include <ServiceRegistry.h>

#include <QAction>
#include <QMetaObject>

Output::Output()
{
    ServiceRegistry::instance().registerService<OutputService>(this);
    m_logOutputWidget = new OutputWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    QAction* toggleAction = m_uiFrameService->addWidget(m_logOutputWidget, ("日志"));
    Q_UNUSED(toggleAction);
    Logger::installQtMessageBridge();
}

void Output::outputLog(QString str)
{
    if (!m_logOutputWidget)
        return;
    QMetaObject::invokeMethod(
        m_logOutputWidget, [w = m_logOutputWidget, str]() { w->outputMessage(str); },
        Qt::QueuedConnection);
}
