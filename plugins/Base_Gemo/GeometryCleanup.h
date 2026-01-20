#pragma once

#include <QString>

#include <GeomManagerInstance.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

namespace GeometryCleanup
{
inline void removeExistingGeometry(const QString& uuid, bool renderNow = true)
{
    if (uuid.isEmpty()) {
        return;
    }
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr) {
        return;
    }
    if (!mgr->getGeomShapeObject(uuid)) {
        return;
    }
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>()) {
        vis->removeProp(uuid + "_face", false);
        vis->removeProp(uuid + "_edge", renderNow);
    }
    mgr->removeGeomShapeObject(uuid);
}
}
