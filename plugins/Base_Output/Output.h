#ifndef OUTPUT_H
#define OUTPUT_H

#include <services/OutputService.h>
#include <services/UiFrameService.h>
#include "OutputWidget.h"
#include <QObject>

class Output: public QObject, public OutputService
{
    Q_OBJECT
public:
    Output();

    //输出进度
    void outputProgress(QString str);
    //输出日志
    void outputLog(QString str);
private:
    UiFrameService *m_uiFrameService;
    //输出日志的窗口
    OutputWidget *m_logOutputWidget;
    //输出进度的窗口
    OutputWidget *m_progressOutputWidget;
};

#endif // OUTPUT_H
