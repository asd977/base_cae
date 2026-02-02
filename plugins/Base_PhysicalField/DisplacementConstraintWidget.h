#pragma once

#include "PickManager.h"
#include <BasePage.h>
#include <QHash>
#include <QVector>
#include <vtkObject.h>
#include <vtkSmartPointer.h>

class vtkUnstructuredGrid;
class vtkCellPicker;
class vtkCallbackCommand;
class vtkIntArray;

class vtkPoints;
class vtkPolyData;
class vtkVertexGlyphFilter;
class vtkPolyDataMapper;
class vtkActor;

class vtkRenderer;
class vtkRenderWindowInteractor;

namespace Ui { class DisplacementConstraintWidget; }

class VisualWidgetService;
class DisplacementConstraintSelectionManager;

class DisplacementConstraintWidget : public BasePage
{
    Q_OBJECT
    friend class DisplacementConstraintSelectionManager;
public:
    explicit DisplacementConstraintWidget(QWidget *parent = nullptr);
    ~DisplacementConstraintWidget() override;

    void beforeLoad() override;
    void beforeUnload() override;

    int uxFlag();
    int uyFlag();
    int uzFlag();

private slots:

    void onNodePicked(const QString &domain, const QString &activeUuid, GridKind, vtkIdType, int nodeId, double x, double y, double z, const QString &prevUuid);
private:


private:
    Ui::DisplacementConstraintWidget *ui = nullptr;

};
