#pragma once

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkProperty.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>

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
    prop->SetInterpolationToGouraud();
    prop->LightingOn();
    prop->SetAmbient(0.20);
    prop->SetDiffuse(0.78);
    prop->SetSpecular(0.04);
    prop->SetSpecularPower(12.0);
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

inline void applyPostScalarBarStyle(vtkScalarBarActor* scalarBar, const QString& title)
{
    if (!scalarBar)
        return;

    scalarBar->SetVisibility(true);
    scalarBar->SetTitle(title.toUtf8().constData());
    scalarBar->SetNumberOfLabels(6);
    scalarBar->UnconstrainedFontSizeOn();
    scalarBar->SetPosition(0.88, 0.12);
    scalarBar->SetWidth(0.10);
    scalarBar->SetHeight(0.76);

    if (auto* t = scalarBar->GetTitleTextProperty())
        t->SetColor(0.08, 0.08, 0.08);
    if (auto* l = scalarBar->GetLabelTextProperty())
        l->SetColor(0.08, 0.08, 0.08);
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
