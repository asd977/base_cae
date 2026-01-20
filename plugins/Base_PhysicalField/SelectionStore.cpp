#include "SelectionStore.h"
#include <QDebug>

SelectionStore& SelectionStore::instance()
{
    static SelectionStore s;
    return s;
}

SelectionStore::SelectionStore(QObject* parent) : QObject(parent) {}

const QSet<int>& SelectionStore::emptyNodeSet()
{
    static QSet<int> s;
    return s;
}
const QSet<qint64>& SelectionStore::emptyFaceSet()
{
    static QSet<qint64> s;
    return s;
}

const QSet<int>& SelectionStore::nodes(const QString& domain, const QString& uuid) const
{
    auto itD = m_nodes.find(domain);
    if (itD == m_nodes.end()) return emptyNodeSet();
    auto itU = itD.value().find(uuid);
    if (itU == itD.value().end()) return emptyNodeSet();
    return itU.value();
}

const QSet<qint64>& SelectionStore::faces(const QString& domain, const QString& uuid) const
{
    auto itD = m_faces.find(domain);
    if (itD == m_faces.end()) return emptyFaceSet();
    auto itU = itD.value().find(uuid);
    if (itU == itD.value().end()) return emptyFaceSet();
    return itU.value();
}

QString SelectionStore::nodeOwner(const QString& domain, int nodeId) const
{
    auto itD = m_nodeOwner.find(domain);
    if (itD == m_nodeOwner.end()) return {};
    return itD.value().value(nodeId);
}

QString SelectionStore::faceOwner(const QString& domain, qint64 faceId) const
{
    auto itD = m_faceOwner.find(domain);
    if (itD == m_faceOwner.end()) return {};
    return itD.value().value(faceId);
}

void SelectionStore::evictNodeIfOwnedByOther(const QString& domain, const QString& newOwnerUuid, int nodeId)
{
    QString oldOwner = nodeOwner(domain, nodeId);
    if (oldOwner.isEmpty() || oldOwner == newOwnerUuid) return;

    auto& byUuid = m_nodes[domain];
    auto itSet = byUuid.find(oldOwner);
    if (itSet != byUuid.end()) {
        if (itSet.value().remove(nodeId) > 0) {
            emit selectionChanged(domain, oldOwner, PickMode::Node);
        }
    }
}

void SelectionStore::evictFaceIfOwnedByOther(const QString& domain, const QString& newOwnerUuid, qint64 faceId)
{
    QString oldOwner = faceOwner(domain, faceId);
    if (oldOwner.isEmpty() || oldOwner == newOwnerUuid) return;

    auto& byUuid = m_faces[domain];
    auto itSet = byUuid.find(oldOwner);
    if (itSet != byUuid.end()) {
        if (itSet.value().remove(faceId) > 0) {
            emit selectionChanged(domain, oldOwner, PickMode::Face);
        }
    }
}

void SelectionStore::clearNodes(const QString& domain, const QString& uuid)
{
    auto& byUuid = m_nodes[domain];
    auto it = byUuid.find(uuid);
    if (it == byUuid.end() || it.value().isEmpty()) return;

    // 清 owner 反向索引
    for (int nid : it.value()) {
        auto& ownerMap = m_nodeOwner[domain];
        if (ownerMap.value(nid) == uuid) ownerMap.remove(nid);
    }
    it.value().clear();
    emit selectionChanged(domain, uuid, PickMode::Node);
}

void SelectionStore::clearFaces(const QString& domain, const QString& uuid)
{
    auto& byUuid = m_faces[domain];
    auto it = byUuid.find(uuid);
    if (it == byUuid.end() || it.value().isEmpty()) return;

    for (qint64 fid : it.value()) {
        auto& ownerMap = m_faceOwner[domain];
        if (ownerMap.value(fid) == uuid) ownerMap.remove(fid);
    }
    it.value().clear();
    emit selectionChanged(domain, uuid, PickMode::Face);
}

void SelectionStore::removeNode(const QString& domain, const QString& uuid, int nodeId)
{
    auto& set = m_nodes[domain][uuid];
    if (set.remove(nodeId) > 0) {
        auto& ownerMap = m_nodeOwner[domain];
        if (ownerMap.value(nodeId) == uuid) ownerMap.remove(nodeId);
        emit selectionChanged(domain, uuid, PickMode::Node);
    }
}

void SelectionStore::removeFace(const QString& domain, const QString& uuid, qint64 faceId)
{
    auto& set = m_faces[domain][uuid];
    if (set.remove(faceId) > 0) {
        auto& ownerMap = m_faceOwner[domain];
        if (ownerMap.value(faceId) == uuid) ownerMap.remove(faceId);
        emit selectionChanged(domain, uuid, PickMode::Face);
    }
}

void SelectionStore::toggleNodeExclusive(const QString& domain, const QString& uuid, int nodeId)
{
    auto& set = m_nodes[domain][uuid];
    if (set.contains(nodeId)) {
        removeNode(domain, uuid, nodeId);
        return;
    }

    // ✅ 覆盖：先把旧 owner 中的同 id 移除
    evictNodeIfOwnedByOther(domain, uuid, nodeId);

    set.insert(nodeId);
    m_nodeOwner[domain][nodeId] = uuid;

    emit selectionChanged(domain, uuid, PickMode::Node);
}

void SelectionStore::toggleFaceExclusive(const QString& domain, const QString& uuid, qint64 faceId)
{
    auto& set = m_faces[domain][uuid];
    if (set.contains(faceId)) {
        removeFace(domain, uuid, faceId);
        return;
    }

    evictFaceIfOwnedByOther(domain, uuid, faceId);

    set.insert(faceId);
    m_faceOwner[domain][faceId] = uuid;

    emit selectionChanged(domain, uuid, PickMode::Face);
}

void SelectionStore::addNodesExclusive(const QString& domain, const QString& uuid, const QVector<int>& nodeIds)
{
    bool changed = false;
    auto& set = m_nodes[domain][uuid];

    for (int nid : nodeIds) {
        if (set.contains(nid)) continue;

        evictNodeIfOwnedByOther(domain, uuid, nid);
        set.insert(nid);
        m_nodeOwner[domain][nid] = uuid;
        changed = true;
    }

    if (changed) emit selectionChanged(domain, uuid, PickMode::Node);
}

void SelectionStore::addFacesExclusive(const QString& domain, const QString& uuid, const QVector<qint64>& faceIds)
{
    bool changed = false;
    auto& set = m_faces[domain][uuid];

    for (qint64 fid : faceIds) {
        if (set.contains(fid)) continue;

        evictFaceIfOwnedByOther(domain, uuid, fid);
        set.insert(fid);
        m_faceOwner[domain][fid] = uuid;
        changed = true;
    }

    if (changed) emit selectionChanged(domain, uuid, PickMode::Face);
}
