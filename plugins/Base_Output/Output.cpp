#include "Output.h"

#include <ServiceRegistry.h>

#include <QAction>

Output::Output() {
  ServiceRegistry::instance().registerService<OutputService>(this);
  m_logOutputWidget = new OutputWidget;
  m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
  QAction *toggleAction =
      m_uiFrameService->addWidget(m_logOutputWidget, ("日志"));
  Q_UNUSED(toggleAction);
}

void Output::outputLog(QString str) { m_logOutputWidget->outputMessage(str); }
