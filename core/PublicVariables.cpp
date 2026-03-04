#include "PublicVariables.h"

PublicVariables &PublicVariables::instance() {
  static PublicVariables registry;
  return registry;
}

QVariant PublicVariables::get(const QString &key) const {
  auto it = configMap.constFind(key);
  return (it == configMap.constEnd()) ? QVariant() : it.value();
}

bool PublicVariables::registerKey(const QString &key, const QString &ownerUuid,
                                  QString *conflictOut) {
  if (key.isEmpty() || ownerUuid.isEmpty()) return false;

  auto it = ownerMap.constFind(key);
  if (it != ownerMap.constEnd() && it.value() != ownerUuid) {
    if (conflictOut) *conflictOut = it.value();
    return false;
  }
  ownerMap[key] = ownerUuid;
  return true;
}

bool PublicVariables::set(const QString &key, const QVariant &value,
                          const QString &ownerUuid) {
  if (!ownerUuid.isEmpty()) {
    QString conflict;
    if (!registerKey(key, ownerUuid, &conflict)) {
      qWarning() << "[PublicVariables] set failed. Key owned by:" << conflict
                 << " key=" << key;
      return false;
    }
  } else {
    if (!ownerMap.contains(key)) {
      qWarning() << "[PublicVariables] set rejected for unregistered key:"
                 << key;
      return false;
    }
  }

  configMap[key] = value;
  qDebug() << "Setting" << key << "to" << value;
  return true;
}

bool PublicVariables::remove(const QString &key, const QString &ownerUuid) {
  auto it = ownerMap.constFind(key);
  if (it == ownerMap.constEnd() || it.value() != ownerUuid) return false;

  ownerMap.remove(key);
  configMap.remove(key);
  return true;
}

QString PublicVariables::ownerOf(const QString &key) const {
  return ownerMap.value(key);
}

QStringList PublicVariables::keysForOwner(const QString &ownerUuid) const {
  QStringList r;
  for (auto it = ownerMap.constBegin(); it != ownerMap.constEnd(); ++it)
    if (it.value() == ownerUuid) r << it.key();
  return r;
}

QVariantMap PublicVariables::snapshotForOwner(const QString &ownerUuid) const {
  QVariantMap map;
  for (auto it = ownerMap.constBegin(); it != ownerMap.constEnd(); ++it)
    if (it.value() == ownerUuid)
      map.insert(it.key(), configMap.value(it.key()));
  return map;
}

bool PublicVariables::renameKey(const QString &oldKey, const QString &newKey,
                                const QString &ownerUuid,
                                QString *conflictOut) {
  if (oldKey == newKey) return true;
  if (!ownerMap.contains(oldKey) || ownerMap.value(oldKey) != ownerUuid)
    return false;

  auto itNew = ownerMap.constFind(newKey);
  if (itNew != ownerMap.constEnd() && itNew.value() != ownerUuid) {
    if (conflictOut) *conflictOut = itNew.value();
    return false;
  }

  QVariant v = configMap.value(oldKey);
  ownerMap.remove(oldKey);
  configMap.remove(oldKey);
  ownerMap[newKey] = ownerUuid;
  configMap[newKey] = v;
  return true;
}
