#include "Output.h"
#include <ServiceRegistry.h>
#include <QAction>
#include <QDateTime>
#include <QToolButton>
Output::Output()
{
    ServiceRegistry::instance().registerService<OutputService>(this);
    m_logOutputWidget = new OutputWidget;
    m_progressOutputWidget = new OutputWidget;
    m_uiFrameService = ServiceRegistry::instance().getService<UiFrameService>();
    QAction *toggleAction = m_uiFrameService->addWidget(m_logOutputWidget,("日志"));
    QToolButton *toolButton = new QToolButton();
    toolButton->setText(("日志"));
    toolButton->setIcon(QIcon(":/resources/menu/log.png"));
    m_uiFrameService->addMenuButton(("视图"), "1", toolButton);
    connect(toolButton, &QToolButton::clicked, toggleAction, &QAction::trigger);

    QAction *toggleAction2 = m_uiFrameService->addWidget(m_progressOutputWidget,("进度"));
    QToolButton *toolButton2 = new QToolButton();
    toolButton2->setText(("进度"));
    toolButton2->setIcon(QIcon(":/resources/menu/progress.png"));
    m_uiFrameService->addMenuButton(("视图"), "1", toolButton2);
    connect(toolButton2, &QToolButton::clicked, toggleAction2, &QAction::trigger);
}

void Output::outputProgress(QString str)
{
    m_progressOutputWidget->outputMessage(str);
}

void Output::outputLog(QString str)
{
	QString dataStr = QDateTime::currentDateTime().toString("[yyyy年M月d日 ap hh:mm] ");
    QString logStr = dataStr + str;
    m_logOutputWidget->outputMessage(logStr);
}
