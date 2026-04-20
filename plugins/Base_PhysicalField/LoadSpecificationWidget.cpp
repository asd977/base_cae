#include "LoadSpecificationWidget.h"

#include <CustomComboBox.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSet.h>
#include <vtkCellData.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkMapper.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkUnsignedCharArray.h>

#include <QComboBox>
#include <QPushButton>
#include <QDebug>

#include "PickManager.h"
#include "ui_LoadSpecificationWidget.h"

static const QString kPickSurfacePropId = QString("loadspec.pick.surface");

LoadSpecificationWidget::LoadSpecificationWidget(QWidget* parent)
    : BasePage("LoadSpecificationWidget"), ui(new Ui::LoadSpecificationWidget)
{
    ui->setupUi(this);
}

LoadSpecificationWidget::~LoadSpecificationWidget()
{
    delete ui;
}

void LoadSpecificationWidget::beforeLoad()
{
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    m_meshCtrl = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!m_visual || !m_meshCtrl)
        return;

    m_volumeGrid = m_meshCtrl->getVtkUnstructuredGrid();
    if (!m_volumeGrid)
        return;

    // !!! 按你项目真实接口改名：renderer()/interactor()/render() !!!
    m_renderer = m_visual->renderer();
    m_interactor = m_visual->interactor();

    if (!m_renderer || !m_interactor)
    {
        qWarning() << "[LoadSpecificationWidget] renderer/interactor is null.";
        return;
    }

    // 默认不启用，等勾选框触发
    m_pickEnabled = false;

    setupBatchGroupCombo();
}

void LoadSpecificationWidget::beforeUnload()
{
    enableFacePick(false);

    m_volumeGrid = nullptr;
    m_renderer = nullptr;
    m_interactor = nullptr;
    m_visual = nullptr;
    m_meshCtrl = nullptr;
}

void LoadSpecificationWidget::on_clickCheckBox_checkStateChanged(const Qt::CheckState& arg1)
{
    enableFacePick(arg1 == Qt::Checked);
}

void LoadSpecificationWidget::setupBatchGroupCombo()
{
    ui->comboBoxDim->blockSignals(true);
    ui->comboBoxDim->clear();
    ui->comboBoxDim->addItem("dim1 线", 1);
    ui->comboBoxDim->addItem("dim2 面", 2);
    ui->comboBoxDim->setCurrentIndex(1);
    ui->comboBoxDim->blockSignals(false);

    // 防止页面切换后重复连接导致槽函数被多次触发
    QObject::disconnect(ui->comboBoxDim, nullptr, this, nullptr);
    QObject::disconnect(ui->comboBoxGroup, nullptr, this, nullptr);

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LoadSpecificationWidget::onDimChanged, Qt::UniqueConnection);
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
                    if (row < 0 || !m_meshCtrl)
                        return;
                    const MshPickMap& map = m_meshCtrl->mshPickMapSolid();
                    const int physId = ui->comboBoxGroup->itemData(row).toInt();
                    const int dim = ui->comboBoxDim->currentData().toInt();
                    if (dim == 1)
                    {
                        QVector<vtkIdType> edgeKeys;
                        for (qint64 k : map.bndEdgePhysToEdgeKeys.value(physId))
                            edgeKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   edgeKeys);
                    }
                    else
                    {
                        QVector<vtkIdType> faceKeys;
                        for (qint64 k : map.bndFacePhysToFaceKeys.value(physId))
                            faceKeys.push_back(vtkIdType(k));
                        PickManager::instance().setPreviewFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                                   faceKeys);
                    }
                },
                Qt::UniqueConnection);
        connect(groupCombo, &CustomComboBox::dropdownClosed, this,
                [this]()
                {
                    const int dim = ui->comboBoxDim->currentData().toInt();
                    PickManager::instance().clearPreviewSelection(getPageType(), m_uuid,
                                                                  dim == 1 ? PickMode::Edge : PickMode::Face);
                },
                Qt::UniqueConnection);
    }
    refreshGroupCombo();
}

void LoadSpecificationWidget::refreshGroupCombo()
{
    if (!m_meshCtrl)
        return;
    const MshPickMap& map = m_meshCtrl->mshPickMapSolid();
    const int dimFilter = ui->comboBoxDim->currentData().toInt();
    ui->comboBoxGroup->blockSignals(true);
    ui->comboBoxGroup->clear();
    for (auto it = map.physIdToName.constBegin(); it != map.physIdToName.constEnd(); ++it)
    {
        const int physId = it.key();
        if (map.physIdToDim.value(physId, -1) != dimFilter)
            continue;
        ui->comboBoxGroup->addItem(QString("%1 (id=%2)").arg(it.value()).arg(physId), physId);
    }
    ui->comboBoxGroup->blockSignals(false);
}

void LoadSpecificationWidget::onDimChanged(int)
{
    refreshGroupCombo();
}

void LoadSpecificationWidget::onGroupChanged(int index)
{
    if (index < 0 || !m_meshCtrl)
        return;
    const MshPickMap& map = m_meshCtrl->mshPickMapSolid();
    const int physId = ui->comboBoxGroup->itemData(index).toInt();
    const int dim = ui->comboBoxDim->currentData().toInt();

    if (dim == 1)
    {
        QVector<vtkIdType> edgeKeys;
        for (qint64 v : map.bndEdgePhysToEdgeKeys.value(physId))
            edgeKeys.push_back(vtkIdType(v));
        PickManager::instance().setSelectedEdgeKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    edgeKeys, false);
    }
    else if (dim == 2)
    {
        QVector<vtkIdType> faceKeys;
        for (qint64 v : map.bndFacePhysToFaceKeys.value(physId))
            faceKeys.push_back(vtkIdType(v));
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    faceKeys, false);
    }
}

void LoadSpecificationWidget::enableFacePick(bool on)
{
    if (on == m_pickEnabled)
        return;

    m_pickEnabled = on;

    if (on)
    {
        setupSurfaceAndPicker();
    }
    else
    {
        teardownSurfaceAndPicker();
    }
}

void LoadSpecificationWidget::setupSurfaceAndPicker()
{
    if (!m_visual || !m_renderer || !m_interactor)
        return;

    // ---------- 1) 优先复用当前可视化里正在显示的表面数据 ----------
    vtkActor* sourceSurfaceActor = nullptr;
    vtkDataSet* sourceSurfaceDataSet = nullptr;
    const QStringList surfacePropIds = {QStringLiteral("mesh.imported.surface"),
                                        QStringLiteral("mesh.global.surf")};
    for (const QString& propId : surfacePropIds)
    {
        vtkProp* prop = m_visual->getProp(propId);
        auto* actor = vtkActor::SafeDownCast(prop);
        if (!actor)
            continue;
        auto* mapper = vtkDataSetMapper::SafeDownCast(actor->GetMapper());
        if (!mapper || !mapper->GetInput())
            continue;
        sourceSurfaceActor = actor;
        sourceSurfaceDataSet = mapper->GetInput();
        break;
    }

    if (!sourceSurfaceDataSet)
    {
        qWarning() << "[LoadSpecificationWidget] no visible mesh surface actor for picking.";
        return;
    }
    auto* poly = vtkPolyData::SafeDownCast(sourceSurfaceDataSet);
    if (!poly)
    {
        qWarning() << "[LoadSpecificationWidget] visible surface dataset is not vtkPolyData.";
        return;
    }

    m_surface = vtkSmartPointer<vtkPolyData>::New();
    m_surface->ShallowCopy(poly);

    if (!m_surface || m_surface->GetNumberOfCells() == 0)
    {
        qWarning() << "[LoadSpecificationWidget] surface empty.";
        return;
    }

    // ---------- 2) pick actor：复用现有外表面 actor ----------
    if (sourceSurfaceActor)
    {
        m_surfaceActor = sourceSurfaceActor;
    }
    else
    {
        // 不再额外创建“可选外表面”结构，直接终止。
        qWarning() << "[LoadSpecificationWidget] missing source surface actor.";
        return;
    }

    // ---------- 3) 直接在表面 cell 上染色，不再叠加 face overlay actor ----------
    const vtkIdType nCells = m_surface->GetNumberOfCells();
    auto cellColors = vtkSmartPointer<vtkUnsignedCharArray>::New();
    cellColors->SetName("PickCellColors");
    cellColors->SetNumberOfComponents(3);
    cellColors->SetNumberOfTuples(nCells);
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const unsigned char normalColor[3] = {179, 179, 179};
        cellColors->SetTypedTuple(cid, normalColor);
    }
    m_surface->GetCellData()->SetScalars(cellColors);

    m_surfaceMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_surfaceMapper->SetInputData(m_surface);
    m_surfaceMapper->ScalarVisibilityOn();
    m_surfaceMapper->SetScalarModeToUseCellData();
    m_surfaceMapper->SetColorModeToDirectScalars();

    m_originalSurfaceMapper = m_surfaceActor->GetMapper();
    m_surfaceActor->SetMapper(m_surfaceMapper);
    m_surfaceActor->GetProperty()->SetColor(1.0, 1.0, 1.0);

    // ---------- 4) picker（只 pick surfaceActor） ----------
    m_picker = vtkSmartPointer<vtkCellPicker>::New();
    m_picker->SetTolerance(0.0005);
    m_picker->PickFromListOn();
    m_picker->InitializePickList();
    m_picker->AddPickList(m_surfaceActor);

    // ---------- 5) 左键回调 ----------
    m_leftPressCb = vtkSmartPointer<vtkCallbackCommand>::New();
    m_leftPressCb->SetClientData(this);
    m_leftPressCb->SetCallback(&LoadSpecificationWidget::OnLeftButtonPress);
    m_interactor->AddObserver(vtkCommand::LeftButtonPressEvent, m_leftPressCb);

    m_selectedSurfaceCellIds.clear();

    qDebug() << "[LoadSpecificationWidget] Face picking enabled. surfaceCells="
             << m_surface->GetNumberOfCells();

    m_visual->render();
}

void LoadSpecificationWidget::teardownSurfaceAndPicker()
{
    if (m_interactor && m_leftPressCb)
    {
        m_interactor->RemoveObserver(m_leftPressCb);
    }

    m_leftPressCb = nullptr;
    m_picker = nullptr;

    m_selectedSurfaceCellIds.clear();

    if (m_visual)
    {
        if (m_surfaceActor && m_originalSurfaceMapper)
            m_surfaceActor->SetMapper(m_originalSurfaceMapper);
        m_visual->removeProp(kPickSurfacePropId, false);
        m_visual->render();
    }

    m_surface = nullptr;
    m_surfaceMapper = nullptr;
    m_surfaceActor = nullptr;
    m_originalSurfaceMapper = nullptr;

    qDebug() << "[LoadSpecificationWidget] Face picking disabled.";
}

void LoadSpecificationWidget::OnLeftButtonPress(vtkObject* caller, unsigned long, void* clientdata,
                                                void*)
{
    auto* self = reinterpret_cast<LoadSpecificationWidget*>(clientdata);
    if (!self || !self->m_interactor)
        return;

    int xy[2];
    self->m_interactor->GetEventPosition(xy);
    self->handlePick(xy[0], xy[1]);

    // 不阻断相机交互：让后续的交互风格继续处理
    // 你的工程如果发现“点选会影响旋转/拖拽”，再在 UI 上用模式切换去控制即可。
}

void LoadSpecificationWidget::handlePick(int displayX, int displayY)
{
    if (!m_picker || !m_renderer || !m_surface)
        return;

    int ok = m_picker->Pick(displayX, displayY, 0.0, m_renderer);
    if (!ok)
        return;

    vtkIdType cellId = m_picker->GetCellId();
    if (cellId < 0 || cellId >= m_surface->GetNumberOfCells())
        return;

    vtkCell* cell = m_surface->GetCell(cellId);
    if (!cell)
        return;

    vtkIdList* ptIds = cell->GetPointIds();
    if (!ptIds)
        return;

    // 取 PointData 的 node_id（如果你在 buildGridFromModel 里写了 node_id，
    // 一般 surface 也会继承下来；若没继承，就只输出 pointId）
    vtkIntArray* nodeIdArr =
        vtkIntArray::SafeDownCast(m_surface->GetPointData()->GetArray("node_id"));

    QStringList vtkPidList, nodeIdList;
    for (vtkIdType i = 0; i < ptIds->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = ptIds->GetId(i);
        vtkPidList << QString::number(pid);

        if (nodeIdArr)
        {
            nodeIdList << QString::number(nodeIdArr->GetValue(pid));
        }
    }

    // 再点一次取消选中
    if (m_selectedSurfaceCellIds.contains(cellId))
        m_selectedSurfaceCellIds.remove(cellId);
    else
        m_selectedSurfaceCellIds.insert(cellId);

    if (nodeIdArr)
    {
        qDebug() << "[PickFace] surfaceCellId=" << cellId << " vtkPointIds={"
                 << vtkPidList.join(",") << "}"
                 << " node_id={" << nodeIdList.join(",") << "}";
    }
    else
    {
        qDebug() << "[PickFace] surfaceCellId=" << cellId << " vtkPointIds={"
                 << vtkPidList.join(",") << "}"
                 << " (no node_id array in PointData)";
    }

    rebuildHighlight();
    if (m_visual)
        m_visual->render();
}

void LoadSpecificationWidget::rebuildHighlight()
{
    if (!m_surface)
        return;

    auto* cellColors =
        vtkUnsignedCharArray::SafeDownCast(m_surface->GetCellData()->GetScalars("PickCellColors"));
    if (!cellColors)
        return;

    const vtkIdType nCells = m_surface->GetNumberOfCells();
    for (vtkIdType cid = 0; cid < nCells; ++cid)
    {
        const bool selected = m_selectedSurfaceCellIds.contains(cid);
        const unsigned char selectedColor[3] = {255, 51, 26};
        const unsigned char normalColor[3] = {179, 179, 179};
        cellColors->SetTypedTuple(cid, selected ? selectedColor : normalColor);
    }
    cellColors->Modified();
    m_surface->GetCellData()->Modified();
    m_surface->Modified();
}
