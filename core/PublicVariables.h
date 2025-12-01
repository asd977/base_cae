#ifndef PUBLICVARIABLES_H
#define PUBLICVARIABLES_H

#include <QMap>
#include <QVariant>
#include <QString>
#include <QStringList>
#include <QDebug>
#include <core_global.h>

class CORE_EXPORT PublicVariables
{
public:
    static PublicVariables& instance();

    QVariant get(const QString &key) const;

    // 写入：
    //  - 提供 ownerUuid：必要时会注册键归属（若被他人占用则失败）
    //  - 不提供 ownerUuid：仅当键已注册时才允许写入
    bool set(const QString &key, const QVariant &value, const QString &ownerUuid = QString());

    // 注册键归属（key -> ownerUuid）。若已被其他 owner 占用返回 false，并在 conflictOut 填充对方 uuid
    bool registerKey(const QString &key, const QString &ownerUuid, QString *conflictOut = nullptr);

    // 删除键（仅 ownerUuid 匹配才允许）
    bool remove(const QString &key, const QString &ownerUuid);

    // 查询
    QString ownerOf(const QString &key) const;                 // 返回 ownerUuid
    QStringList keysForOwner(const QString &ownerUuid) const;  // 某 uuid 下的所有键
    QVariantMap snapshotForOwner(const QString &ownerUuid) const;

    // 重命名键：检查全局唯一，且 oldKey 必须归属 ownerUuid
    bool renameKey(const QString &oldKey, const QString &newKey, const QString &ownerUuid, QString *conflictOut = nullptr);

    bool contains(const QString &key) const { return configMap.contains(key); }

private:
    PublicVariables() {}
    ~PublicVariables() {}

    QMap<QString, QVariant> configMap;   // key -> value
    QMap<QString, QString>  ownerMap;    // key -> ownerUuid
};

#endif // PUBLICVARIABLES_H
