#include "Output.h"
#include <ServiceRegistry.h>
#include <QAction>
#include <QDateTime>
#include <QToolButton>
Output::Output()
{
    ServiceRegistry::instance().registerService<OutputService>(this);
    m_logOutputWidget = new OutputWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    QAction *toggleAction = m_uiFrameService->addWidget(m_logOutputWidget,("日志"));

}


void Output::outputLog(QString str)
{
	QString dataStr = QDateTime::currentDateTime().toString("[yyyy年M月d日 ap hh:mm] ");
    QString logStr = dataStr + str;
    m_logOutputWidget->outputMessage(logStr);
}
