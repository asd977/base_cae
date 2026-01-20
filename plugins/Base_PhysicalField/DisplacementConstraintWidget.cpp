#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"

#include <QDebug>
#include <ServiceRegistry.h>

#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

// VTK
#include <vtkUnstructuredGrid.h>
#include <vtkCellPicker.h>
#include <vtkCallbackCommand.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkPointData.h>
#include <vtkIntArray.h>
#include <vtkMath.h>

#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkVertexGlyphFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <PickManager.h>

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget *parent)
    : BasePage("DisplacementConstraintWidget")
    , ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);

}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{

}

void DisplacementConstraintWidget::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(),m_uuid,PickMode::Node);
}

void DisplacementConstraintWidget::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(),m_uuid,PickMode::Node);
}

void DisplacementConstraintWidget::on_clickSelectionCheckBox_checkStateChanged(const Qt::CheckState &arg1)
{

}
