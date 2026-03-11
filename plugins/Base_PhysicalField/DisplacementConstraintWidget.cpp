#include "DisplacementConstraintWidget.h"

#include <PublicConstants.h>
#include <PublicVariables.h>
#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/PhySicalFieldService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>
#include <QPushButton>

#include "ui_DisplacementConstraintWidget.h"

// VTK
#include <PickManager.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkMath.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkVertexGlyphFilter.h>

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget* parent)
    : BasePage("DisplacementConstraintWidget"), ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
    ui->UXLineEdit->setText("1");
    ui->UYLineEdit->setText("1");
    ui->UZLineEdit->setText("1");

    const int NDIM = PublicVariables::instance().get(PublicConstants::kMeshDimension).toInt();
    const bool is2D = (NDIM == 2);
    Q_UNUSED(NDIM);
    Q_UNUSED(is2D);
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
}

void DisplacementConstraintWidget::beforeLoad()
{
    PickManager::instance();
    PickManager::instance().activate(getPageType(), m_uuid, PickMode::Node);
    QObject::connect(&PickManager::instance(), &PickManager::nodePicked, this,
                     &DisplacementConstraintWidget::onNodePicked, Qt::UniqueConnection);

    // ===== 1) 从某处拿到映射（示例：meshSvc 提供 pick map）=====
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

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

    // 防止页面来回切换后重复连接，造成批量选逻辑多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    // 维度变化时仅刷新候选组
    QObject::connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                     &DisplacementConstraintWidget::onDimChanged, Qt::UniqueConnection);

    auto* batchAddBtn = findChild<QPushButton*>("btnBatchAdd");
    if (!batchAddBtn)
    {
        batchAddBtn = new QPushButton(tr("添加"), this);
        batchAddBtn->setObjectName("btnBatchAdd");
        if (auto* host = ui->comboBoxGroup->parentWidget())
        {
            if (auto* lay = host->layout())
                lay->addWidget(batchAddBtn);
        }
    }
    QObject::disconnect(batchAddBtn, nullptr, this, nullptr);
    connect(batchAddBtn, &QPushButton::clicked, this,
            [this]()
            {
                onGroupChanged(ui->comboBoxGroup->currentIndex());
            },
            Qt::UniqueConnection);

    if (auto* groupCombo = qobject_cast<CustomComboBox*>(ui->comboBoxGroup))
    {
        QObject::disconnect(groupCombo, nullptr, this, nullptr);
        connect(groupCombo, &CustomComboBox::itemHovered, this,
                [this](const int& row)
                {
                    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
                    if (!meshSvc || row < 0)
                        return;
                    const MshPickMap& map = meshSvc->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = map.physIdToDim.value(physId, -1);
                    QVector<int> pointIdsInt;
                    if (dim == 0)
                        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
                    else if (dim == 1)
                        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
                    else if (dim == 2)
                        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
                    QVector<vtkIdType> pids;
                    for (int pid : pointIdsInt)
                        pids.push_back(vtkIdType(pid));
                    PickManager::instance().setPreviewPointIds(getPageType(), m_uuid, GridKind::Solid, pids);
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid, PickMode::Node);
                },
                Qt::UniqueConnection);
    }
}

void DisplacementConstraintWidget::refreshGroupCombo(int dimFilter)
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();

    for (auto it = map.physIdToName.begin(); it != map.physIdToName.end(); ++it)
    {
        const int physId = it.key();
        const QString name = it.value();
        const int dim = map.physIdToDim.value(physId, -1);
        if (dim < 0 || dim > 2)
            continue;
        if (dimFilter >= 0 && dimFilter != dim)
            continue;

        const QString text = QString("%1  (dim=%2, id=%3)").arg(name).arg(dim).arg(physId);
        ui->comboBoxGroup->addItem(text, physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void DisplacementConstraintWidget::onDimChanged(int)
{
    refreshGroupCombo(ui->comboBoxDim->currentData().toInt());
}

void DisplacementConstraintWidget::onGroupChanged(int index)
{
    if (index < 0)
        return;

    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    const MshPickMap& map = meshSvc->mshPickMapSolid();

    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = map.physIdToDim.value(physId, -1);

    // 取出“vtk pointId 列表”
    QVector<int> pointIdsInt;
    if (dim == 0)
        pointIdsInt = map.bndPointPhysToPointIds.value(physId);
    else if (dim == 1)
        pointIdsInt = map.bndEdgePhysToPointIds.value(physId);
    else if (dim == 2)
        pointIdsInt = map.bndFacePhysToPointIds.value(physId);
    else
        return;

    qDebug() << "dim " << dim << " " << "pointIdsInt.size() " << pointIdsInt.size();
    QVector<vtkIdType> pids;
    pids.reserve(pointIdsInt.size());
    for (int pid : pointIdsInt)
        pids.push_back(vtkIdType(pid));

    // 传给 PickManager：replace = false（通过“添加”并入当前选择）
    PickManager::instance().setSelectedPointIds(getPageType(), m_uuid, GridKind::Solid, pids, false);

    // 如果你希望同步 UI（比如显示数量）
    // ui->SelectedCountLabel->setText(QString::number(pids.size()));
}

void DisplacementConstraintWidget::beforeUnload()
{
    QObject::disconnect(&PickManager::instance(), &PickManager::nodePicked, this,
                        &DisplacementConstraintWidget::onNodePicked);
    PickManager::instance().deactivate(getPageType(), m_uuid, PickMode::Node);
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

void DisplacementConstraintWidget::onNodePicked(const QString& domain, const QString& activeUuid,
                                                GridKind, vtkIdType, int nodeId, double x, double y,
                                                double z, const QString& prevUuid)
{
    if (domain != getPageType())
        return;
    if (activeUuid != m_uuid)
        return;

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
