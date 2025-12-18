#include "EventManager.h"

EventManager& EventManager::instance() {
    static EventManager registry;
    return registry;
}


void EventManager::registerHandler(const QString & eventType, std::function<void(const QVariantMap&)> handler)
{
	QWriteLocker locker(&m_lock);
	m_eventHandlers[eventType].append(handler);  // 保持原逻辑
}

void EventManager::sendEvent(const QString &eventType, const QVariantMap &data) {
	QReadLocker locker(&m_lock);
	if (m_eventHandlers.contains(eventType)) {
		for (auto &handler : m_eventHandlers[eventType]) {
			handler(data);  // 传入 map，所有处理器统一访问
		}
	}
}
