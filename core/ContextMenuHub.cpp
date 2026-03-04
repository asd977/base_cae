#include "ContextMenuHub.h"

#include <QAction>
#include <QMenu>
#include <algorithm>

ContextMenuHub& ContextMenuHub::instance()
{
    static ContextMenuHub s;
    return s;
}

void ContextMenuHub::registerProvider(const QString& scope, QObject* owner, BuildFn build,
                                      int priority, AcceptFn accept)
{
    if (scope.isEmpty() || !build)
        return;

    Provider p;
    p.scope = scope;
    p.priority = priority;
    p.owner = owner;
    p.accept = std::move(accept);
    p.build = std::move(build);

    m_providers.push_back(std::move(p));

    std::sort(m_providers.begin(), m_providers.end(),
              [](const Provider& a, const Provider& b)
              {
                  return a.priority > b.priority;
              });
}

void ContextMenuHub::buildMenu(const QString& scope, QMenu* menu, const QVariantMap& ctx)
{
    if (!menu || scope.isEmpty())
        return;

    // 懒清理：owner 已销毁的 provider
    for (int i = m_providers.size() - 1; i >= 0; --i)
    {
        if (m_providers[i].owner == nullptr)
            continue;
        if (!m_providers[i].owner.isNull())
            continue;
        m_providers.removeAt(i);
    }

    for (const auto& p : m_providers)
    {
        if (p.scope != scope)
            continue;

        if (p.owner != nullptr && p.owner.isNull())
            continue;

        if (p.accept && !p.accept(ctx))
            continue;

        p.build(menu, ctx);
    }
}

void ContextMenuHub::addAction(QMenu* menu, QObject* owner, const QString& text,
                               std::function<void()> fn)
{
    if (!menu || text.isEmpty() || !fn)
        return;

    QAction* act = menu->addAction(text);

    // 用 owner 作为上下文：owner 析构后信号自动断开
    QObject* ctxObj = owner ? owner : menu;

    QObject::connect(act, &QAction::triggered, ctxObj,
                     [guard = QPointer<QObject>(owner), fn]() mutable
                     {
                         if (guard && guard.isNull())
                             return; // owner 已死，啥也不做
                         fn();
                     });
}

void ContextMenuHub::addSeparator(QMenu* menu)
{
    if (menu)
        menu->addSeparator();
}
