#pragma once

#include <services/VisualWidgetService.h>
#include <vtkActor.h>
#include <vtkSmartPointer.h>
#include <vtkScalarBarActor.h>
#include <vtkTextProperty.h>

#include <QString>


inline void applyStandardPostScalarBar(vtkScalarBarActor* scalarBar)
{
    if (!scalarBar)
        return;

    // 后处理各页面共用同一个 ScalarBarActor。进入普通结果页时完整恢复默认样式，
    // 避免继承云图页面的拖动位置、自定义刻度、字号等状态。
    scalarBar->SetOrientationToVertical();
    scalarBar->UnconstrainedFontSizeOn();
    scalarBar->SetWidth(0.10);
    scalarBar->SetHeight(0.65);
    scalarBar->SetPosition(0.885, 0.175);
    scalarBar->SetMaximumWidthInPixels(1000);
    scalarBar->SetMaximumHeightInPixels(2000);
    scalarBar->UseCustomLabelsOff();
    scalarBar->SetCustomLabels(nullptr);
    scalarBar->SetNumberOfLabels(7);
    scalarBar->SetLabelFormat("%.4g");
    scalarBar->DrawBackgroundOff();
    scalarBar->DrawFrameOff();

    if (vtkTextProperty* title = scalarBar->GetTitleTextProperty())
    {
        title->SetFontSize(16);
        title->SetColor(0.08, 0.08, 0.08);
        title->SetBold(1);
    }
    if (vtkTextProperty* label = scalarBar->GetLabelTextProperty())
    {
        label->SetFontSize(13);
        label->SetColor(0.08, 0.08, 0.08);
        label->SetBold(0);
    }
    scalarBar->Modified();
}

inline void applyStandardPostScalarBar(VisualWidgetService* service, const QString& title)
{
    if (!service)
        return;
    vtkScalarBarActor* scalarBar = service->scalarBarActor();
    if (!scalarBar)
        return;
    applyStandardPostScalarBar(scalarBar);
    scalarBar->SetTitle(title.toUtf8().constData());
    scalarBar->Modified();
}

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
