// EventManager.h
#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include <QObject>
#include <QMap>
#include <QList>
#include <QString>
#include <functional>
#include <QReadWriteLock>
#include <core_global.h>
#include <QVariantMap>

class CORE_EXPORT EventManager
{
public:
    static EventManager& instance();

	// 注册：支持多参数的处理器
    void registerHandler(const QString &eventType, std::function<void(const QVariantMap&)> handler);
    // 发送事件
    void sendEvent(const QString &eventType, const QVariantMap &data = QVariantMap());


private:
    EventManager() = default;  // 私有构造函数确保单例
    Q_DISABLE_COPY(EventManager);

	QMap<QString, QList<std::function<void(const QVariantMap&)>>> m_eventHandlers;
    QReadWriteLock m_lock;  // 用于保护多线程访问
};

#endif // EVENTMANAGER_H
