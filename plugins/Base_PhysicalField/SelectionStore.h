#ifndef SELECTIONSTORE_H
#define SELECTIONSTORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

// 选择模式
enum class PickMode
{
    Node,
    Face
};

/**
 * SelectionStore：保存 “domain + uuid -> 选中集合”
 * 同时提供反向索引：domain + (type,id) -> ownerUuid
 *
 * ✅ 后设置覆盖前设置（同 domain 同 type 同 id）：
 *    B uuid 选中 node 5，会自动把 A uuid 里的 node 5 移除
 *
 * ✅ 切页回来：用 uuid 就能读回选中集合并重建高亮
 */
class SelectionStore : public QObject
{
    Q_OBJECT
  public:
    static SelectionStore& instance();

    // --------- Query (只读引用，方便高效重建高亮) ---------
    const QSet<int>& nodes(const QString& domain, const QString& uuid) const;
    const QSet<qint64>& faces(const QString& domain, const QString& uuid) const;

    // --------- Mutations (写入并自动互斥覆盖) ---------
    void clearNodes(const QString& domain, const QString& uuid);
    void clearFaces(const QString& domain, const QString& uuid);

    void toggleNodeExclusive(const QString& domain, const QString& uuid, int nodeId);
    void toggleFaceExclusive(const QString& domain, const QString& uuid, qint64 faceId);

    void addNodesExclusive(const QString& domain, const QString& uuid, const QVector<int>& nodeIds);
    void addFacesExclusive(const QString& domain, const QString& uuid,
                           const QVector<qint64>& faceIds);

    void removeNode(const QString& domain, const QString& uuid, int nodeId);
    void removeFace(const QString& domain, const QString& uuid, qint64 faceId);

    // owner 查询：用于“反向读取谁占有了这个点/面”（可选）
    QString nodeOwner(const QString& domain, int nodeId) const;
    QString faceOwner(const QString& domain, qint64 faceId) const;

  signals:
    // domain/uuid 的选择集发生变化（新增/删除/清空/被覆盖导致变化）
    void selectionChanged(const QString& domain, const QString& uuid, PickMode mode);

  private:
    explicit SelectionStore(QObject* parent = nullptr);

    // ---- 存储：domain -> uuid -> set ----
    QHash<QString, QHash<QString, QSet<int>>> m_nodes;
    QHash<QString, QHash<QString, QSet<qint64>>> m_faces;

    // ---- 反向索引：domain -> id -> ownerUuid ----
    QHash<QString, QHash<int, QString>> m_nodeOwner;
    QHash<QString, QHash<qint64, QString>> m_faceOwner;

    static const QSet<int>& emptyNodeSet();
    static const QSet<qint64>& emptyFaceSet();

    // 内部：把 id 从旧 owner 中移除（如果存在）
    void evictNodeIfOwnedByOther(const QString& domain, const QString& newOwnerUuid, int nodeId);
    void evictFaceIfOwnedByOther(const QString& domain, const QString& newOwnerUuid, qint64 faceId);
};

#endif // SELECTIONSTORE_H
