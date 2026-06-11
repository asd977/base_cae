#pragma once

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>

#include <QString>

inline QString postActorId(vtkActor* actor)
{
    return QString("post_actor_%1").arg(reinterpret_cast<quintptr>(actor), 0, 16);
}

inline QString postActorId(const QString& ownerUuid, const QString& actorRole)
{
    const QString owner = ownerUuid.trimmed();
    const QString role = actorRole.trimmed();
    if (owner.isEmpty() || role.isEmpty())
        return QString();
    return QString("post.%1.%2").arg(owner, role);
}

inline bool addActorByService(VisualWidgetService* service, const vtkSmartPointer<vtkActor>& actor,
                              bool renderNow = true)
{
    if (!service || !actor)
        return false;
    const QString id = postActorId(actor.GetPointer());
    const bool ok = service->addProp(id, actor, "post", QString(), false, false);
    const bool attached = ok || service->getProp(id) == actor.GetPointer();
    if (renderNow)
        service->render();
    return attached;
}

inline bool addActorByService(VisualWidgetService* service, const QString& id,
                              const vtkSmartPointer<vtkActor>& actor, bool renderNow = true)
{
    if (!service || id.isEmpty() || !actor)
        return false;
    const bool ok = service->addProp(id, actor, "post", QString(), false, false);
    const bool attached = ok || service->getProp(id) == actor.GetPointer();
    if (renderNow)
        service->render();
    return attached;
}

inline bool removeActorByService(VisualWidgetService* service,
                                 const vtkSmartPointer<vtkActor>& actor, bool renderNow = true)
{
    if (!service || !actor)
        return false;
    const bool ok = service->removeProp(postActorId(actor.GetPointer()), false);
    if (renderNow)
        service->render();
    return ok;
}

inline bool removeActorByService(VisualWidgetService* service, const QString& id,
                                 bool renderNow = true)
{
    if (!service || id.isEmpty())
        return false;
    const bool ok = service->removeProp(id, false);
    if (renderNow)
        service->render();
    return ok;
}

inline void showPostProcessingOnly(VisualWidgetService* service, bool renderNow = true)
{
    if (!service)
        return;
    service->setGroupVisible("post", true, false);
    service->setGroupVisible("Geometry", false, false);
    service->setGroupVisible("网格", false, false);
    if (renderNow)
        service->render();
}
