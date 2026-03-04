#pragma once

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include <QString>

inline QString postActorId(vtkActor* actor)
{
    return QString("post_actor_%1").arg(reinterpret_cast<quintptr>(actor), 0, 16);
}

inline void applyPostMapperStyle(vtkMapper* mapper)
{
    if (!mapper)
        return;
    mapper->InterpolateScalarsBeforeMappingOn();
}

inline void applyPostActorStyle(vtkActor* actor)
{
    if (!actor)
        return;

    auto* prop = actor->GetProperty();
    if (!prop)
        return;

    prop->SetRepresentationToSurface();
    prop->SetInterpolationToPhong();
    prop->LightingOn();
    prop->SetAmbient(0.15);
    prop->SetDiffuse(0.85);
    prop->SetSpecular(0.08);
    prop->SetSpecularPower(20.0);
    prop->EdgeVisibilityOff();
}

inline void applyPostLookupTableStyle(vtkLookupTable* lut)
{
    if (!lut)
        return;
    lut->SetNumberOfTableValues(256);
    lut->SetHueRange(0.667, 0.0);
    lut->SetSaturationRange(1.0, 1.0);
    lut->SetValueRange(1.0, 1.0);
    lut->Build();
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
