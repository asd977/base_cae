#ifndef SMTHREADING_H
#define SMTHREADING_H

#include <core_global.h>

#include <QDebug>
#include <QThread>
#include <functional>
#include <mutex>
#include <queue>

/**
 * @brief 线程工具类
 * @author tongle
 * @date 2025.08.01
 */

// A simple thread with task queue
// auto thread = new SMThreadingTool::SMThreading(this);;
// thread->start();
// thread->postTask([=]() {computeSum(1000000); });
namespace SMThreadingTool
{
class CORE_EXPORT SMThreading : public QThread
{
    Q_OBJECT
  public:
    SMThreading(QObject* parent);
    ~SMThreading();

    void postTask(const std::function<void()>& task);
    void run() override;
    void stop();
    int getTaskCount();
  signals:
    void allTasksFinished();

  private:
    std::queue<std::function<void()>> m_taskQueue;
    std::mutex m_mutex;
    bool m_shouldStop{false};
};
} // namespace SMThreadingTool

#endif // SMTHREADING_H
