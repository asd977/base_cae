#include "GeomManagerInstance.h"

namespace GeomManager
{
	GeomManagerInstance *GeomManagerInstance::_instance = nullptr;
	GeomManagerInstance * GeomManager::GeomManagerInstance::getInstance()
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

	void GeomManagerInstance::appendGeomShapeObject(GeomShapeObject * shape)
	{
		_geomShapeMap[shape->getUUid()] = shape;
	}

    void GeomManagerInstance::removeGeomShapeObject(GeomShapeObject *shape)
    {
       _geomShapeMap.remove(shape->getUUid());
    }

	bool GeomManagerInstance::isEmpty()
	{
        bool flag = 0 < 1;
		return flag;
	}

}

