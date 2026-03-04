#include "LoadSpecificationWidget.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <struct/MeshMap.h>
#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellPicker.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkGeometryFilter.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkUnstructuredGrid.h>

#include <QComboBox>
#include <QDebug>

#include "PickManager.h"
#include "ui_LoadSpecificationWidget.h"

static const QString kPickSurfacePropId = QString("loadspec.pick.surface");
static const QString kPickHighlightPropId = QString("loadspec.pick.highlight");

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

    connect(ui->comboBoxDim, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LoadSpecificationWidget::onDimChanged, Qt::UniqueConnection);
    connect(ui->comboBoxGroup, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &LoadSpecificationWidget::onGroupChanged, Qt::UniqueConnection);
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
                                                    edgeKeys, true);
    }
    else if (dim == 2)
    {
        QVector<vtkIdType> faceKeys;
        for (qint64 v : map.bndFacePhysToFaceKeys.value(physId))
            faceKeys.push_back(vtkIdType(v));
        PickManager::instance().setSelectedFaceKeys(getPageType(), m_uuid, GridKind::Solid,
                                                    faceKeys, true);
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
    if (!m_visual || !m_volumeGrid || !m_renderer || !m_interactor)
        return;

    // ---------- 1) 生成外表面，并拷贝到持久对象里（关键修复点） ----------
    auto surfFilter = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
    surfFilter->SetInputData(m_volumeGrid);
    surfFilter->Update();

    m_surface = vtkSmartPointer<vtkPolyData>::New();
    m_surface->ShallowCopy(surfFilter->GetOutput()); // 关键：脱离 filter 生命周期

    if (!m_surface || m_surface->GetNumberOfCells() == 0)
    {
        qWarning() << "[LoadSpecificationWidget] surface empty.";
        return;
    }

    // ---------- 2) 外表面显示 ----------
    m_surfaceMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_surfaceMapper->SetInputData(m_surface); // 确保 mapper 有输入
    m_surfaceMapper->ScalarVisibilityOff();

    m_surfaceActor = vtkSmartPointer<vtkActor>::New();
    m_surfaceActor->SetMapper(m_surfaceMapper);
    m_surfaceActor->PickableOn();
    m_surfaceActor->GetProperty()->SetOpacity(0.25);
    m_surfaceActor->GetProperty()->EdgeVisibilityOn();
    m_surfaceActor->GetProperty()->SetLineWidth(1.0);

    m_visual->removeProp(kPickSurfacePropId, false);
    m_visual->addProp(kPickSurfacePropId, m_surfaceActor, QString("载荷"), QString("可选外表面"),
                      true, true);

    // ---------- 3) 高亮 actor：先给一个空输入（关键修复点） ----------
    auto emptyPoly = vtkSmartPointer<vtkPolyData>::New();

    m_highlightMapper = vtkSmartPointer<vtkDataSetMapper>::New();
    m_highlightMapper->SetInputData(emptyPoly); // 关键：避免 0 connections
    m_highlightMapper->ScalarVisibilityOff();

    m_highlightActor = vtkSmartPointer<vtkActor>::New();
    m_highlightActor->SetMapper(m_highlightMapper);
    m_highlightActor->PickableOff();
    m_highlightActor->GetProperty()->SetOpacity(1.0);
    m_highlightActor->GetProperty()->EdgeVisibilityOn();
    m_highlightActor->GetProperty()->SetLineWidth(2.5);
    m_highlightActor->GetProperty()->SetColor(1.0, 0.2, 0.1);
    m_highlightActor->GetProperty()->SetRenderLinesAsTubes(true);

    m_visual->removeProp(kPickHighlightPropId, false);
    m_visual->addProp(kPickHighlightPropId, m_highlightActor, QString("载荷"), QString("选中高亮"),
                      true, true);

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
        m_visual->removeProp(kPickHighlightPropId, false);
        m_visual->removeProp(kPickSurfacePropId, false);
        m_visual->render();
    }

    m_surface = nullptr;
    m_surfaceMapper = nullptr;
    m_surfaceActor = nullptr;

    m_highlightMapper = nullptr;
    m_highlightActor = nullptr;

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
    if (!m_surface || !m_highlightMapper)
        return;

    if (m_selectedSurfaceCellIds.isEmpty())
    {
        auto empty = vtkSmartPointer<vtkPolyData>::New();
        m_highlightMapper->SetInputData(empty);
        return;
    }

    auto idList = vtkSmartPointer<vtkIdList>::New();
    idList->SetNumberOfIds(m_selectedSurfaceCellIds.size());

    int k = 0;
    for (vtkIdType cid : m_selectedSurfaceCellIds)
        idList->SetId(k++, cid);

    auto extract = vtkSmartPointer<vtkExtractCells>::New();
    extract->SetInputData(m_surface);
    extract->SetCellList(idList);
    extract->Update();

    // vtkExtractCells 输出是 unstructured grid，这里转成 polydata 来渲染轮廓/面片
    auto geom = vtkSmartPointer<vtkGeometryFilter>::New();
    geom->SetInputConnection(extract->GetOutputPort());
    geom->Update();

    m_highlightMapper->SetInputData(geom->GetOutput());
}
