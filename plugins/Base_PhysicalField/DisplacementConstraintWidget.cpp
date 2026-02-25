#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"

#include <QDebug>
#include <QSet>
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
                     this, &DisplacementConstraintWidget::onNodePicked,
                     Qt::UniqueConnection);

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    // 你需要在 meshSvc 里提供一个 getter：返回最近一次 msh 导入的 MshPickMap
    // 例如：const MshPickMap& map = meshSvc->mshPickMapSolid();
    const MshPickMap& map = meshSvc->mshPickMapSolid();

    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("全部维度", -1);
    ui->comboBoxDim->addItem("dim0 点", 0);
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(3);
    ui->comboBoxDim->blockSignals(false);

    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());

    // 下拉框改变 -> 批量选
    QObject::connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &DisplacementConstraintWidget::onDimChanged, Qt::UniqueConnection);
    QObject::connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged),
                     this, &DisplacementConstraintWidget::onGroupChanged, Qt::UniqueConnection);
    QObject::connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::activated),
                     this, &DisplacementConstraintWidget::onGroupChanged, Qt::UniqueConnection);
}


void DisplacementConstraintWidget::refreshGroupCombo(int dimFilter)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc) return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.begin(); it != map.physIdToName.end(); ++it)
    {
        const int physId = it.key();
        const QString name = it.value();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2) continue;
        if (dimFilter >= 0 && dimFilter != dim) continue;

        int pointCount = 0;
        if (dim == 0)      pointCount = map.bndPointPhysToPointIds.value(physId).size();
        else if (dim == 1) pointCount = map.bndEdgePhysToPointIds.value(physId).size();
        else if (dim == 2) pointCount = map.bndFacePhysToPointIds.value(physId).size();

        if (pointCount <= 0)
        {
            qDebug() << "[DisplacementConstraintWidget] skip empty group"
                     << "name=" << name << "physId=" << physId << "dim=" << dim;
            continue;
        }

        const QString text = QString("%1  (dim=%2, id=%3, n=%4)").arg(name).arg(dim).arg(physId).arg(pointCount);
        ui->comboBoxGroup->addItem(text, physId);
    }
    ui->comboBoxGroup->blockSignals(false);

    qDebug() << "[DisplacementConstraintWidget] refreshGroupCombo"
             << "dimFilter=" << dimFilter
             << "count=" << ui->comboBoxGroup->count();
}

void DisplacementConstraintWidget::onDimChanged(int)
{
    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());
}

void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0)
    {
        qDebug() << "[DisplacementConstraintWidget] onGroupChanged ignored: invalid index" << index;
        return;
    }

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
    else
    {
        qDebug() << "[DisplacementConstraintWidget] onGroupChanged ignored: unsupported dim"
                 << "index=" << index << "physId=" << physId << "dim=" << dim;
        return;
    }

    qDebug() << "[DisplacementConstraintWidget] onGroupChanged"
             << "index=" << index
             << "text=" << ui->comboBoxGroup->itemText(index)
             << "physId=" << physId
             << "dim=" << dim
             << "pointIds=" << pointIdsInt.size();

    if (pointIdsInt.isEmpty())
    {
        // 某些网格物理组可能 dim 标注与边界映射不一致；做一次兜底合并，避免“看起来选中了但不变色”
        const QVector<int> p0 = map.bndPointPhysToPointIds.value(physId);
        const QVector<int> p1 = map.bndEdgePhysToPointIds.value(physId);
        const QVector<int> p2 = map.bndFacePhysToPointIds.value(physId);
        QSet<int> merged;
        for (int v : p0) merged.insert(v);
        for (int v : p1) merged.insert(v);
        for (int v : p2) merged.insert(v);
        pointIdsInt = merged.values().toVector();

        qDebug() << "[DisplacementConstraintWidget] fallback merged pointIds"
                 << "physId=" << physId
                 << "dim=" << dim
                 << "point=" << p0.size()
                 << "edge=" << p1.size()
                 << "face=" << p2.size()
                 << "merged=" << pointIdsInt.size();
    }

    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt) pids.push_back(vtkIdType(pid));

    // 传给 PickManager：replace = true（下拉框就是“一键换一组”）
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, true);

    qDebug() << "[DisplacementConstraintWidget] setSelectedPointIds done"
             << "uuid=" << m_uuid
             << "replace=true"
             << "selectedCount=" << pids.size();

    // 如果你希望同步 UI（比如显示数量）
    // ui->SelectedCountLabel->setText(QString::number(pids.size()));
}


void DisplacementConstraintWidget::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked,
                        this, &DisplacementConstraintWidget::onNodePicked);
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
