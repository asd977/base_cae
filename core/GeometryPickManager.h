#pragma once

#include <core_global.h>

#include <QObject>
#include <QHash>
#include <QString>
#include <QVector>
#include <functional>

#include <vtkProp.h>
#include <vtkSmartPointer.h>

enum class GeometryPickMode
{
    Vertex = 0,
    Edge = 1,
    Face = 2,
    Solid = 3
};

struct CORE_EXPORT GeometryPickItem
{
    QString shapeUuid;
    GeometryPickMode mode = GeometryPickMode::Edge;
    int index = 0;

    bool operator==(const GeometryPickItem& other) const
    {
        return shapeUuid == other.shapeUuid && mode == other.mode && index == other.index;
    }

    bool isValid() const
    {
        return !shapeUuid.isEmpty() && index > 0;
    }
};

class CORE_EXPORT GeometryPickManager : public QObject
{
    Q_OBJECT

public:
    using ActorFactory = std::function<vtkSmartPointer<vtkProp>(const QVector<GeometryPickItem>& items,
                                                                 bool preview)>;

    static GeometryPickManager& instance();

    void setActorFactory(ActorFactory factory);
    void clearActorFactory();

    void setPreview(const QString& domain, const QString& ownerUuid, const GeometryPickItem& item);
    void setPreview(const QString& domain, const QString& ownerUuid, const QVector<GeometryPickItem>& items);
    void setSelection(const QString& domain, const QString& ownerUuid, const QVector<GeometryPickItem>& items);

    QVector<GeometryPickItem> selection(const QString& domain, const QString& ownerUuid) const;

    void clearPreview(const QString& domain, const QString& ownerUuid, bool renderNow = true);
    void clearSelection(const QString& domain, const QString& ownerUuid, bool renderNow = true);
    void clearDomain(const QString& domain, const QString& ownerUuid = QString(),
                     bool renderNow = true);

signals:
    void selectionChanged(const QString& domain, const QString& ownerUuid,
                          const QVector<GeometryPickItem>& items);
    void previewChanged(const QString& domain, const QString& ownerUuid,
                        const QVector<GeometryPickItem>& items);

private:
    explicit GeometryPickManager(QObject* parent = nullptr);

    QString stateKey(const QString& domain, const QString& ownerUuid) const;
    QString propId(const QString& domain, const QString& ownerUuid, bool preview) const;
    void updateHighlight(const QString& domain, const QString& ownerUuid, bool preview,
                         const QVector<GeometryPickItem>& items);
    void removeHighlight(const QString& domain, const QString& ownerUuid, bool preview,
                         bool renderNow = true);

    ActorFactory m_actorFactory;
    QHash<QString, QVector<GeometryPickItem>> m_selectionByKey;
    QHash<QString, QVector<GeometryPickItem>> m_previewByKey;
};

Q_DECLARE_METATYPE(GeometryPickItem)
Q_DECLARE_METATYPE(QVector<GeometryPickItem>)
