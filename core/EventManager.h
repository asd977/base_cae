#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include <core_global.h>

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QReadWriteLock>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtGlobal>
#include <atomic>
#include <functional>

class CORE_EXPORT EventManager {
 public:
  using HandlerId = quint64;
  using Handler = std::function<void(const QVariantMap&)>;

  // 回调执行策略
  enum class Dispatch {
    Direct,  // 直接在 sendEvent 的线程执行（不做线程切换）
    Queued,  // 如果有 context，则投递到 context 线程执行（异步）
    Auto     // 有 context 且跨线程 => Queued；同线程 => Direct
  };

  static EventManager& instance();

  // 兼容旧接口：不带 context
  HandlerId registerHandler(const QString& eventType, Handler handler,
                            int priority = 0);

  // 推荐接口：带 context，自动随对象销毁解绑
  HandlerId registerHandler(const QString& eventType, QObject* context,
                            Handler handler, int priority = 0);

  bool unregisterHandler(HandlerId id);

  // 批量解绑（可选）
  int unregisterHandlers(QObject* context);
  int clearEvent(const QString& eventType);
  void clearAll();

  // 默认 Auto（UI/跨线程更安全）
  void sendEvent(const QString& eventType,
                 const QVariantMap& data = QVariantMap(),
                 Dispatch dispatch = Dispatch::Auto);

 private:
  EventManager() = default;
  Q_DISABLE_COPY(EventManager);

  struct HandlerEntry {
    HandlerId id = 0;
    int priority = 0;
    QPointer<QObject> context;  // 可为空
    Handler fn;
  };

 private:
  HandlerId nextId();

  // 清理某个事件里的“已失效 context”项（懒清理）
  void cleanupDeadHandlers_locked(const QString& eventType);

 private:
  std::atomic<HandlerId> m_nextId{1};

  QHash<QString, QVector<HandlerEntry>> m_handlers;  // eventType -> handlers
  QHash<HandlerId, QString> m_idToType;              // id -> eventType

  QReadWriteLock m_lock;
};

#endif  // EVENTMANAGER_H
