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
    EventManager() {}  // 私有构造函数确保单例

	QMap<QString, QList<std::function<void(const QVariantMap&)>>> eventHandlers;
    QReadWriteLock lock;  // 用于保护多线程访问
};

#endif // EVENTMANAGER_H
