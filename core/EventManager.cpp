#include "EventManager.h"

#include <QCoreApplication>
#include <QThread>
#include <QTimer>

EventManager& EventManager::instance()
{
    static EventManager em;
    return em;
}

EventManager::HandlerId EventManager::nextId()
{
    return m_nextId.fetch_add(1, std::memory_order_relaxed);
}

EventManager::HandlerId EventManager::registerHandler(const QString& eventType, Handler handler,
                                                      int priority)
{
    return registerHandler(eventType, nullptr, std::move(handler), priority);
}

EventManager::HandlerId EventManager::registerHandler(const QString& eventType, QObject* context,
                                                      Handler handler, int priority)
{
    const HandlerId id = nextId();

    {
        QWriteLocker locker(&m_lock);

        HandlerEntry entry;
        entry.id = id;
        entry.priority = priority;
        entry.context = context;
        entry.fn = std::move(handler);

        auto& vec = m_handlers[eventType];

        // 按 priority 降序插入（同优先级保持注册顺序）
        int insertPos = vec.size();
        for (int i = 0; i < vec.size(); ++i)
        {
            if (priority > vec[i].priority)
            {
                insertPos = i;
                break;
            }
        }
        vec.insert(insertPos, std::move(entry));

        m_idToType.insert(id, eventType);
    }

    // 如果有 context：销毁时自动解绑（更干净）
    if (context)
    {
        QObject* app = QCoreApplication::instance();
        if (app)
        {
            QObject::connect(
                context, &QObject::destroyed, app,
                [id]()
                {
                    EventManager::instance().unregisterHandler(id);
                },
                Qt::DirectConnection);
        }
        // 即使 app 为空，也没关系：dispatch 时 QPointer 会防 UAF，后面懒清理
    }

    return id;
}

bool EventManager::unregisterHandler(HandlerId id)
{
    QWriteLocker locker(&m_lock);

    auto itType = m_idToType.find(id);
    if (itType == m_idToType.end())
        return false;

    const QString eventType = itType.value();
    m_idToType.erase(itType);

    auto it = m_handlers.find(eventType);
    if (it == m_handlers.end())
        return false;

    auto& vec = it.value();
    for (int i = 0; i < vec.size(); ++i)
    {
        if (vec[i].id == id)
        {
            vec.removeAt(i);
            break;
        }
    }

    if (vec.isEmpty())
        m_handlers.remove(eventType);

    return true;
}

int EventManager::unregisterHandlers(QObject* context)
{
    if (!context)
        return 0;

    int removed = 0;
    QWriteLocker locker(&m_lock);

    // 遍历所有事件，删掉 context 匹配的
    for (auto it = m_handlers.begin(); it != m_handlers.end();)
    {
        auto& vec = it.value();
        for (int i = vec.size() - 1; i >= 0; --i)
        {
            if (vec[i].context == context)
            {
                m_idToType.remove(vec[i].id);
                vec.removeAt(i);
                ++removed;
            }
        }

        if (vec.isEmpty())
            it = m_handlers.erase(it);
        else
            ++it;
    }

    return removed;
}

int EventManager::clearEvent(const QString& eventType)
{
    QWriteLocker locker(&m_lock);

    auto it = m_handlers.find(eventType);
    if (it == m_handlers.end())
        return 0;

    const auto& vec = it.value();
    for (const auto& h : vec)
        m_idToType.remove(h.id);

    const int removed = vec.size();
    m_handlers.remove(eventType);
    return removed;
}

void EventManager::clearAll()
{
    QWriteLocker locker(&m_lock);
    m_handlers.clear();
    m_idToType.clear();
}

void EventManager::cleanupDeadHandlers_locked(const QString& eventType)
{
    auto it = m_handlers.find(eventType);
    if (it == m_handlers.end())
        return;

    auto& vec = it.value();
    for (int i = vec.size() - 1; i >= 0; --i)
    {
        // 绑定了 context 且 context 已销毁 => 清理
        if (vec[i].context != nullptr && vec[i].context.isNull())
        {
            m_idToType.remove(vec[i].id);
            vec.removeAt(i);
        }
    }

    if (vec.isEmpty())
        m_handlers.remove(eventType);
}

void EventManager::sendEvent(const QString& eventType, const QVariantMap& data, Dispatch dispatch)
{
    QVector<HandlerEntry> snapshot;

    {
        QReadLocker locker(&m_lock);

        auto it = m_handlers.find(eventType);
        if (it == m_handlers.end())
            return;

        snapshot = it.value(); // 关键：拷贝出来，避免锁内执行回调
    }

    // 执行回调（不持锁）
    bool needCleanup = false;

    for (const auto& h : snapshot)
    {
        // context 绑定且已死：跳过并标记清理
        if (h.context != nullptr && h.context.isNull())
        {
            needCleanup = true;
            continue;
        }

        // 没有 context：只能直接执行（由调用方负责线程）
        if (h.context == nullptr)
        {
            h.fn(data);
            continue;
        }

        QObject* ctx = h.context.data();

        const bool sameThread = (QThread::currentThread() == ctx->thread());
        const bool runDirect =
            (dispatch == Dispatch::Direct) || (dispatch == Dispatch::Auto && sameThread);

        if (runDirect)
        {
            h.fn(data);
        }
        else
        {
            // 投递到 context 线程（异步），且 context 销毁会自动取消 singleShot
            QPointer<QObject> guard(ctx);
            auto fnCopy = h.fn;   // std::function 可拷贝
            auto dataCopy = data; // QVariantMap 可拷贝

            QTimer::singleShot(0, ctx,
                               [guard, fnCopy, dataCopy]() mutable
                               {
                                   if (!guard.isNull())
                                       fnCopy(dataCopy);
                               });
        }
    }

    // 懒清理：把已经销毁的 context handler 从表里删掉
    if (needCleanup)
    {
        QWriteLocker locker(&m_lock);
        cleanupDeadHandlers_locked(eventType);
    }
}
