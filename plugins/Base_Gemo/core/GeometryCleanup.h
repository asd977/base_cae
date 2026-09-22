#pragma once

#include "CadDocument.h"
#include <GeometryDisplayManager.h>

#include <QString>

namespace GeometryCleanup
{
inline void removeExistingGeometry(const QString& uuid, bool renderNow = true)
{
    if (uuid.isEmpty())
    {
        return;
    }
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        return;
    }
    if (!mgr->getGeomShapeObject(uuid))
    {
        return;
    }
    GeometryDisplayManager::removeObject(uuid, true, renderNow);
}
} // namespace GeometryCleanup
