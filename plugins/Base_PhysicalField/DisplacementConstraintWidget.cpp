#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"

#include <QDebug>
#include <ServiceRegistry.h>

#include <services/MeshModuleControllerService.h>
#include <services/PhySicalFieldService.h>
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
    ui->UXLineEdit->setText("1");
    ui->UYLineEdit->setText("1");
    ui->UZLineEdit->setText("1");

}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{

}

void DisplacementConstraintWidget::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().updateStructure();
    PickManager::instance().activate(getPageType(),m_uuid,PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked,
                     this, &DisplacementConstraintWidget::onNodePicked);
}

void DisplacementConstraintWidget::beforeUnload()
{
    PickManager::instance().deactivate(getPageType(),m_uuid,PickMode::Node);
}

int DisplacementConstraintWidget::uxFlag()
{
    return ui->UXLineEdit->text().toInt();
}

int DisplacementConstraintWidget::uyFlag()
{
    return ui->UYLineEdit->text().toInt();
}

int DisplacementConstraintWidget::uzFlag()
{
    return ui->UZLineEdit->text().toInt();
}

void DisplacementConstraintWidget::onNodePicked(const QString& domain,
                                                const QString& activeUuid,
                                                GridKind,
                                                vtkIdType,
                                                int nodeId,
                                                double x, double y, double z,
                                                const QString& prevUuid)
{
    if (domain != getPageType()) return;
    if (activeUuid != m_uuid) return;

    // 更新 UI：node_id + 坐标
    ui->IDLabel->setText(QString::number(nodeId));
    ui->XLabel->setText(QString::number(x, 'g', 12));
    ui->YLabel->setText(QString::number(y, 'g', 12));
    ui->ZLabel->setText(QString::number(z, 'g', 12));

    if (!prevUuid.isEmpty())
    {
        auto* pf = ServiceRegistry::instance().getService<PhysicalFieldService>();
        const QString prevName = pf ? pf->pageTitleByUuid(prevUuid) : QString();
        ui->PrevUuidLabel->setText(prevName.isEmpty() ? prevUuid : prevName);
    }
    else
    {
        ui->PrevUuidLabel->clear();
    }
}
