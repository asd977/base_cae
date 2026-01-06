#pragma once
#include <QObject>
#include <QPointer>
#include <QVector>
#include <QVariantMap>
#include <QString>
#include <functional>
#include <core_global.h>

class QMenu;

class CORE_EXPORT ContextMenuHub
{
public:
    using AcceptFn = std::function<bool(const QVariantMap& ctx)>;
    using BuildFn  = std::function<void(QMenu* menu, const QVariantMap& ctx)>;

    struct Provider {
        QString scope;
        int priority = 0;
        QPointer<QObject> owner;   // provider 的宿主（模块对象），销毁后自动失效
        AcceptFn accept;
        BuildFn  build;
    };

    static ContextMenuHub& instance();

    void registerProvider(const QString& scope,
                          QObject* owner,
                          BuildFn build,
                          int priority = 0,
                          AcceptFn accept = {});

    void buildMenu(const QString& scope, QMenu* menu, const QVariantMap& ctx);

    static void addAction(QMenu* menu,
                          QObject* owner,
                          const QString& text,
                          std::function<void()> fn);

    static void addSeparator(QMenu* menu);

private:
    ContextMenuHub() = default;
    QVector<Provider> m_providers;
};
