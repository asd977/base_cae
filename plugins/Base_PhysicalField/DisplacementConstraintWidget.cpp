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

#include <struct/MeshMap.h>

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
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);

    QObject::connect(&PickManager::instance(), &PickManager::nodePicked,
                     this, &DisplacementConstraintWidget::onNodePicked);

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    // 你需要在 meshSvc 里提供一个 getter：返回最近一次 msh 导入的 MshPickMap
    // 例如：const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    // ===== 2) 填充下拉框 =====
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    // 只把 dim=0/1/2 的边界组放进去（位移约束通常选边界节点）
    // 文本显示：name + dim
    for (auto it = map.physIdToName.begin(); it != map.physIdToName.end(); ++it)
    {
        const int physId = it.key();
        const QString name = it.value();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2) continue;

        const QString text = QString("%1  (dim=%2, id=%3)").arg(name).arg(dim).arg(physId);
        ui->comboBoxGroup->addItem(text, physId);
    }
    ui->comboBoxGroup->blockSignals(false);

    // 下拉框改变 -> 批量选
    QObject::connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &DisplacementConstraintWidget::onGroupChanged);
}

void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0) return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();

    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);

    // 取出“vtk pointId 列表”
    QVector<int> pointIdsInt;
    if (dim == 0)      pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1) pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2) pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else return;

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt) pids.push_back(vtkIdType(pid));

    // 传给 PickManager：replace = true（下拉框就是“一键换一组”）
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, true);

    // 如果你希望同步 UI（比如显示数量）
    // ui->SelectedCountLabel->setText(QString::number(pids.size()));
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
