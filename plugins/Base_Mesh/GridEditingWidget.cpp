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
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDataSetSurfaceFilter.h>
#include <vtkExtractCells.h>
#include <vtkIdList.h>
#include <vtkIdTypeArray.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkPointData.h>
#include <vtkPointSet.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyLine.h>
#include <vtkProperty.h>
#include <vtkProperty2D.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>
#include <vtkXMLUnstructuredGridWriter.h>

#include "MeshGridIO.h"

namespace
{
// registry ids
static const QString kIiinGroup = QString("IIIN"); // 只是 UI 分组名，可保留
static const QString kImportedMeshSurfacePropId = QString("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QString("mesh.imported.line");
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

        if (!Widget->isMousePickingEnabled())
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
    m_viewSurface = nullptr;
    m_originalRenderInput = nullptr;
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
    m_viewSurfaceNodeIdToVtkId.clear();
    m_fullElemIdToCellId.clear();

    m_colorMode = ColorMode::None;

    m_isDragging = false;
    m_dragViewPointId = -1;
    m_dragNodeId = -1;

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
        auto nodeIdLegacy = vtkIntArray::SafeDownCast(pd ? pd->GetArray("node_id") : nullptr);
        auto nodeIdPrimary = vtkIntArray::SafeDownCast(pd ? pd->GetArray("NodeId") : nullptr);
        vtkIntArray* source = nullptr;
        if (nodeIdLegacy && nodeIdLegacy->GetNumberOfTuples() == m_fullGrid->GetNumberOfPoints())
            source = nodeIdLegacy;
        else if (nodeIdPrimary && nodeIdPrimary->GetNumberOfTuples() == m_fullGrid->GetNumberOfPoints())
            source = nodeIdPrimary;

        auto nodeIdLower = ensureIntArray(pd, "node_id", m_fullGrid->GetNumberOfPoints());
        if (source)
        {
            for (vtkIdType i = 0; i < m_fullGrid->GetNumberOfPoints(); ++i)
            {
                const int v = source->GetValue(i);
                nodeIdLower->SetValue(i, v);
            }
        }
        else
        {
            for (vtkIdType i = 0; i < m_fullGrid->GetNumberOfPoints(); ++i)
            {
                const int v = static_cast<int>(i + 1);
                nodeIdLower->SetValue(i, v);
            }
        }
        if (pd && pd->GetArray("NodeId"))
            pd->RemoveArray("NodeId");
    }

    if (m_fullGrid->GetNumberOfCells() > 0)
    {
        auto* cd = m_fullGrid->GetCellData();
        auto elemIdLegacy = vtkIntArray::SafeDownCast(cd ? cd->GetArray("elem_id") : nullptr);
        auto elemIdPrimary = vtkIntArray::SafeDownCast(cd ? cd->GetArray("ElemId") : nullptr);
        vtkIntArray* source = nullptr;
        if (elemIdLegacy && elemIdLegacy->GetNumberOfTuples() == m_fullGrid->GetNumberOfCells())
            source = elemIdLegacy;
        else if (elemIdPrimary && elemIdPrimary->GetNumberOfTuples() == m_fullGrid->GetNumberOfCells())
            source = elemIdPrimary;

        auto elemIdLower = ensureIntArray(cd, "elem_id", m_fullGrid->GetNumberOfCells());
        if (source)
        {
            for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
            {
                const int v = source->GetValue(c);
                elemIdLower->SetValue(c, v);
            }
        }
        else
        {
            for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
            {
                const int v = static_cast<int>(c + 1);
                elemIdLower->SetValue(c, v);
            }
        }
        if (cd && cd->GetArray("ElemId"))
            cd->RemoveArray("ElemId");
        ensureIntArray(cd, "KETYPE", m_fullGrid->GetNumberOfCells());
        ensureIntArray(cd, "MATYPE", m_fullGrid->GetNumberOfCells());
        ensureIntArray(cd, "LAYER", m_fullGrid->GetNumberOfCells());
    }

    rebuildFullMaps();
    updateIdRangeLabels();
    updateFilterCombosFromGrid();
    updateFilterSummaryLabel();


    if (!m_interactor || !m_renderer)
    {
        setupInteractor();
    }

    buildOrUpdateViewGrid();
}

void GridEditingWidget::clearGrid()
{
    clearData();

    if (ui)
    {
        if (ui->labelNodeRangeValue)
            ui->labelNodeRangeValue->setText("--");
        if (ui->labelElemRangeValue)
            ui->labelElemRangeValue->setText("--");
        if (ui->lineEditMoveNodeId)
            ui->lineEditMoveNodeId->clear();
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
        m_fullGrid->GetPointData() ? m_fullGrid->GetPointData()->GetArray("node_id") : nullptr;
    for (vtkIdType p = 0; p < m_fullGrid->GetNumberOfPoints(); ++p)
    {
        int nid = tuple1i(nodeIdArr, p, -1);
        if (nid >= 0)
            m_fullNodeIdToVtkId.insert(nid, p);
    }

    auto* elemIdArr =
        m_fullGrid->GetCellData() ? m_fullGrid->GetCellData()->GetArray("elem_id") : nullptr;
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
        m_viewGrid->GetPointData() ? m_viewGrid->GetPointData()->GetArray("node_id") : nullptr;
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
    vtkCell* cell = m_fullGrid->GetCell(fullCellId);
    if (!cell)
        return false;
    if (cell->GetCellDimension() < 2)
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

        // 兼容 displayCurrentGrid 可能设置的其他 mapper（如 vtkPolyDataMapper）。
        // 网格编辑流程后续依赖 vtkDataSetMapper 接口，这里做一次适配替换。
        if (!m_meshMapper)
        {
            vtkMapper* rawMapper = existingActor->GetMapper();
            vtkDataSet* mapperInput = rawMapper ? vtkDataSet::SafeDownCast(rawMapper->GetInput()) : nullptr;
            if (mapperInput)
            {
                auto dsMapper = vtkSmartPointer<vtkDataSetMapper>::New();
                dsMapper->SetInputData(mapperInput);
                dsMapper->SetScalarVisibility(rawMapper->GetScalarVisibility());
                dsMapper->SetScalarMode(rawMapper->GetScalarMode());
                if (rawMapper->GetArrayName() && rawMapper->GetArrayName()[0] != '\0')
                    dsMapper->SelectColorArray(rawMapper->GetArrayName());
                if (rawMapper->GetLookupTable())
                    dsMapper->SetLookupTable(rawMapper->GetLookupTable());
                existingActor->SetMapper(dsMapper);
                m_meshMapper = dsMapper;
            }
        }
    }
    if (!m_meshMapper)
        return;
    if (!m_originalRenderInput)
        m_originalRenderInput = m_meshMapper->GetInput();

    const bool hasFilter =
        !m_filterKetypes.isEmpty() || !m_filterMatypes.isEmpty() || !m_filterLayers.isEmpty();

    // 过滤显示时，隐藏导入线网格，避免“只筛体/面但线还在”的错觉；
    // 当没有过滤或仅线兜底时再恢复。
    if (m_visualWidgetService)
        m_visualWidgetService->setVisible("mesh.imported.line", !hasFilter, false);

    // 记录 mapper 原有着色状态：避免仅因重建 viewGrid 就丢失当前颜色。
    const int prevScalarVisibility = m_meshMapper->GetScalarVisibility();
    const int prevScalarMode = m_meshMapper->GetScalarMode();
    const char* prevArrayName = m_meshMapper->GetArrayName();
    vtkScalarsToColors* prevLookupTable = m_meshMapper->GetLookupTable();

    bool usedLineFallback = false;
    {
        auto ids = vtkSmartPointer<vtkIdList>::New();
        auto lineIdsFallback = vtkSmartPointer<vtkIdList>::New();

        for (vtkIdType c = 0; c < m_fullGrid->GetNumberOfCells(); ++c)
        {
            vtkCell* cell = m_fullGrid->GetCell(c);
            if (!cell)
                continue;

            if (cell->GetCellDimension() >= 2)
            {
                if (!hasFilter || cellMatchesFilter(c))
                    ids->InsertNextId(c);
                continue;
            }

            // 兜底：当网格仅包含线单元时，允许显示线网格，避免视图为空。
            auto* cd = m_fullGrid->GetCellData();
            auto* aK = cd ? cd->GetArray("KETYPE") : nullptr;
            auto* aM = cd ? cd->GetArray("MATYPE") : nullptr;
            auto* aL = cd ? cd->GetArray("LAYER") : nullptr;
            const int k = tuple1i(aK, c, 0);
            const int m = tuple1i(aM, c, 0);
            const int l = tuple1i(aL, c, 0);
            const bool okK = m_filterKetypes.isEmpty() || m_filterKetypes.contains(k);
            const bool okM = m_filterMatypes.isEmpty() || m_filterMatypes.contains(m);
            const bool okL = m_filterLayers.isEmpty() || m_filterLayers.contains(l);
            if (okK && okM && okL)
                lineIdsFallback->InsertNextId(c);
        }

        vtkIdList* targetIds = ids;
        if (ids->GetNumberOfIds() == 0 && lineIdsFallback->GetNumberOfIds() > 0)
        {
            targetIds = lineIdsFallback;
            usedLineFallback = true;
        }

        auto ex = vtkSmartPointer<vtkExtractCells>::New();
        ex->SetInputData(m_fullGrid);
        ex->SetCellList(targetIds);
        ex->Update();
        m_viewGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
        m_viewGrid->ShallowCopy(ex->GetOutput());

        rebuildViewMaps();
    }

    // 默认复用原始渲染输入；当存在过滤条件时，临时切换到过滤后的显示数据。
    if (hasFilter)
    {
        if (usedLineFallback)
        {
            m_meshMapper->SetInputData(m_viewGrid);
        }
        else
        {
            auto surf = vtkSmartPointer<vtkDataSetSurfaceFilter>::New();
            surf->SetInputData(m_viewGrid);
            surf->PassThroughPointIdsOn();
            surf->PassThroughCellIdsOn();
            surf->Update();
            m_viewSurface = vtkSmartPointer<vtkPolyData>::New();
            m_viewSurface->ShallowCopy(surf->GetOutput());
            m_meshMapper->SetInputData(m_viewSurface);
        }
    }
    else if (m_originalRenderInput)
    {
        m_meshMapper->SetInputData(m_originalRenderInput);
    }

    if (!hasFilter || usedLineFallback)
        m_viewSurface = nullptr;
    if (m_visualWidgetService && usedLineFallback)
        m_visualWidgetService->setVisible("mesh.imported.line", true, false);
    m_viewSurfaceNodeIdToVtkId.clear();
    vtkDataSet* pickDs = m_meshMapper ? m_meshMapper->GetInput() : nullptr;
    vtkPointSet* pickPointSet = pickDs ? vtkPointSet::SafeDownCast(pickDs) : nullptr;
    m_points = pickPointSet ? pickPointSet->GetPoints()
                            : (m_viewGrid ? m_viewGrid->GetPoints() : nullptr);
    if (pickDs)
    {
        auto* nodeIdArr = pickDs->GetPointData() ? pickDs->GetPointData()->GetArray("node_id")
                                                 : nullptr;
        for (vtkIdType p = 0; p < pickDs->GetNumberOfPoints(); ++p)
        {
            const int nid = tuple1i(nodeIdArr, p, -1);
            if (nid >= 0)
                m_viewSurfaceNodeIdToVtkId.insert(nid, p);
        }
    }


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
    if (ui->comboBoxMatype)
    {
        ui->comboBoxMatype->clear();
        ui->comboBoxMatype->addItem(QString("全部"), QVariant());
        QList<int> list = collectUniqueCellInts("MATYPE").values();
        std::sort(list.begin(), list.end());
        const MshPickMap pick = MeshGridIO::instance().getMeshMap();
        for (int v : list)
        {
            const QString name = pick.physicalName(v).trimmed();
            ui->comboBoxMatype->addItem(name.isEmpty() ? QString::number(v) : name, v);
        }
        ui->comboBoxMatype->setCurrentIndex(0);
    }
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

    auto* nodeIdArr =
        m_fullGrid->GetPointData() ? m_fullGrid->GetPointData()->GetArray("node_id") : nullptr;
    auto* elemIdArr =
        m_fullGrid->GetCellData() ? m_fullGrid->GetCellData()->GetArray("elem_id") : nullptr;

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

// ===================== Picking feedback =====================

void GridEditingWidget::onPicked(long long pickedPointId, long long pickedCellId)
{
    if (!ui)
        return;
    vtkDataSet* pickDs = m_meshMapper ? m_meshMapper->GetInput() : nullptr;
    if (!pickDs)
        pickDs = vtkDataSet::SafeDownCast(m_viewGrid.GetPointer());
    if (!pickDs)
        return;
    auto* pointData = pickDs->GetPointData();
    auto* cellData = pickDs->GetCellData();

    if (pickedPointId >= 0 && pointData)
    {
        auto* nodeIdArr = vtkIntArray::SafeDownCast(pointData->GetArray("node_id"));
        if (nodeIdArr && pickedPointId < nodeIdArr->GetNumberOfTuples())
        {
            int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pickedPointId));
            if (ui->lineEditMoveNodeId)
                ui->lineEditMoveNodeId->setText(QString::number(nodeId));
        }
    }

    if (pickedCellId >= 0 && cellData)
    {
        auto* elemIdArr = vtkIntArray::SafeDownCast(cellData->GetArray("elem_id"));
        if (elemIdArr && pickedCellId < elemIdArr->GetNumberOfTuples())
        {
            int elemId = elemIdArr->GetValue(static_cast<vtkIdType>(pickedCellId));
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
        m_viewGrid->GetPointData() ? m_viewGrid->GetPointData()->GetArray("node_id") : nullptr;
    int nodeId = tuple1i(nodeIdArr, static_cast<vtkIdType>(viewPointId), -1);
    if (nodeId < 0)
        return;
    beginNodeDragByNodeId(nodeId);
}

void GridEditingWidget::beginNodeDragByNodeId(int nodeId)
{
    if (!m_fullGrid || nodeId < 0)
        return;
    auto itFull = m_fullNodeIdToVtkId.find(nodeId);
    if (itFull == m_fullNodeIdToVtkId.end())
        return;

    double p0[3];
    m_fullGrid->GetPoints()->GetPoint(itFull.value(), p0);

    m_isDragging = true;
    m_dragViewPointId = -1;
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
    vtkDataSet* pickDs = m_meshMapper ? m_meshMapper->GetInput() : nullptr;
    if (!pickDs)
        pickDs = vtkDataSet::SafeDownCast(m_viewGrid.GetPointer());
    if (!pickDs || !m_points || !m_renderer || cellId < 0)
        return;

    vtkIdType cid = static_cast<vtkIdType>(cellId);
    vtkCell* cell = pickDs->GetCell(cid);
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

    if (bestPid < 0)
        return;

    auto* nodeIdArr = pickDs->GetPointData() ? pickDs->GetPointData()->GetArray("node_id")
                                             : nullptr;
    const int nodeId = tuple1i(nodeIdArr, bestPid, -1);
    beginNodeDragByNodeId(nodeId);
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
    if (m_meshMapper)
    {
        vtkDataSet* renderDs = m_meshMapper->GetInput();
        vtkPointSet* renderPointSet = renderDs ? vtkPointSet::SafeDownCast(renderDs) : nullptr;
        if (renderPointSet && renderPointSet->GetPoints())
        {
            auto itRender = m_viewSurfaceNodeIdToVtkId.find(m_dragNodeId);
            if (itRender != m_viewSurfaceNodeIdToVtkId.end())
            {
                renderPointSet->GetPoints()->SetPoint(itRender.value(), worldPos[0], worldPos[1],
                                                      worldPos[2]);
                renderPointSet->GetPoints()->Modified();
                renderDs->Modified();
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
    if (m_meshMapper)
    {
        vtkDataSet* renderDs = m_meshMapper->GetInput();
        vtkPointSet* renderPointSet = renderDs ? vtkPointSet::SafeDownCast(renderDs) : nullptr;
        if (renderPointSet && renderPointSet->GetPoints())
        {
            auto itRender = m_viewSurfaceNodeIdToVtkId.find(nodeId);
            if (itRender != m_viewSurfaceNodeIdToVtkId.end())
            {
                renderPointSet->GetPoints()->SetPoint(itRender.value(), p);
                renderPointSet->GetPoints()->Modified();
                renderDs->Modified();
            }
        }
    }
    if (m_visualWidgetService)
        m_visualWidgetService->render();
}

void GridEditingWidget::deleteElemById(int elemId)
{
    //    if (!m_fullGrid) return;
    //    auto it = m_fullElemIdToCellId.find(elemId);
    //    if (it == m_fullElemIdToCellId.end()) { return; }
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
    qInfo() << "网格已保存到" << out;
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
