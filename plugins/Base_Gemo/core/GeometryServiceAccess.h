#pragma once

#include <IGeometryService.h>
#include <ServiceRegistry.h>

#include <QMessageBox>
#include <QObject>
#include <QWidget>

inline IGeometryService* geometryService()
{
    return ServiceRegistry::instance().getService<IGeometryService>();
}

inline IGeometryService* requireGeometryService(QWidget* owner)
{
    IGeometryService* service = geometryService();
    if (!service && owner)
    {
        QMessageBox::warning(owner,
                             QObject::tr("几何服务不可用"),
                             QObject::tr("几何服务尚未初始化，请重新加载几何模块后重试。"));
    }
    return service;
}
