#pragma once
#include <QObject>
#include <QString>
#include <geometry_global.h>

#include "CadBody.h"

namespace GeomManager
{
class GEOMETRY_EXPORT CadDocument : public QObject
{
    Q_OBJECT
  private:
    CadDocument() = default;
    ~CadDocument();

  public:
    static CadDocument* getInstance();
    void appendGeomShapeObject(CadBody* shape);
    void removeGeomShapeObject(CadBody* shape);
    bool removeGeomShapeObject(const QString& uuid);
    void clear();

    CadBody* getGeomShapeObject(const QString& uuid) const;
    bool isEmpty();

    QHash<QString, CadBody*> getShapMap()
    {
        return _geomShapeMap;
    };

  private:
    static CadDocument* _instance;
    QHash<QString, CadBody*> _geomShapeMap{};
};

using GeomManagerInstance = CadDocument;
} // namespace GeomManager
