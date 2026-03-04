#include "GeomManagerInstance.h"

namespace GeomManager
{
GeomManagerInstance* GeomManagerInstance::_instance = nullptr;
GeomManagerInstance* GeomManager::GeomManagerInstance::getInstance()
{
    if (_instance == nullptr)
    {
        _instance = new GeomManagerInstance;
    }
    return _instance;
}

GeomManager::GeomManagerInstance::~GeomManagerInstance()
{
}

void GeomManagerInstance::appendGeomShapeObject(GeomShapeObject* shape)
{
    _geomShapeMap[shape->getUUid()] = shape;
}

void GeomManagerInstance::removeGeomShapeObject(GeomShapeObject* shape)
{
    if (!shape)
    {
        return;
    }
    _geomShapeMap.remove(shape->getUUid());
    delete shape;
}

bool GeomManagerInstance::removeGeomShapeObject(const QString& uuid)
{
    if (uuid.isEmpty())
    {
        return false;
    }
    GeomShapeObject* shape = _geomShapeMap.value(uuid, nullptr);
    if (!shape)
    {
        return false;
    }
    _geomShapeMap.remove(uuid);
    delete shape;
    return true;
}

GeomShapeObject* GeomManagerInstance::getGeomShapeObject(const QString& uuid) const
{
    if (uuid.isEmpty())
    {
        return nullptr;
    }
    return _geomShapeMap.value(uuid, nullptr);
}

bool GeomManagerInstance::isEmpty()
{
    return _geomShapeMap.isEmpty();
}

} // namespace GeomManager
