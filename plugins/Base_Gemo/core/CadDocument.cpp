#include "CadDocument.h"

#include <QtAlgorithms>

using namespace GeomManager;

CadDocument* CadDocument::_instance = nullptr;

CadDocument* GeomManager::CadDocument::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new CadDocument;
    }
    return _instance;
}

GeomManager::CadDocument::~CadDocument()
{
    clear();
}

void CadDocument::appendGeomShapeObject(CadBody* shape)
{
    if (!shape)
        return;

    const QString uuid = shape->getUUid();
    if (_geomShapeMap.contains(uuid))
    {
        CadBody* existing = _geomShapeMap.take(uuid);
        if (existing != shape)
            delete existing;
    }

    _geomShapeMap.insert(shape->getUUid(), shape);
}

void CadDocument::removeGeomShapeObject(CadBody* shape)
{
    if (!shape)
        return;

    removeGeomShapeObject(shape->getUUid());
}

bool CadDocument::removeGeomShapeObject(const QString& uuid)
{
    CadBody* object = _geomShapeMap.take(uuid);
    if (!object)
        return false;

    delete object;
    return true;
}

CadBody* CadDocument::getGeomShapeObject(const QString& uuid) const
{
    if (_geomShapeMap.contains(uuid))
    {
        return _geomShapeMap.value(uuid);
    }
    return nullptr;
}

bool CadDocument::isEmpty()
{
    return _geomShapeMap.isEmpty();
}

void CadDocument::clear()
{
    qDeleteAll(_geomShapeMap);
    _geomShapeMap.clear();
}
