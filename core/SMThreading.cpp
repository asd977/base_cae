#include "SMThreading.h"

namespace SMThreadingTool
{
SMThreading::SMThreading(QObject* parent) : QThread(parent)
{
    this->setStackSize(1024 * 1024);
}

SMThreading::~SMThreading()
{
    this->stop();
}

void SMThreading::postTask(const std::function<void()>& task)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_taskQueue.push(task);
}

void SMThreading::run()
{
    qInfo() << "SMThreading:" << this->currentThreadId();
    while (!m_shouldStop)
    {
        bool queueEmpty = false;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            queueEmpty = m_taskQueue.empty();
        }

        //
        while (!queueEmpty && !m_shouldStop)
        {
            std::function<void()> task;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                task = m_taskQueue.front();
                m_taskQueue.pop();
                queueEmpty = m_taskQueue.empty();
            }

            task();
            if (queueEmpty)
            {
                emit allTasksFinished(); //
            }
        }
        QThread::msleep(100);
    }
}

void SMThreading::stop()
{
    m_shouldStop = true;
}

int SMThreading::getTaskCount()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_taskQueue.size();
}

} // namespace SMThreadingTool
