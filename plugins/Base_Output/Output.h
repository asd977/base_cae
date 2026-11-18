#ifndef OUTPUT_H
#define OUTPUT_H

#include <services/OutputService.h>
#include <services/UiFrameService.h>

#include <QObject>

#include "OutputWidget.h"

class Output : public QObject, public OutputService
{
    Q_OBJECT
  public:
    Output();
    ~Output() override;
    // 输出日志
    void outputLog(QString str);
    static void installQtLogBridge();

  private:
    static void qtMessageHandler(QtMsgType type, const QMessageLogContext& context,
                                 const QString& msg);
    UiFrameService* m_uiFrameService;
    // 输出日志的窗口
    OutputWidget* m_logOutputWidget;
};

#endif // OUTPUT_H
