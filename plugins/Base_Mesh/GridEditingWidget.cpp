#include "GridEditingWidget.h"

#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QRect>
#include <QTextStream>
#include <QVariant>
#include <algorithm>
#include <cmath>
#include <limits>

#include "ui_GridEditingWidget.h"

// services
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

// VTK
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkCell.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkCoordinate.h>
#include <vtkDataArray.h>
#include <vtkDataSetMapper.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLabeledDataMapper.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkPolyLine.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkSmartPointer.h>
#include <vtkTextProperty.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridWriter.h>

namespace
{
// registry ids
static const QString kIiinGroup = QString("IIIN"); // 只是 UI 分组名，可保留
static const QString kImportedMeshSurfacePropId = QString("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QString("mesh.imported.line");
static const QString kNodeLblPropId = QString("iiin.node_labels");
static const QString kElemLblPropId = QString("iiin.elem_labels");
static const QString kSelectBoxPropId = QString("iiin.box_selection");
} // namespace

static inline int tuple1i(vtkDataArray* a, vtkIdType id, int def = -1)
{
    if (!a)
        return def;
    if (id < 0 || id >= a->GetNumberOfTuples())
        return def;
    return static_cast<int>(a->GetTuple1(id));
}

static vtkSmartPointer<vtkIntArray> ensureIntArray(vtkFieldData* fd, const char* name,
                                                   vtkIdType nTuples)
{
    auto* arr = vtkIntArray::SafeDownCast(fd->GetArray(name));
    if (arr && arr->GetNumberOfTuples() == nTuples)
        return arr;

    auto a = vtkSmartPointer<vtkIntArray>::New();
    a->SetName(name);
    a->SetNumberOfComponents(1);
    a->SetNumberOfTuples(nTuples);
    fd->AddArray(a);
    return a;
}

// ------------------- 鼠标回调 -------------------

class GridEditingMouseCallback : public vtkCommand
{
  public:
    static GridEditingMouseCallback* New()
    {
        return new GridEditingMouseCallback;
    }

    GridEditingWidget* Widget = nullptr;
    vtkCellPicker* CellPicker = nullptr;
    vtkPointPicker* PointPicker = nullptr;

    void Execute(vtkObject* caller, unsigned long eventId, void* callData) override
    {
        Q_UNUSED(callData);

        auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
        if (!iren || !Widget)
            return;

        if (!Widget->isMousePickingEnabled() && !Widget->isLabelBoxSelectionActive())
            return;

        auto rw = iren->GetRenderWindow();
        if (!rw)
            return;

        auto rc = rw->GetRenderers();
        if (!rc || rc->GetNumberOfItems() == 0)
            return;

        vtkRenderer* renderer = rc->GetFirstRenderer();
        if (!renderer)
            return;

        int pos[2];
        iren->GetEventPosition(pos);
        int x = pos[0];
        int y = pos[1];

        // 左键按下
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            if (Widget->isLabelBoxSelectionActive())
            {
                Widget->startLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            long long pickedPoint = -1;
            long long pickedCell = -1;

            if (CellPicker)
            {
                if (CellPicker->Pick(x, y, 0, renderer))
                {
                    pickedCell = CellPicker->GetCellId();
                }
            }

            if (PointPicker)
            {
                if (PointPicker->Pick(x, y, 0, renderer))
                {
                    pickedPoint = PointPicker->GetPointId();
                }
            }

            Widget->onPicked(pickedPoint, pickedCell);

            // 拖动：以 cell 为准，从 cell 内挑最近且最前的点
            if (pickedCell >= 0)
            {
                Widget->beginNodeDragFromCell(pickedCell, x, y);
                this->AbortFlagOn();
            }
            return;
        }

        // 鼠标移动
        if (eventId == vtkCommand::MouseMoveEvent)
        {
            if (Widget->isLabelBoxSelectionActive() && Widget->isBoxSelecting())
            {
                Widget->updateLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            if (!Widget->isDraggingNode())
                return;

            if (!CellPicker)
                return;

            if (CellPicker->Pick(x, y, 0, renderer))
            {
                double wp[3];
                CellPicker->GetPickPosition(wp);
                Widget->dragNodeToWorldPosition(wp);
            }

            this->AbortFlagOn();
            return;
        }

        // 左键抬起
        if (eventId == vtkCommand::LeftButtonReleaseEvent)
        {
            if (Widget->isLabelBoxSelectionActive() && Widget->isBoxSelecting())
            {
                Widget->finishLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            if (Widget->isDraggingNode())
            {
                Widget->endNodeDrag();
                this->AbortFlagOn();
            }
            return;
        }
    }
};

// ------------------- GridEditingWidget 实现 -------------------

GridEditingWidget::GridEditingWidget(QWidget* parent)
    : BasePage("GridEditingWidget"), ui(new Ui::GridEditingWidget)
{
    ui->setupUi(this);

    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();

    updateSurfaceLabelSummary(QString("外表面编号：未生成"));

    if (ui->comboBoxColorMode)
    {
        ui->comboBoxColorMode->clear();
        ui->comboBoxColorMode->addItem(QString("默认"), static_cast<int>(ColorMode::None));
        ui->comboBoxColorMode->addItem(QString("单元类型"), static_cast<int>(ColorMode::Ketype));
        ui->comboBoxColorMode->addItem(QString("材料类型"), static_cast<int>(ColorMode::Matype));
        ui->comboBoxColorMode->addItem(QString("填筑分层"), static_cast<int>(ColorMode::Layer));
        ui->comboBoxColorMode->setCurrentIndex(0);
    }
}

GridEditingWidget::~GridEditingWidget()
{
    if (m_interactor && m_mouseCallback)
    {
        m_interactor->RemoveObserver(m_mouseCallback);
    }
    m_mouseCallback = nullptr;
    delete ui;
}

void GridEditingWidget::clearData()
{
    m_fullGrid = nullptr;
    m_viewGrid = nullptr;
    m_points = nullptr;

    m_sourceName.clear();
    m_dirty = false;

    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    m_highlightedMatypes.clear();
    m_highlightedMatype = -1;
    m_matypeLookupTable = nullptr;

    m_minNodeId = m_maxNodeId = 0;
    m_minElemId = m_maxElemId = 0;

    m_fullNodeIdToVtkId.clear();
    m_viewNodeIdToVtkId.clear();
    m_fullElemIdToCellId.clear();

    m_labelSelectionMode = LabelSelectionMode::None;
    m_isBoxSelecting = false;

    m_colorMode = ColorMode::None;

    m_isDragging = false;
    m_dragViewPointId = -1;
    m_dragNodeId = -1;

    updateSurfaceLabelSummary(QString("外表面编号：未生成"));

    if (m_visualWidgetService)
    {
        m_visualWidgetService->setVisible(kSelectBoxPropId, false, false);
    }
}

void GridEditingWidget::setEditableGrid(vtkUnstructuredGrid* grid, const QString& sourceName)
{
    clearData();
    m_sourceName = sourceName;

    if (!grid)
        return;

    m_fullGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    m_fullGrid->DeepCopy(grid);

    // 兜底：确保必要数组存在
    if (m_fullGrid->GetNumberOfPoints() > 0)
    {
        auto* pd = m_fullGrid->GetPointData();
        auto nodeId = vtkIntArray::SafeDownCast(pd ? pd->GetArray("NodeId") : nullptr);
        auto legacyNodeId = vtkIntArray::SafeDownCast(pd ? pd->GetArray("node_id") : nullptr);
        if (!nodeId || nodeId->GetNumberOfTuples() != m_fullGrid->GetNumberOfPoints())
        {
            nodeId = ensureIntArray(pd, "NodeId", m_fullGrid->GetNumberOfPoints());
            if (legacyNodeId &&
                legacyNodeId->GetNumberOfTuples() == m_fullGrid->GetNumberOfPoints())
            {
                for (vtkIdType i = 0; i < m_fullGrid->GetNumberOfPoints(); ++i)
                {
                    nodeId->SetValue(i, legacyNodeId->GetValue(i));
                }
            }
            else
            {
                for (vtkIdType i = 0; i < m_fullGrid->GetNumberOfPoints(); ++i)
                {
                    nodeId->SetValue(i, static_cast<int>(i + 1));
                }
            }
        }

        // 与导入网格常用命名保持一致，避免后续流程读到不同数组名导致映射错位。
        if (!legacyNodeId || legacyNodeId->GetNumberOfTuples() != m_fullGrid->GetNumberOfPoints())
        {
            legacyNodeId = ensureIntArray(pd, "node_id", m_fullGrid->GetNumberOfPoints());
        }
        for (vtkIdType i = 0; i < m_fullGrid->GetNumberOfPoints(); ++i)
        {
            legacyNodeId->SetValue(i, nodeId->GetValue(i));
        }
    }

    if (m_fullGrid->GetNumberOfCells() > 0)
    {
        auto* cd = m_fullGrid->GetCellData();
        auto elemId = vtkIntArray::SafeDownCast(cd ? cd->GetArray("ElemId") : nullptr);
        auto legacyElemId = vtkIntArray::SafeDownCast(cd ? cd->GetArray("elem_id") : nullptr);
        if (!elemId || elemId->GetNumberOfTuples() != m_fullGrid->GetNumberOfCells())
        {
            elemId = ensureIntArray(cd, "ElemId", m_fullGrid->GetNumberOfCells());
            if (legacyElemId &&
                legacyElemId->GetNumberOfTuples() == m_fullGrid->GetNumberOfCells())
            {
                for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
                {
                    elemId->SetValue(c, legacyElemId->GetValue(c));
                }
            }
            else
            {
                for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
                {
                    elemId->SetValue(c, static_cast<int>(c + 1));
                }
            }
        }

        // 同步旧字段命名，保证导出/显示流程中的 ID 一致。
        if (!legacyElemId || legacyElemId->GetNumberOfTuples() != m_fullGrid->GetNumberOfCells())
        {
            legacyElemId = ensureIntArray(cd, "elem_id", m_fullGrid->GetNumberOfCells());
        }
        for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
        {
            legacyElemId->SetValue(c, elemId->GetValue(c));
        }

        ensureIntArray(cd, "KETYPE", m_fullGrid->GetNumberOfCells());
        ensureIntArray(cd, "MATYPE", m_fullGrid->GetNumberOfCells());
        ensureIntArray(cd, "LAYER", m_fullGrid->GetNumberOfCells());
    }

    rebuildFullMaps();
    updateIdRangeLabels();
    updateFilterCombosFromGrid();
    updateFilterSummaryLabel();

    ensureLabelActorsCreated();

    if (!m_interactor || !m_renderer)
    {
        setupInteractor();
    }

    buildOrUpdateViewGrid();
}

void GridEditingWidget::clearGrid()
{
    if (m_visualWidgetService)
    {
        m_visualWidgetService->removeProp(kNodeLblPropId, false);
        m_visualWidgetService->removeProp(kElemLblPropId, false);
        m_visualWidgetService->setVisible(kSelectBoxPropId, false, false);
        m_visualWidgetService->render();
    }

    clearData();

    if (ui)
    {
        if (ui->labelNodeRangeValue)
            ui->labelNodeRangeValue->setText("--");
        if (ui->labelElemRangeValue)
            ui->labelElemRangeValue->setText("--");
        if (ui->lineEditMoveNodeId)
            ui->lineEditMoveNodeId->clear();
        if (ui->lineEditElemIdForLabels)
            ui->lineEditElemIdForLabels->clear();
        if (ui->lineEditDeleteElemId)
            ui->lineEditDeleteElemId->clear();
        if (ui->labelFilterSummary)
            ui->labelFilterSummary->setText(QString("当前过滤：全部"));
        if (ui->comboBoxColorMode)
            ui->comboBoxColorMode->setCurrentIndex(0);
    }
}

vtkDataArray* GridEditingWidget::pointArray(const char* name) const
{
    if (!m_viewGrid)
        return nullptr;
    auto* pd = m_viewGrid->GetPointData();
    return pd ? pd->GetArray(name) : nullptr;
}

vtkDataArray* GridEditingWidget::cellArray(const char* name) const
{
    if (!m_viewGrid)
        return nullptr;
    auto* cd = m_viewGrid->GetCellData();
    return cd ? cd->GetArray(name) : nullptr;
}

void GridEditingWidget::rebuildFullMaps()
{
    m_fullNodeIdToVtkId.clear();
    m_fullElemIdToCellId.clear();
    if (!m_fullGrid)
        return;

    auto* nodeIdArr =
        m_fullGrid->GetPointData() ? m_fullGrid->GetPointData()->GetArray("NodeId") : nullptr;
    for (vtkIdType p = 0; p < m_fullGrid->GetNumberOfPoints(); ++p)
    {
        int nid = tuple1i(nodeIdArr, p, -1);
        if (nid >= 0)
            m_fullNodeIdToVtkId.insert(nid, p);
    }

    auto* elemIdArr =
        m_fullGrid->GetCellData() ? m_fullGrid->GetCellData()->GetArray("ElemId") : nullptr;
    for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
    {
        int eid = tuple1i(elemIdArr, c, -1);
        if (eid >= 0)
            m_fullElemIdToCellId.insert(eid, c);
    }
}

void GridEditingWidget::rebuildViewMaps()
{
    m_viewNodeIdToVtkId.clear();
    if (!m_viewGrid)
        return;

    auto* nodeIdArr =
        m_viewGrid->GetPointData() ? m_viewGrid->GetPointData()->GetArray("NodeId") : nullptr;
    for (vtkIdType p = 0; p < m_viewGrid->GetNumberOfPoints(); ++p)
    {
        int nid = tuple1i(nodeIdArr, p, -1);
        if (nid >= 0)
            m_viewNodeIdToVtkId.insert(nid, p);
    }
}

QSet<int> GridEditingWidget::parseIntSetFromString(const QString& text) const
{
    QSet<int> s;
    const auto parts = text.split(',', QString::SkipEmptyParts);
    for (const QString& part : parts)
    {
        const QString v = part.trimmed();
        if (v.isEmpty())
            continue;
        bool ok = false;
        int n = v.toInt(&ok);
        if (ok)
            s.insert(n);
    }
    return s;
}

// ===================== Filter / ViewGrid =====================

bool GridEditingWidget::cellMatchesFilter(vtkIdType fullCellId) const
{
    if (!m_fullGrid)
        return false;
    auto* cd = m_fullGrid->GetCellData();
    if (!cd)
        return true;

    auto* aK = cd->GetArray("KETYPE");
    auto* aM = cd->GetArray("MATYPE");
    auto* aL = cd->GetArray("LAYER");

    int k = tuple1i(aK, fullCellId, 0);
    int m = tuple1i(aM, fullCellId, 0);
    int l = tuple1i(aL, fullCellId, 0);

    if (!m_filterKetypes.isEmpty() && !m_filterKetypes.contains(k))
        return false;
    if (!m_filterMatypes.isEmpty() && !m_filterMatypes.contains(m))
        return false;
    if (!m_filterLayers.isEmpty() && !m_filterLayers.contains(l))
        return false;
    return true;
}

void GridEditingWidget::buildOrUpdateViewGrid()
{
    if (!m_visualWidgetService || !m_fullGrid)
        return;
    if (m_fullGrid->GetNumberOfPoints() == 0 || m_fullGrid->GetNumberOfCells() == 0)
        return;

    // 找 mesh actor / mapper
    vtkActor* existingActor = nullptr;
    if (m_visualWidgetService)
    {
        existingActor =
            vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshSurfacePropId));
        if (!existingActor)
        {
            existingActor =
                vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshLinePropId));
        }
    }
    if (existingActor)
    {
        m_meshActor = existingActor;
        m_meshMapper = vtkDataSetMapper::SafeDownCast(existingActor->GetMapper());
    }
    if (!m_meshMapper)
        return;

    const bool hasFilter =
        !m_filterKetypes.isEmpty() || !m_filterMatypes.isEmpty() || !m_filterLayers.isEmpty();

    // 记录 mapper 原有着色状态：避免仅因重建 viewGrid 就丢失当前颜色。
    const int prevScalarVisibility = m_meshMapper->GetScalarVisibility();
    const int prevScalarMode = m_meshMapper->GetScalarMode();
    const char* prevArrayName = m_meshMapper->GetArrayName();
    vtkScalarsToColors* prevLookupTable = m_meshMapper->GetLookupTable();

    if (!hasFilter)
    {
        m_viewGrid = m_fullGrid;
        m_meshMapper->SetInputData(m_viewGrid);
        m_points = m_viewGrid->GetPoints();
        rebuildViewMaps();
    }
    else
    {
        auto ids = vtkSmartPointer<vtkIdList>::New();

        for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
        {
            if (cellMatchesFilter(c))
                ids->InsertNextId(c);
        }

        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(m_fullGrid);
        ex->SetCellList(ids);
        ex->Update();
        m_viewGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        m_viewGrid->ShallowCopy(ex->GetOutput());

        m_meshMapper->SetInputData(m_viewGrid);
        m_points = m_viewGrid->GetPoints();
        rebuildViewMaps();
    }

    ensureLabelActorsCreated();

    if (!m_interactor || !m_renderer)
        setupInteractor();

    if (m_cellPicker && m_meshActor)
    {
        m_cellPicker->InitializePickList();
        m_cellPicker->AddPickList(m_meshActor);
        m_cellPicker->PickFromListOn();
    }
    if (m_pointPicker && m_meshActor)
    {
        m_pointPicker->InitializePickList();
        m_pointPicker->AddPickList(m_meshActor);
        m_pointPicker->PickFromListOn();
    }

    // 仅在显式选择颜色模式或存在材料高亮 LUT 时改写 mapper 颜色状态。
    // 否则恢复 mapper 原有着色配置，避免创建/重建网格编辑时颜色丢失。
    if (m_matypeLookupTable || m_colorMode != ColorMode::None)
    {
        applyColorMode();
    }
    else
    {
        m_meshMapper->SetScalarMode(prevScalarMode);
        if (prevArrayName && prevArrayName[0] != '\0')
        {
            m_meshMapper->SelectColorArray(prevArrayName);
        }
        if (prevLookupTable)
        {
            m_meshMapper->SetLookupTable(prevLookupTable);
            m_meshMapper->UseLookupTableScalarRangeOn();
        }
        if (prevScalarVisibility)
            m_meshMapper->ScalarVisibilityOn();
        else
            m_meshMapper->ScalarVisibilityOff();
    }
    m_visualWidgetService->render();
}

// ===================== Labels actors =====================

void GridEditingWidget::ensureLabelActorsCreated()
{
    if (!m_visualWidgetService)
        return;

    if (!m_nodeLabelMapper)
    {
        auto mapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
        auto actor = vtkSmartPointer<vtkActor2D>::New();

        mapper->SetLabelModeToLabelFieldData();
        mapper->SetLabelFormat("N%d");
        actor->SetMapper(mapper);

        m_nodeLabelMapper = mapper;
        m_nodeLabelActor = actor;

        if (m_visualWidgetService->getProp(kNodeLblPropId) == nullptr)
        {
            m_visualWidgetService->addProp(kNodeLblPropId, m_nodeLabelActor, kIiinGroup,
                                           QString("节点号"), false, false);
        }
        m_visualWidgetService->setVisible(kNodeLblPropId, false, false);
    }

    if (!m_elemLabelMapper)
    {
        auto mapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
        auto actor = vtkSmartPointer<vtkActor2D>::New();

        mapper->SetLabelModeToLabelFieldData();
        mapper->SetLabelFormat("E%d");
        actor->SetMapper(mapper);

        m_elemLabelMapper = mapper;
        m_elemLabelActor = actor;

        if (m_visualWidgetService->getProp(kElemLblPropId) == nullptr)
        {
            m_visualWidgetService->addProp(kElemLblPropId, m_elemLabelActor, kIiinGroup,
                                           QString("单元号"), false, false);
        }
        m_visualWidgetService->setVisible(kElemLblPropId, false, false);
    }

    applyLabelTextStyles();
}

void GridEditingWidget::applyLabelTextStyles()
{
    if (m_nodeLabelMapper && !m_nodeLabelTextProperty)
    {
        m_nodeLabelTextProperty = vtkSmartPointer<vtkTextProperty>::New();
        m_nodeLabelTextProperty->SetColor(0.09, 0.36, 0.89);
        m_nodeLabelTextProperty->SetBold(true);
        m_nodeLabelTextProperty->SetBackgroundColor(0.95, 0.97, 1.0);
        m_nodeLabelTextProperty->SetBackgroundOpacity(0.35);
    }
    if (m_elemLabelMapper && !m_elemLabelTextProperty)
    {
        m_elemLabelTextProperty = vtkSmartPointer<vtkTextProperty>::New();
        m_elemLabelTextProperty->SetColor(0.93, 0.42, 0.08);
        m_elemLabelTextProperty->SetBold(true);
        m_elemLabelTextProperty->SetBackgroundColor(1.0, 0.97, 0.93);
        m_elemLabelTextProperty->SetBackgroundOpacity(0.35);
    }

    if (m_nodeLabelMapper && m_nodeLabelTextProperty)
    {
        m_nodeLabelMapper->SetLabelTextProperty(m_nodeLabelTextProperty);
    }
    if (m_elemLabelMapper && m_elemLabelTextProperty)
    {
        m_elemLabelMapper->SetLabelTextProperty(m_elemLabelTextProperty);
    }
}

// ===================== Interactor =====================

void GridEditingWidget::setupInteractor()
{
    if (!m_visualWidgetService)
        return;
    if (m_interactor)
        return;

    m_interactor = m_visualWidgetService->interactor();
    m_renderer = m_visualWidgetService->renderer();
    if (!m_interactor || !m_renderer)
        return;

    m_cellPicker = vtkSmartPointer<vtkCellPicker>::New();
    m_pointPicker = vtkSmartPointer<vtkPointPicker>::New();

    auto cb = vtkSmartPointer<GridEditingMouseCallback>::New();
    cb->Widget = this;
    cb->CellPicker = m_cellPicker;
    cb->PointPicker = m_pointPicker;

    m_mouseCallback = cb;

    double priority = 1.0;
    m_interactor->AddObserver(vtkCommand::LeftButtonPressEvent, cb, priority);
    m_interactor->AddObserver(vtkCommand::MouseMoveEvent, cb, priority);
    m_interactor->AddObserver(vtkCommand::LeftButtonReleaseEvent, cb, priority);
}

// ===================== UI: filter =====================

void GridEditingWidget::on_pushButtonApplyFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    if (ui->comboBoxKetype)
    {
        QVariant data = ui->comboBoxKetype->currentData();
        if (data.isValid())
            m_filterKetypes.insert(data.toInt());
    }
    if (ui->comboBoxMatype)
    {
        QVariant data = ui->comboBoxMatype->currentData();
        if (data.isValid())
            m_filterMatypes.insert(data.toInt());
    }
    if (ui->comboBoxLayer)
    {
        QVariant data = ui->comboBoxLayer->currentData();
        if (data.isValid())
            m_filterLayers.insert(data.toInt());
    }

    updateFilterSummaryLabel();
    buildOrUpdateViewGrid();
}

void GridEditingWidget::on_pushButtonResetFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    if (ui->comboBoxKetype)
        ui->comboBoxKetype->setCurrentIndex(0);
    if (ui->comboBoxMatype)
        ui->comboBoxMatype->setCurrentIndex(0);
    if (ui->comboBoxLayer)
        ui->comboBoxLayer->setCurrentIndex(0);

    updateFilterSummaryLabel();
    buildOrUpdateViewGrid();
}

QSet<int> GridEditingWidget::collectUniqueCellInts(const char* arrayName) const
{
    QSet<int> s;
    if (!m_fullGrid)
        return s;
    auto* cd = m_fullGrid->GetCellData();
    auto* a = cd ? cd->GetArray(arrayName) : nullptr;
    if (!a)
        return s;

    for (vtkIdType i = 0; i < a->GetNumberOfTuples(); ++i)
    {
        s.insert(static_cast<int>(a->GetTuple1(i)));
    }
    return s;
}

void GridEditingWidget::updateFilterCombosFromGrid()
{
    if (!ui)
        return;

    auto fillCombo = [](QComboBox* combo, const QSet<int>& values)
    {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(QString("全部"), QVariant());
        QList<int> list = values.values();
        std::sort(list.begin(), list.end());
        for (int v : list)
            combo->addItem(QString::number(v), v);
        combo->setCurrentIndex(0);
    };

    fillCombo(ui->comboBoxKetype, collectUniqueCellInts("KETYPE"));
    fillCombo(ui->comboBoxMatype, collectUniqueCellInts("MATYPE"));
    fillCombo(ui->comboBoxLayer, collectUniqueCellInts("LAYER"));
}

void GridEditingWidget::updateFilterSummaryLabel()
{
    if (!ui || !ui->labelFilterSummary)
        return;

    auto describeSet = [](const QSet<int>& values)
    {
        if (values.isEmpty())
            return QString("全部");
        QList<int> list = values.values();
        std::sort(list.begin(), list.end());
        QStringList strList;
        for (int v : list)
            strList << QString::number(v);
        return strList.join(',');
    };

    const QString summary = QString("当前过滤：KETYPE(%1) | MATYPE(%2) | LAYER(%3)")
                                .arg(describeSet(m_filterKetypes))
                                .arg(describeSet(m_filterMatypes))
                                .arg(describeSet(m_filterLayers));

    ui->labelFilterSummary->setText(summary);
}

void GridEditingWidget::updateSurfaceLabelSummary(const QString& text)
{
    if (ui && ui->labelSurfaceLabelSummary)
    {
        ui->labelSurfaceLabelSummary->setText(text);
    }
}

// ===================== UI: show labels by box =====================

void GridEditingWidget::on_pushButtonShowNodeLabels_clicked()
{
    beginLabelBoxSelection(LabelSelectionMode::SurfaceNode);
}

void GridEditingWidget::on_pushButtonShowElemLabel_clicked()
{
    beginLabelBoxSelection(LabelSelectionMode::SurfaceElem);
}

void GridEditingWidget::on_pushButtonShowSurfaceLabels_clicked()
{
    beginLabelBoxSelection(LabelSelectionMode::SurfaceBoth);
}

void GridEditingWidget::beginLabelBoxSelection(LabelSelectionMode mode)
{
    m_labelSelectionMode = mode;
    m_isBoxSelecting = false;

    if (m_visualWidgetService)
    {
        m_visualWidgetService->setVisible(kNodeLblPropId, false, false);
        m_visualWidgetService->setVisible(kElemLblPropId, false, false);
        m_visualWidgetService->setVisible(kSelectBoxPropId, false, false);
        m_visualWidgetService->render();
    }

    QString hint;
    switch (mode)
    {
    case LabelSelectionMode::SurfaceNode:
        hint = QString("外表面编号：请在视图区框选（网格点号）");
        break;
    case LabelSelectionMode::SurfaceElem:
        hint = QString("外表面编号：请在视图区框选（单元号）");
        break;
    case LabelSelectionMode::SurfaceBoth:
        hint = QString("外表面编号：请在视图区框选（节点+单元）");
        break;
    default:
        hint = QString("外表面编号：未生成");
        break;
    }
    updateSurfaceLabelSummary(hint);
}

void GridEditingWidget::ensureSelectionBoxActorCreated()
{
    if (!m_visualWidgetService || m_boxSelectionActor)
        return;

    m_boxSelectionPoly = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(5);
    m_boxSelectionPoly->SetPoints(points);

    auto polyLine = vtkSmartPointer<vtkPolyLine>::New();
    polyLine->GetPointIds()->SetNumberOfIds(5);
    for (vtkIdType i = 0; i < 5; ++i)
    {
        polyLine->GetPointIds()->SetId(i, i);
    }

    auto lines = vtkSmartPointer<vtkCellArray>::New();
    lines->InsertNextCell(polyLine);
    m_boxSelectionPoly->SetLines(lines);

    m_boxSelectionCoord = vtkSmartPointer<vtkCoordinate>::New();
    m_boxSelectionCoord->SetCoordinateSystemToDisplay();

    m_boxSelectionMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    m_boxSelectionMapper->SetTransformCoordinate(m_boxSelectionCoord);
    m_boxSelectionMapper->SetInputData(m_boxSelectionPoly);

    m_boxSelectionActor = vtkSmartPointer<vtkActor2D>::New();
    m_boxSelectionActor->SetMapper(m_boxSelectionMapper);
    m_boxSelectionActor->GetProperty()->SetColor(0.05, 0.55, 0.95);
    m_boxSelectionActor->GetProperty()->SetLineWidth(1.5);

    if (m_visualWidgetService->getProp(kSelectBoxPropId) == nullptr)
    {
        m_visualWidgetService->addProp(kSelectBoxPropId, m_boxSelectionActor, kIiinGroup,
                                       QString("框选区域"), false, false);
    }
    m_visualWidgetService->setVisible(kSelectBoxPropId, false, false);
}

void GridEditingWidget::updateSelectionBoxActor(const QRect& rect)
{
    if (!m_boxSelectionPoly)
        return;
    auto points = m_boxSelectionPoly->GetPoints();
    if (!points)
        return;

    const double minX = rect.left();
    const double maxX = rect.right();
    const double minY = rect.top();
    const double maxY = rect.bottom();

    points->SetPoint(0, minX, minY, 0.0);
    points->SetPoint(1, maxX, minY, 0.0);
    points->SetPoint(2, maxX, maxY, 0.0);
    points->SetPoint(3, minX, maxY, 0.0);
    points->SetPoint(4, minX, minY, 0.0);
    points->Modified();
    m_boxSelectionPoly->Modified();
}

void GridEditingWidget::startLabelBoxSelection(int x, int y)
{
    if (m_labelSelectionMode == LabelSelectionMode::None)
        return;

    m_isBoxSelecting = true;
    m_boxStart[0] = x;
    m_boxStart[1] = y;
    m_boxEnd[0] = x;
    m_boxEnd[1] = y;

    ensureSelectionBoxActorCreated();
    const QRect rect(QPoint(x, y), QPoint(x, y));
    updateSelectionBoxActor(rect);

    if (m_visualWidgetService)
    {
        m_visualWidgetService->setVisible(kSelectBoxPropId, true, false);
        m_visualWidgetService->render();
    }
}

void GridEditingWidget::updateLabelBoxSelection(int x, int y)
{
    if (!m_isBoxSelecting)
        return;

    m_boxEnd[0] = x;
    m_boxEnd[1] = y;

    const int minX = std::min(m_boxStart[0], m_boxEnd[0]);
    const int maxX = std::max(m_boxStart[0], m_boxEnd[0]);
    const int minY = std::min(m_boxStart[1], m_boxEnd[1]);
    const int maxY = std::max(m_boxStart[1], m_boxEnd[1]);
    const QRect rect(QPoint(minX, minY), QPoint(maxX, maxY));
    updateSelectionBoxActor(rect);

    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void GridEditingWidget::finishLabelBoxSelection(int x, int y)
{
    if (!m_isBoxSelecting)
        return;

    m_boxEnd[0] = x;
    m_boxEnd[1] = y;
    m_isBoxSelecting = false;

    const int minX = std::min(m_boxStart[0], m_boxEnd[0]);
    const int maxX = std::max(m_boxStart[0], m_boxEnd[0]);
    const int minY = std::min(m_boxStart[1], m_boxEnd[1]);
    const int maxY = std::max(m_boxStart[1], m_boxEnd[1]);
    const QRect rect(QPoint(minX, minY), QPoint(maxX, maxY));

    updateSelectionBoxActor(rect);

    if (m_visualWidgetService)
    {
        m_visualWidgetService->setVisible(kSelectBoxPropId, false, false);
    }

    showSurfaceLabelsInRect(rect, m_labelSelectionMode);
    m_labelSelectionMode = LabelSelectionMode::None;
}

// 视图内“外表面”判定：用 viewGrid bounds 做 bbox 表面节点（和你原逻辑一致）
void GridEditingWidget::showSurfaceLabelsInRect(const QRect& rect, LabelSelectionMode mode)
{
    if (!m_visualWidgetService)
    {
        updateSurfaceLabelSummary(QString("外表面编号：渲染服务不可用"));
        return;
    }
    ensureLabelActorsCreated();
    applyLabelTextStyles();

    vtkRenderer* renderer = m_visualWidgetService->renderer();
    if (!renderer || !m_viewGrid)
    {
        updateSurfaceLabelSummary(QString("外表面编号：缺少渲染器或网格"));
        return;
    }

    const bool showNodes =
        (mode == LabelSelectionMode::SurfaceNode || mode == LabelSelectionMode::SurfaceBoth);
    const bool showElems =
        (mode == LabelSelectionMode::SurfaceElem || mode == LabelSelectionMode::SurfaceBoth);

    auto projectToDisplay = [&](const double world[3], double& sx, double& sy, double& sz) -> bool
    {
        renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
        renderer->WorldToDisplay();
        double* dp = renderer->GetDisplayPoint();
        sx = dp[0];
        sy = dp[1];
        sz = dp[2];
        return std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sz);
    };

    const int minX = rect.left();
    const int maxX = rect.right();
    const int minY = rect.top();
    const int maxY = rect.bottom();

    auto inRect = [&](double sx, double sy)
    {
        return sx >= minX && sx <= maxX && sy >= minY && sy <= maxY;
    };

    double bounds[6];
    m_viewGrid->GetBounds(bounds);
    const double xmin = bounds[0], xmax = bounds[1];
    const double ymin = bounds[2], ymax = bounds[3];
    const double zmin = bounds[4], zmax = bounds[5];
    const double span = std::max({xmax - xmin, ymax - ymin, zmax - zmin});
    const double eps = std::max(1e-9, span * 1e-6);

    auto isBoundaryPoint = [&](const double p[3])
    {
        return (std::abs(p[0] - xmin) <= eps) || (std::abs(p[0] - xmax) <= eps) ||
               (std::abs(p[1] - ymin) <= eps) || (std::abs(p[1] - ymax) <= eps) ||
               (std::abs(p[2] - zmin) <= eps) || (std::abs(p[2] - zmax) <= eps);
    };

    auto* nodeIdArr =
        m_viewGrid->GetPointData() ? m_viewGrid->GetPointData()->GetArray("NodeId") : nullptr;
    auto* elemIdArr =
        m_viewGrid->GetCellData() ? m_viewGrid->GetCellData()->GetArray("ElemId") : nullptr;

    if (!nodeIdArr || !elemIdArr)
    {
        updateSurfaceLabelSummary(QString("外表面编号：缺少 NodeId 或 ElemId 数组"));
        m_visualWidgetService->setVisible(kNodeLblPropId, false, false);
        m_visualWidgetService->setVisible(kElemLblPropId, false, true);
        return;
    }

    const double binSize = 60.0; // 分桶像素，避免过密
    const int maxLabels = maxSurfaceLabelCount();

    struct Cand
    {
        double wx, wy, wz;
        double depth;
        int id;
    };

    auto pickFrontmost = [&](QMap<QPair<int, int>, Cand>& buckets, int bx, int by, const Cand& c)
    {
        QPair<int, int> key(bx, by);
        auto it = buckets.find(key);
        if (it == buckets.end() || c.depth < it->depth)
        {
            buckets.insert(key, c);
        }
    };

    // -------- nodes --------
    int selectedNodeCount = 0;
    if (showNodes)
    {
        QMap<QPair<int, int>, Cand> nodeBuckets;

        for (vtkIdType pid = 0; pid < m_viewGrid->GetNumberOfPoints(); ++pid)
        {
            double p[3];
            m_viewGrid->GetPoint(pid, p);
            if (!isBoundaryPoint(p))
                continue;

            double sx = 0, sy = 0, sz = 0;
            if (!projectToDisplay(p, sx, sy, sz))
                continue;
            if (!inRect(sx, sy))
                continue;

            int nid = tuple1i(nodeIdArr, pid, -1);
            if (nid < 0)
                continue;

            const int bx = static_cast<int>(std::floor(sx / binSize));
            const int by = static_cast<int>(std::floor(sy / binSize));
            pickFrontmost(nodeBuckets, bx, by, {p[0], p[1], p[2], sz, nid});
        }

        QVector<Cand> nodes = nodeBuckets.values().toVector();
        std::sort(nodes.begin(), nodes.end(),
                  [](const Cand& a, const Cand& b)
                  {
                      return a.id < b.id;
                  });

        if (nodes.size() > maxLabels)
        {
            const int step = std::max(1, (nodes.size() + maxLabels - 1) / maxLabels);
            QVector<Cand> trimmed;
            for (int i = 0; i < nodes.size(); i += step)
                trimmed.push_back(nodes[i]);
            nodes.swap(trimmed);
        }
        selectedNodeCount = nodes.size();

        auto poly = vtkSmartPointer<vtkPolyData>::New();
        auto pts = vtkSmartPointer<vtkPoints>::New();
        auto idA = vtkSmartPointer<vtkIntArray>::New();
        idA->SetName("NodeId");
        idA->SetNumberOfComponents(1);

        for (const auto& c : nodes)
        {
            pts->InsertNextPoint(c.wx, c.wy, c.wz);
            idA->InsertNextValue(c.id);
        }
        poly->SetPoints(pts);
        poly->GetPointData()->AddArray(idA);

        m_nodeLabelMapper->SetInputData(poly);
        m_nodeLabelMapper->SetFieldDataName("NodeId");
        m_visualWidgetService->setVisible(kNodeLblPropId, pts->GetNumberOfPoints() > 0, false);
    }
    else
    {
        m_visualWidgetService->setVisible(kNodeLblPropId, false, false);
    }

    // -------- elems --------
    int selectedElemCount = 0;
    if (showElems)
    {
        QMap<QPair<int, int>, Cand> elemBuckets;

        for (vtkIdType cid = 0; cid < m_viewGrid->GetNumberOfCells(); ++cid)
        {
            vtkCell* cell = m_viewGrid->GetCell(cid);
            if (!cell)
                continue;

            vtkIdList* pids = cell->GetPointIds();
            if (!pids || pids->GetNumberOfIds() == 0)
                continue;

            bool onSurface = false;
            double cx = 0, cy = 0, cz = 0;
            int cnt = 0;

            for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
            {
                double p[3];
                m_viewGrid->GetPoint(pids->GetId(i), p);
                cx += p[0];
                cy += p[1];
                cz += p[2];
                ++cnt;
                if (isBoundaryPoint(p))
                    onSurface = true;
            }
            if (!onSurface || cnt == 0)
                continue;

            double center[3] = {cx / cnt, cy / cnt, cz / cnt};
            double sx = 0, sy = 0, sz = 0;
            if (!projectToDisplay(center, sx, sy, sz))
                continue;
            if (!inRect(sx, sy))
                continue;

            int eid = tuple1i(elemIdArr, cid, -1);
            if (eid < 0)
                continue;

            const int bx = static_cast<int>(std::floor(sx / binSize));
            const int by = static_cast<int>(std::floor(sy / binSize));
            pickFrontmost(elemBuckets, bx, by, {center[0], center[1], center[2], sz, eid});
        }

        QVector<Cand> elems = elemBuckets.values().toVector();
        std::sort(elems.begin(), elems.end(),
                  [](const Cand& a, const Cand& b)
                  {
                      return a.id < b.id;
                  });

        if (elems.size() > maxLabels)
        {
            const int step = std::max(1, (elems.size() + maxLabels - 1) / maxLabels);
            QVector<Cand> trimmed;
            for (int i = 0; i < elems.size(); i += step)
                trimmed.push_back(elems[i]);
            elems.swap(trimmed);
        }
        selectedElemCount = elems.size();

        auto poly = vtkSmartPointer<vtkPolyData>::New();
        auto pts = vtkSmartPointer<vtkPoints>::New();
        auto idA = vtkSmartPointer<vtkIntArray>::New();
        idA->SetName("ElemId");
        idA->SetNumberOfComponents(1);

        for (const auto& c : elems)
        {
            pts->InsertNextPoint(c.wx, c.wy, c.wz);
            idA->InsertNextValue(c.id);
        }
        poly->SetPoints(pts);
        poly->GetPointData()->AddArray(idA);

        m_elemLabelMapper->SetInputData(poly);
        m_elemLabelMapper->SetFieldDataName("ElemId");
        m_visualWidgetService->setVisible(kElemLblPropId, pts->GetNumberOfPoints() > 0, false);
    }
    else
    {
        m_visualWidgetService->setVisible(kElemLblPropId, false, false);
    }

    const QString summary = QString("外表面编号：框选节点 %1，框选单元 %2")
                                .arg(showNodes ? selectedNodeCount : 0)
                                .arg(showElems ? selectedElemCount : 0);
    updateSurfaceLabelSummary(summary);

    m_visualWidgetService->render();
}

// ===================== Picking feedback =====================

void GridEditingWidget::onPicked(long long pickedPointId, long long pickedCellId)
{
    if (!m_viewGrid || !ui)
        return;

    auto* pointData = m_viewGrid->GetPointData();
    auto* cellData = m_viewGrid->GetCellData();

    if (pickedPointId >= 0 && pointData)
    {
        auto* nodeIdArr = vtkIntArray::SafeDownCast(pointData->GetArray("NodeId"));
        if (nodeIdArr && pickedPointId < nodeIdArr->GetNumberOfTuples())
        {
            int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pickedPointId));
            if (ui->lineEditMoveNodeId)
                ui->lineEditMoveNodeId->setText(QString::number(nodeId));
        }
    }

    if (pickedCellId >= 0 && cellData)
    {
        auto* elemIdArr = vtkIntArray::SafeDownCast(cellData->GetArray("ElemId"));
        if (elemIdArr && pickedCellId < elemIdArr->GetNumberOfTuples())
        {
            int elemId = elemIdArr->GetValue(static_cast<vtkIdType>(pickedCellId));
            if (ui->lineEditElemIdForLabels)
                ui->lineEditElemIdForLabels->setText(QString::number(elemId));
            if (ui->lineEditDeleteElemId)
                ui->lineEditDeleteElemId->setText(QString::number(elemId));
        }
    }
}

// ===================== Drag node =====================

void GridEditingWidget::beginNodeDragByPointId(long long viewPointId)
{
    if (!m_viewGrid || !m_fullGrid || viewPointId < 0)
        return;

    auto* nodeIdArr =
        m_viewGrid->GetPointData() ? m_viewGrid->GetPointData()->GetArray("NodeId") : nullptr;
    int nodeId = tuple1i(nodeIdArr, static_cast<vtkIdType>(viewPointId), -1);
    if (nodeId < 0)
        return;

    auto itFull = m_fullNodeIdToVtkId.find(nodeId);
    if (itFull == m_fullNodeIdToVtkId.end())
        return;

    double p0[3];
    m_fullGrid->GetPoints()->GetPoint(itFull.value(), p0);

    m_isDragging = true;
    m_dragViewPointId = viewPointId;
    m_dragNodeId = nodeId;

    m_dragStartX = p0[0];
    m_dragStartY = p0[1];
    m_dragStartZ = p0[2];

    if (ui)
    {
        if (ui->lineEditMoveNodeId)
            ui->lineEditMoveNodeId->setText(QString::number(nodeId));
        if (ui->lineEditNodeX0)
            ui->lineEditNodeX0->setText(QString::number(m_dragStartX));
        if (ui->lineEditNodeY0)
            ui->lineEditNodeY0->setText(QString::number(m_dragStartY));
        if (ui->lineEditNodeZ0)
            ui->lineEditNodeZ0->setText(QString::number(m_dragStartZ));

        if (ui->lineEditNodeXCur)
            ui->lineEditNodeXCur->setText(QString::number(m_dragStartX));
        if (ui->lineEditNodeYCur)
            ui->lineEditNodeYCur->setText(QString::number(m_dragStartY));
        if (ui->lineEditNodeZCur)
            ui->lineEditNodeZCur->setText(QString::number(m_dragStartZ));
    }
}

void GridEditingWidget::beginNodeDragFromCell(long long cellId, int displayX, int displayY)
{
    if (!m_viewGrid || !m_points || !m_renderer || cellId < 0)
        return;

    vtkIdType cid = static_cast<vtkIdType>(cellId);
    vtkCell* cell = m_viewGrid->GetCell(cid);
    if (!cell)
        return;

    vtkIdList* ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() == 0)
        return;

    double bestDepth = 1e9;
    vtkIdType bestPid = -1;
    const double pixelTol2 = 15.0 * 15.0;

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = ids->GetId(i);

        double world[3];
        m_points->GetPoint(pid, world);

        m_renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
        m_renderer->WorldToDisplay();
        double disp[3];
        m_renderer->GetDisplayPoint(disp);

        double dx = disp[0] - displayX;
        double dy = disp[1] - displayY;
        double d2 = dx * dx + dy * dy;

        if (d2 > pixelTol2)
            continue;

        if (disp[2] < bestDepth)
        {
            bestDepth = disp[2];
            bestPid = pid;
        }
    }

    if (bestPid >= 0)
    {
        beginNodeDragByPointId(static_cast<long long>(bestPid));
    }
}

void GridEditingWidget::dragNodeToWorldPosition(const double worldPos[3])
{
    if (!m_isDragging || !m_fullGrid)
        return;

    auto itFull = m_fullNodeIdToVtkId.find(m_dragNodeId);
    if (itFull == m_fullNodeIdToVtkId.end())
        return;

    auto* fullPts = m_fullGrid->GetPoints();
    if (!fullPts)
        return;

    fullPts->SetPoint(itFull.value(), worldPos[0], worldPos[1], worldPos[2]);
    fullPts->Modified();
    m_fullGrid->Modified();

    // 同步 view
    if (m_viewGrid)
    {
        auto itView = m_viewNodeIdToVtkId.find(m_dragNodeId);
        if (itView != m_viewNodeIdToVtkId.end())
        {
            auto* viewPts = m_viewGrid->GetPoints();
            if (viewPts)
            {
                viewPts->SetPoint(itView.value(), worldPos[0], worldPos[1], worldPos[2]);
                viewPts->Modified();
                m_viewGrid->Modified();
            }
        }
    }

    m_dirty = true;

    if (m_visualWidgetService)
        m_visualWidgetService->render();

    if (ui)
    {
        if (ui->lineEditNodeXCur)
            ui->lineEditNodeXCur->setText(QString::number(worldPos[0]));
        if (ui->lineEditNodeYCur)
            ui->lineEditNodeYCur->setText(QString::number(worldPos[1]));
        if (ui->lineEditNodeZCur)
            ui->lineEditNodeZCur->setText(QString::number(worldPos[2]));
    }
}

void GridEditingWidget::endNodeDrag()
{
    if (!m_isDragging)
        return;

    m_isDragging = false;
    m_dragViewPointId = -1;
    m_dragNodeId = -1;

    m_dirty = true;
}

// ===================== Manual move / delete =====================

void GridEditingWidget::moveNodeById(int nodeId, const double* newX, const double* newY,
                                     const double* newZ)
{
    if (!m_fullGrid)
        return;

    auto itFull = m_fullNodeIdToVtkId.find(nodeId);
    if (itFull == m_fullNodeIdToVtkId.end())
    {
        qWarning() << "[GridEditingWidget] moveNodeById: node not found:" << nodeId;
        return;
    }

    double p[3];
    m_fullGrid->GetPoints()->GetPoint(itFull.value(), p);

    if (newX)
        p[0] = *newX;
    if (newY)
        p[1] = *newY;
    if (newZ)
        p[2] = *newZ;

    m_fullGrid->GetPoints()->SetPoint(itFull.value(), p);
    m_fullGrid->GetPoints()->Modified();
    m_fullGrid->Modified();

    // view 同步
    if (m_viewGrid)
    {
        auto itView = m_viewNodeIdToVtkId.find(nodeId);
        if (itView != m_viewNodeIdToVtkId.end())
        {
            m_viewGrid->GetPoints()->SetPoint(itView.value(), p);
            m_viewGrid->GetPoints()->Modified();
            m_viewGrid->Modified();
        }
    }

    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void GridEditingWidget::deleteElemById(int elemId)
{
    //    if (!m_fullGrid) return;

    //    auto it = m_fullElemIdToCellId.find(elemId);
    //    if (it == m_fullElemIdToCellId.end()) {
    //        qWarning() << "[GridEditingWidget] deleteElemById: elem not found:"
    //        << elemId; return;
    //    }
    //    vtkIdType delCid = it.value();

    //    // 通用稳妥：重建 fullGrid（避免不同 VTK 版本 DeleteCell 支持差异）
    //    auto keep = vtkSmartPointer<vtkIdTypeArray>::New();
    //    keep->SetNumberOfComponents(1);
    //    for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c) {
    //        if (c == delCid) continue;
    //        keep->InsertNextValue(c);
    //    }

    //    auto ex = vtkSmartPointer<vtkExtractCells>::New();
    //    ex->SetInputData(m_fullGrid);
    //    ex->SetCellList(keep);
    //    ex->Update();

    //    auto newFull = vtkSmartPointer<vtkUnstructuredGrid>::New();
    //    newFull->ShallowCopy(ex->GetOutput());
    //    m_fullGrid = newFull;

    //    m_dirty = true;

    //    rebuildFullMaps();
    //    updateIdRangeLabels();
    //    updateFilterCombosFromGrid();
    //    buildOrUpdateViewGrid();
}

void GridEditingWidget::on_pushButtonMoveNode_clicked()
{
    if (!ui || !m_fullGrid)
        return;

    bool ok = false;
    int nodeId = ui->lineEditMoveNodeId ? ui->lineEditMoveNodeId->text().trimmed().toInt(&ok) : 0;
    if (!ok)
        return;

    QString sx = ui->lineEditNewX ? ui->lineEditNewX->text().trimmed() : QString();
    QString sy = ui->lineEditNewY ? ui->lineEditNewY->text().trimmed() : QString();
    QString sz = ui->lineEditNewZ ? ui->lineEditNewZ->text().trimmed() : QString();

    double vx = 0, vy = 0, vz = 0;
    const double *px = nullptr, *py = nullptr, *pz = nullptr;

    if (!sx.isEmpty())
    {
        vx = sx.toDouble(&ok);
        if (!ok)
            return;
        px = &vx;
    }
    if (!sy.isEmpty())
    {
        vy = sy.toDouble(&ok);
        if (!ok)
            return;
        py = &vy;
    }
    if (!sz.isEmpty())
    {
        vz = sz.toDouble(&ok);
        if (!ok)
            return;
        pz = &vz;
    }

    moveNodeById(nodeId, px, py, pz);
    m_dirty = true;
}

void GridEditingWidget::on_pushButtonDeleteElem_clicked()
{
    if (!ui || !m_fullGrid)
        return;

    bool ok = false;
    int elemId =
        ui->lineEditDeleteElemId ? ui->lineEditDeleteElemId->text().trimmed().toInt(&ok) : 0;
    if (!ok)
        return;

    deleteElemById(elemId);
}

// ===================== ID ranges =====================

void GridEditingWidget::updateIdRangeLabels()
{
    if (!ui || !m_fullGrid)
        return;

    auto* pd = m_fullGrid->GetPointData();
    auto* cd = m_fullGrid->GetCellData();

    auto* nodeIdArr = pd ? pd->GetArray("NodeId") : nullptr;
    auto* elemIdArr = cd ? cd->GetArray("ElemId") : nullptr;

    if (!nodeIdArr || nodeIdArr->GetNumberOfTuples() == 0)
    {
        if (ui->labelNodeRangeValue)
            ui->labelNodeRangeValue->setText("--");
    }
    else
    {
        int minN = std::numeric_limits<int>::max();
        int maxN = std::numeric_limits<int>::min();
        for (vtkIdType i = 0; i < nodeIdArr->GetNumberOfTuples(); ++i)
        {
            int v = static_cast<int>(nodeIdArr->GetTuple1(i));
            minN = std::min(minN, v);
            maxN = std::max(maxN, v);
        }
        m_minNodeId = minN;
        m_maxNodeId = maxN;
        if (ui->labelNodeRangeValue)
            ui->labelNodeRangeValue->setText(QString("%1 ~ %2").arg(minN).arg(maxN));
    }

    if (!elemIdArr || elemIdArr->GetNumberOfTuples() == 0)
    {
        if (ui->labelElemRangeValue)
            ui->labelElemRangeValue->setText("--");
    }
    else
    {
        int minE = std::numeric_limits<int>::max();
        int maxE = std::numeric_limits<int>::min();
        for (vtkIdType i = 0; i < elemIdArr->GetNumberOfTuples(); ++i)
        {
            int v = static_cast<int>(elemIdArr->GetTuple1(i));
            minE = std::min(minE, v);
            maxE = std::max(maxE, v);
        }
        m_minElemId = minE;
        m_maxElemId = maxE;
        if (ui->labelElemRangeValue)
            ui->labelElemRangeValue->setText(QString("%1 ~ %2").arg(minE).arg(maxE));
    }
}

// ===================== Opacity / Wireframe / Picking toggle
// =====================

QStringList GridEditingWidget::availableMeshPropIds() const
{
    QStringList ids;
    if (!m_visualWidgetService)
        return ids;

    if (m_visualWidgetService->getProp(kImportedMeshSurfacePropId))
        ids << kImportedMeshSurfacePropId;
    if (m_visualWidgetService->getProp(kImportedMeshLinePropId))
        ids << kImportedMeshLinePropId;
    return ids;
}

void GridEditingWidget::on_checkBoxEnablePicking_toggled(bool checked)
{
    m_enableMousePicking = checked;
}

// ===================== Color mode / LUT =====================

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildLookupTable(const QSet<int>& values) const
{
    int maxValue = -1;
    for (int v : values)
        maxValue = qMax(maxValue, v);
    if (maxValue < 0)
        return nullptr;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxValue + 1);
    lut->SetTableRange(0.0, maxValue);
    lut->Build();

    for (int i = 0; i <= maxValue; ++i)
    {
        if (values.contains(i))
        {
            QColor c = QColor::fromHsv((i * 37) % 360, 170, 220);
            lut->SetTableValue(i, c.redF(), c.greenF(), c.blueF(), 1.0);
        }
        else
        {
            lut->SetTableValue(i, 0.85, 0.85, 0.85, 1.0);
        }
    }
    return lut;
}

void GridEditingWidget::applyColorMode()
{
    if (!m_meshMapper || !m_visualWidgetService || !m_viewGrid)
        return;

    // 如果外部高亮 MATYPE LUT 生效，则优先用它（除非你想强制覆盖）
    if (m_matypeLookupTable)
    {
        m_meshMapper->SetScalarModeToUseCellFieldData();
        m_meshMapper->SelectColorArray("MATYPE");
        m_meshMapper->SetLookupTable(m_matypeLookupTable);
        m_meshMapper->UseLookupTableScalarRangeOn();
        m_meshMapper->ScalarVisibilityOn();
        m_visualWidgetService->render();
        return;
    }

    QString arrayName;
    QSet<int> values;

    switch (m_colorMode)
    {
    case ColorMode::Ketype:
        arrayName = "KETYPE";
        values = collectUniqueCellInts("KETYPE");
        break;
    case ColorMode::Matype:
        arrayName = "MATYPE";
        values = collectUniqueCellInts("MATYPE");
        break;
    case ColorMode::Layer:
        arrayName = "LAYER";
        values = collectUniqueCellInts("LAYER");
        break;
    case ColorMode::None:
    default:
        m_meshMapper->ScalarVisibilityOff();
        m_visualWidgetService->render();
        return;
    }

    if (values.isEmpty())
    {
        m_meshMapper->ScalarVisibilityOff();
        m_visualWidgetService->render();
        return;
    }

    auto lut = buildLookupTable(values);
    if (!lut)
    {
        m_meshMapper->ScalarVisibilityOff();
        m_visualWidgetService->render();
        return;
    }

    m_meshMapper->SetScalarModeToUseCellFieldData();
    m_meshMapper->SelectColorArray(arrayName.toUtf8().constData());
    m_meshMapper->SetLookupTable(lut);
    m_meshMapper->UseLookupTableScalarRangeOn();
    m_meshMapper->ScalarVisibilityOn();
    m_visualWidgetService->render();
}

void GridEditingWidget::on_comboBoxColorMode_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    if (!ui || !ui->comboBoxColorMode)
        return;

    QVariant data = ui->comboBoxColorMode->currentData();
    if (data.isValid())
    {
        m_colorMode = static_cast<ColorMode>(data.toInt());
    }
    else
    {
        m_colorMode = ColorMode::None;
    }

    // 清掉 highlight LUT（你也可以选择不清）
    m_matypeLookupTable = nullptr;

    applyColorMode();
}

// 外部调用：高亮 MATYPE（不依赖 IiinGridModel）
void GridEditingWidget::highlightMatypes(const QSet<int>& matypes, int selectedMatype,
                                         int previewMatype, int preDeleteMatype)
{
    m_highlightedMatypes = matypes;
    m_highlightedMatype = selectedMatype;

    if (!m_meshMapper || !m_viewGrid || !m_visualWidgetService)
    {
        return;
    }

    const bool hasSelection =
        !matypes.isEmpty() || selectedMatype >= 0 || previewMatype >= 0 || preDeleteMatype >= 0;

    if (!hasSelection)
    {
        m_matypeLookupTable = nullptr;
        applyColorMode();
        return;
    }

    // 最大 MATYPE 取值：从 viewGrid 的 MATYPE 数组扫一遍 + 参数补充
    int maxMatype = -1;
    auto* aM = m_viewGrid->GetCellData() ? m_viewGrid->GetCellData()->GetArray("MATYPE") : nullptr;
    if (aM)
    {
        for (vtkIdType i = 0; i < aM->GetNumberOfTuples(); ++i)
        {
            maxMatype = qMax(maxMatype, static_cast<int>(aM->GetTuple1(i)));
        }
    }
    maxMatype = qMax(maxMatype, selectedMatype);
    maxMatype = qMax(maxMatype, previewMatype);
    maxMatype = qMax(maxMatype, preDeleteMatype);
    for (int v : matypes)
        maxMatype = qMax(maxMatype, v);

    if (maxMatype < 0)
        return;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxMatype + 1);
    lut->SetTableRange(0.0, maxMatype);
    lut->Build();

    // 默认灰
    for (int i = 0; i <= maxMatype; ++i)
    {
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);
    }
    // 集合绿
    for (int matype : matypes)
    {
        if (matype >= 0 && matype <= maxMatype)
        {
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
        }
    }
    // 选中红
    if (selectedMatype >= 0 && selectedMatype <= maxMatype)
    {
        lut->SetTableValue(selectedMatype, 0.9, 0.2, 0.2, 1.0);
    }
    // 预览蓝
    if (previewMatype >= 0 && previewMatype <= maxMatype)
    {
        lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
    }
    // 待删橙
    if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype)
    {
        lut->SetTableValue(preDeleteMatype, 0.9, 0.55, 0.2, 1.0);
    }

    m_matypeLookupTable = lut;

    m_meshMapper->SetScalarModeToUseCellFieldData();
    m_meshMapper->SelectColorArray("MATYPE");
    m_meshMapper->SetLookupTable(lut);
    m_meshMapper->UseLookupTableScalarRangeOn();
    m_meshMapper->ScalarVisibilityOn();

    m_visualWidgetService->render();
}

// ===================== Save =====================

bool GridEditingWidget::saveAsVtu(const QString& filePath)
{
    if (!m_fullGrid || filePath.isEmpty())
        return false;

    auto writer = vtkSmartPointer<vtkXMLUnstructuredGridWriter>::New();
    writer->SetInputData(m_fullGrid);
#if VTK_MAJOR_VERSION <= 5
    writer->SetFileName(filePath.toLocal8Bit().constData());
#else
    writer->SetFileName(filePath.toUtf8().constData());
#endif
    const int ok = writer->Write();
    return ok != 0;
}

void GridEditingWidget::on_pushButtonSave_clicked()
{
    if (!m_fullGrid || m_fullGrid->GetNumberOfCells() == 0)
    {
        qWarning() << "[GridEditingWidget] nothing to save.";
        return;
    }

    // 默认保存：如果 sourceName 是有效路径，就直接写；否则弹窗保存 .vtu
    QString out = m_sourceName;
    const bool hasPath = !out.isEmpty() && (out.contains('/') || out.contains('\\'));

    if (!hasPath || !out.endsWith(".vtu", Qt::CaseInsensitive))
    {
        QString defaultName = out;
        if (defaultName.isEmpty())
            defaultName = "edited_mesh.vtu";
        if (!defaultName.endsWith(".vtu", Qt::CaseInsensitive))
            defaultName += ".vtu";

        out = QFileDialog::getSaveFileName(this, QString("保存网格"), defaultName,
                                           QString("VTK Unstructured Grid (*.vtu)"));
        if (out.isEmpty())
            return;
        if (!out.endsWith(".vtu", Qt::CaseInsensitive))
            out += ".vtu";
    }

    if (!saveAsVtu(out))
    {
        qWarning() << "[GridEditingWidget] save vtu failed:" << out;
        return;
    }

    m_dirty = false;
    qDebug() << "[GridEditingWidget] saved to" << out;
}

// ===================== beforeLoad/unload =====================

void GridEditingWidget::beforeLoad()
{
    // 这里不再从 IiinGridModel 拉数据
    // 你可以在外部调用 setEditableGrid(...) 注入当前网格
}

void GridEditingWidget::beforeUnload()
{
}

// ===================== 单个单元标签显示（可选，用于你原先的两个按钮逻辑扩展）
// =====================

void GridEditingWidget::showNodeLabelsForElem(int elemId)
{
    if (!m_visualWidgetService || !m_nodeLabelMapper || !m_fullGrid)
        return;

    auto it = m_fullElemIdToCellId.find(elemId);
    if (it == m_fullElemIdToCellId.end())
    {
        m_visualWidgetService->setVisible(kNodeLblPropId, false, true);
        return;
    }

    vtkIdType cid = it.value();
    vtkCell* cell = m_fullGrid->GetCell(cid);
    if (!cell)
        return;

    auto* nodeIdArr =
        m_fullGrid->GetPointData() ? m_fullGrid->GetPointData()->GetArray("NodeId") : nullptr;
    if (!nodeIdArr)
        return;

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto idArr = vtkSmartPointer<vtkIntArray>::New();
    idArr->SetName("NodeId");
    idArr->SetNumberOfComponents(1);

    vtkIdList* pids = cell->GetPointIds();
    for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = pids->GetId(i);

        double p[3];
        m_fullGrid->GetPoints()->GetPoint(pid, p);

        int nid = tuple1i(nodeIdArr, pid, -1);
        if (nid < 0)
            continue;

        points->InsertNextPoint(p);
        idArr->InsertNextValue(nid);
    }

    poly->SetPoints(points);
    poly->GetPointData()->AddArray(idArr);

    m_nodeLabelMapper->SetInputData(poly);
    m_nodeLabelMapper->SetFieldDataName("NodeId");
    m_visualWidgetService->setVisible(kNodeLblPropId, points->GetNumberOfPoints() > 0, true);
}

void GridEditingWidget::showElemLabelForElem(int elemId)
{
    if (!m_visualWidgetService || !m_elemLabelMapper || !m_fullGrid)
        return;

    auto it = m_fullElemIdToCellId.find(elemId);
    if (it == m_fullElemIdToCellId.end())
    {
        m_visualWidgetService->setVisible(kElemLblPropId, false, true);
        return;
    }

    vtkIdType cid = it.value();
    vtkCell* cell = m_fullGrid->GetCell(cid);
    if (!cell)
        return;

    vtkIdList* pids = cell->GetPointIds();
    if (!pids || pids->GetNumberOfIds() == 0)
        return;

    double cx = 0, cy = 0, cz = 0;
    for (vtkIdType i = 0; i < pids->GetNumberOfIds(); ++i)
    {
        double p[3];
        m_fullGrid->GetPoints()->GetPoint(pids->GetId(i), p);
        cx += p[0];
        cy += p[1];
        cz += p[2];
    }
    cx /= pids->GetNumberOfIds();
    cy /= pids->GetNumberOfIds();
    cz /= pids->GetNumberOfIds();

    auto poly = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto idArr = vtkSmartPointer<vtkIntArray>::New();
    idArr->SetName("ElemId");
    idArr->SetNumberOfComponents(1);

    points->InsertNextPoint(cx, cy, cz);
    idArr->InsertNextValue(elemId);

    poly->SetPoints(points);
    poly->GetPointData()->AddArray(idArr);

    m_elemLabelMapper->SetInputData(poly);
    m_elemLabelMapper->SetFieldDataName("ElemId");
    m_visualWidgetService->setVisible(kElemLblPropId, true, true);
}
