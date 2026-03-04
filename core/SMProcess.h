#ifndef SMPROCESS_H
#define SMPROCESS_H

#include <core_global.h>

#include <QObject>
#include <QProcess>
/**
 * @brief 进程工具类
 * @author tongle
 * @date 2025.08.01
 */

// example
// QString program = "1.exe";
// SMProcessTool::SMProcess* process = new SMProcessTool::SMProcess(program);
// process->setWorkingDirectory("./");
// connect(process, &SMProcessTool::SMProcess::onCompleted, this, [&](bool
// completed) { 	if (completed)
//	{
//	}
// });

namespace SMProcessTool
{
class CORE_EXPORT SMProcess : public QProcess
{
    Q_OBJECT
  public:
    explicit SMProcess(const QString& program);
    SMProcess(const QString& program, const QStringList& args);
    ~SMProcess();
    void startProcess(bool async = false); // async 是否异步启动

  signals:
    void onCompleted(bool);
    void onErrorSignal(QString);

  public slots:
    void onFinished(int code, QProcess::ExitStatus status);
    void onError(QProcess::ProcessError error);
    void onOutput();

  private:
    SMProcess() = delete;
    void initializeConnections();

    QString m_programPath;
    QStringList m_arguments;
};
} // namespace SMProcessTool

#endif // SMPROCESS_H
