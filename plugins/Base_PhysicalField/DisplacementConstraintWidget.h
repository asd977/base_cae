#pragma once

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

private slots:
    void on_clickSelectionCheckBox_checkStateChanged(const Qt::CheckState &arg1);

private:


private:
    Ui::DisplacementConstraintWidget *ui = nullptr;

};
