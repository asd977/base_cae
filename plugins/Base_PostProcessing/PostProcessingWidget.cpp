#include "PostProcessingWidget.h"

#include <ServiceRegistry.h>

#include <QDebug>

#include <vtkActor.h>
#include <vtkProp.h>

PostProcessingWidget::PostProcessingWidget()
{
    m_visualService = ServiceRegistry::instance().getService<VisualWidgetService>();
    if (!m_visualService)
    {
        qWarning() << "VisualWidgetService not found for PostProcessingWidget.";
    }
}

QString PostProcessingWidget::keyFor(vtkActor* actor) const
{
    return QString("post_actor_%1").arg(reinterpret_cast<quintptr>(actor), 0, 16);
}

bool PostProcessingWidget::addActor(vtkSmartPointer<vtkActor> actor, bool renderNow)
{
    if (!m_visualService || !actor) return false;

    const QString id = keyFor(actor.GetPointer());
    if (!m_actorIds.contains(actor.GetPointer()))
    {
        const bool ok = m_visualService->addProp(id, actor, "post", QString(), false, false);
        if (!ok)
        {
            if (m_visualService->getProp(id) == actor.GetPointer())
            {
                m_actorIds.insert(actor.GetPointer(), id);
            }
            else
            {
                return false;
            }
        }
        else
        {
            m_actorIds.insert(actor.GetPointer(), id);
        }
    }

    if (renderNow) m_visualService->render();
    return true;
}

bool PostProcessingWidget::removeActor(vtkSmartPointer<vtkActor> actor, bool renderNow)
{
    if (!m_visualService || !actor) return false;

    const QString id = m_actorIds.value(actor.GetPointer(), keyFor(actor.GetPointer()));
    const bool ok = m_visualService->removeProp(id, false);
    m_actorIds.remove(actor.GetPointer());

    if (renderNow) m_visualService->render();
    return ok;
}

vtkRenderer* PostProcessingWidget::getRender() const
{
    return m_visualService ? m_visualService->renderer() : nullptr;
}

vtkRenderWindow* PostProcessingWidget::getRenderWindow() const
{
    return m_visualService ? m_visualService->renderWindow() : nullptr;
}

vtkRenderWindowInteractor* PostProcessingWidget::getInteractor() const
{
    return m_visualService ? m_visualService->interactor() : nullptr;
}

vtkScalarBarActor* PostProcessingWidget::getScalarBarActor() const
{
    return m_visualService ? m_visualService->scalarBarActor() : nullptr;
}
