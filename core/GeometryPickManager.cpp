#include "GeometryPickManager.h"

#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <QMetaType>
#include <QRegularExpression>

GeometryPickManager& GeometryPickManager::instance()
{
    static GeometryPickManager manager;
    return manager;
}

GeometryPickManager::GeometryPickManager(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<GeometryPickItem>("GeometryPickItem");
    qRegisterMetaType<QVector<GeometryPickItem>>("QVector<GeometryPickItem>");
}

void GeometryPickManager::setActorFactory(ActorFactory factory)
{
    m_actorFactory = std::move(factory);
}

void GeometryPickManager::clearActorFactory()
{
    m_actorFactory = ActorFactory();
    clearDomain(QString());
}

void GeometryPickManager::setPreview(const QString& domain, const QString& ownerUuid,
                                     const GeometryPickItem& item)
{
    setPreview(domain, ownerUuid, item.isValid() ? QVector<GeometryPickItem>{item}
                                                 : QVector<GeometryPickItem>{});
}

void GeometryPickManager::setPreview(const QString& domain, const QString& ownerUuid,
                                     const QVector<GeometryPickItem>& items)
{
    const QString key = stateKey(domain, ownerUuid);
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    const bool overlayExists =
        visual && visual->getProp(propId(domain, ownerUuid, true)) != nullptr;
    if (items.isEmpty())
    {
        clearPreview(domain, ownerUuid);
        return;
    }

    if (m_previewByKey.value(key) == items && overlayExists)
        return;

    m_previewByKey.insert(key, items);
    updateHighlight(domain, ownerUuid, true, items);
    emit previewChanged(domain, ownerUuid, items);
}

void GeometryPickManager::setSelection(const QString& domain, const QString& ownerUuid,
                                       const QVector<GeometryPickItem>& items)
{
    const QString key = stateKey(domain, ownerUuid);
    auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    const bool overlayExists =
        visual && visual->getProp(propId(domain, ownerUuid, false)) != nullptr;
    if (items.isEmpty())
    {
        clearSelection(domain, ownerUuid);
        return;
    }

    if (m_selectionByKey.value(key) == items && overlayExists)
        return;

    m_selectionByKey.insert(key, items);
    updateHighlight(domain, ownerUuid, false, items);
    emit selectionChanged(domain, ownerUuid, items);
}

QVector<GeometryPickItem> GeometryPickManager::selection(const QString& domain,
                                                         const QString& ownerUuid) const
{
    return m_selectionByKey.value(stateKey(domain, ownerUuid));
}

void GeometryPickManager::clearPreview(const QString& domain, const QString& ownerUuid,
                                       bool renderNow)
{
    const QString key = stateKey(domain, ownerUuid);
    if (!m_previewByKey.contains(key))
        return;

    m_previewByKey.remove(key);
    removeHighlight(domain, ownerUuid, true, renderNow);
    emit previewChanged(domain, ownerUuid, {});
}

void GeometryPickManager::clearSelection(const QString& domain, const QString& ownerUuid,
                                         bool renderNow)
{
    const QString key = stateKey(domain, ownerUuid);
    if (!m_selectionByKey.contains(key))
        return;

    m_selectionByKey.remove(key);
    removeHighlight(domain, ownerUuid, false, renderNow);
    emit selectionChanged(domain, ownerUuid, {});
}

void GeometryPickManager::clearDomain(const QString& domain, const QString& ownerUuid,
                                      bool renderNow)
{
    const QString prefix = (domain.isEmpty() && ownerUuid.isEmpty()) ? QString()
                                                                     : stateKey(domain, ownerUuid);
    const auto removeMatching = [&](QHash<QString, QVector<GeometryPickItem>>& store, bool preview)
    {
        const auto keys = store.keys();
        for (const QString& key : keys)
        {
            if (!prefix.isEmpty() && !key.startsWith(prefix))
                continue;

            const QStringList parts = key.split(QChar(0x1f));
            const QString keyDomain = parts.value(0);
            const QString keyOwner = parts.value(1);
            removeHighlight(keyDomain, keyOwner, preview, false);
            store.remove(key);
        }
    };

    removeMatching(m_previewByKey, true);
    removeMatching(m_selectionByKey, false);
    if (renderNow)
    {
        if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
            visual->render();
    }
}

QString GeometryPickManager::stateKey(const QString& domain, const QString& ownerUuid) const
{
    return domain + QChar(0x1f) + ownerUuid;
}

QString GeometryPickManager::propId(const QString& domain, const QString& ownerUuid, bool preview) const
{
    QString text = domain + "." + ownerUuid;
    text.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_.-]")), QStringLiteral("_"));
    return QStringLiteral("geometry.pick.%1.%2").arg(preview ? QStringLiteral("preview")
                                                             : QStringLiteral("selection"),
                                                     text);
}

void GeometryPickManager::updateHighlight(const QString& domain, const QString& ownerUuid,
                                          bool preview, const QVector<GeometryPickItem>& items)
{
    removeHighlight(domain, ownerUuid, preview);
    if (items.isEmpty() || !m_actorFactory)
        return;

    vtkSmartPointer<vtkProp> prop = m_actorFactory(items, preview);
    if (!prop)
        return;

    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        const QString id = propId(domain, ownerUuid, preview);
        visual->addProp(id, prop, QStringLiteral("Geometry"),
                        preview ? QStringLiteral("geometry.pick.preview")
                                : QStringLiteral("geometry.pick.selection"),
                        false, true);
    }
}

void GeometryPickManager::removeHighlight(const QString& domain, const QString& ownerUuid,
                                          bool preview, bool renderNow)
{
    if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
        visual->removeProp(propId(domain, ownerUuid, preview), renderNow);
}
