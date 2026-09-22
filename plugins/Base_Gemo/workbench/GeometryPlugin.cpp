#include "GeometryPlugin.h"

#include <GeometryPickManager.h>
#include <GeometryService.h>
#include <ServiceRegistry.h>

GeometryPlugin::GeometryPlugin()
{
}

void GeometryPlugin::start()
{
    if (m_geometryService || m_workbench)
        return;

    qInfo() << "启动几何模块";
    m_geometryService = new GeometryService();
    ServiceRegistry::instance().registerService<IGeometryService>(m_geometryService);
    m_workbench = new GeometryWorkbench();
}

void GeometryPlugin::stop()
{
    qInfo() << "停止几何模块";

    GeometryPickManager::instance().clearActorFactory();

    if (ServiceRegistry::instance().getService<PreProcessingService>() == m_workbench)
        ServiceRegistry::instance().registerService<PreProcessingService>(nullptr);
    if (ServiceRegistry::instance().getService<IGeometryService>() == m_geometryService)
        ServiceRegistry::instance().registerService<IGeometryService>(nullptr);

    if (m_workbench)
    {
        m_workbench->closeProject();
        delete m_workbench;
        m_workbench = nullptr;
    }

    delete m_geometryService;
    m_geometryService = nullptr;
}
