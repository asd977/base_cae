#pragma once

#include <QHash>
#include <QString>

#include <services/VisualWidgetService.h>

#include <vtkSmartPointer.h>

class vtkActor;
class vtkRenderer;
class vtkRenderWindow;
class vtkRenderWindowInteractor;
class vtkScalarBarActor;

class PostProcessingWidget
{
public:
    PostProcessingWidget();

    bool addActor(vtkSmartPointer<vtkActor> actor, bool renderNow = true);
    bool removeActor(vtkSmartPointer<vtkActor> actor, bool renderNow = true);

    vtkRenderer* getRender() const;
    vtkRenderWindow* getRenderWindow() const;
    vtkRenderWindowInteractor* getInteractor() const;
    vtkScalarBarActor* getScalarBarActor() const;

private:
    QString keyFor(vtkActor* actor) const;

private:
    VisualWidgetService* m_visualService { nullptr };
    mutable QHash<vtkActor*, QString> m_actorIds;
};
