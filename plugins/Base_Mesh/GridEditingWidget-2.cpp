#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <QColor>
#include <QComboBox>
#include <QDebug>
#include <QFileDialog>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>

#include "GridEditingWidget.h"
#include "MeshGridIO.h"
#include "ui_GridEditingWidget.h"

// VTK
#include <vtkActor.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellPicker.h>
#include <vtkCommand.h>
#include <vtkDataSetMapper.h>
#include <vtkIdList.h>
#include <vtkIntArray.h>
#include <vtkLookupTable.h>
#include <vtkPointData.h>
#include <vtkPointPicker.h>
#include <vtkPoints.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkRendererCollection.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

namespace
{
// registry ids
static const QString kIiinGroup = QString("IIIN");
static const QString kImportedMeshSurfacePropId = QString("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QString("mesh.imported.line");
} // namespace

// ------------------- 鼠标回调（仅保留拾取 + 拖拽） -------------------

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

        // 左键按下：拾取 & 开始拖拽
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            long long pickedPoint = -1;
            long long pickedCell = -1;

            if (CellPicker && CellPicker->Pick(x, y, 0, renderer))
            {
                pickedCell = CellPicker->GetCellId();
            }
            if (PointPicker && PointPicker->Pick(x, y, 0, renderer))
            {
                pickedPoint = PointPicker->GetPointId();
            }

            Widget->onPicked(pickedPoint, pickedCell);

            if (pickedCell >= 0)
            {
                Widget->beginNodeDragFromCell(pickedCell, x, y);
                this->AbortFlagOn();
            }
            return;
        }

        // 鼠标移动：拖拽更新
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

        // 左键抬起：结束拖拽
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
    beforeUnload();
    delete ui;
}

void GridEditingWidget::clearData()
{
    m_model.clear();
    m_fileName.clear();
    m_dirty = false;

    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    m_highlightedMatypes.clear();
    m_highlightedMatype = -1;
    m_matypeLookupTable = nullptr;

    m_nodeIdToVtkId.clear();

    m_minNodeId = m_maxNodeId = 0;
    m_minElemId = m_maxElemId = 0;

    m_points = nullptr;
    m_grid = nullptr;

    m_colorMode = ColorMode::None;

    m_isDragging = false;
    m_dragVtkPointId = -1;
    m_dragNodeId = -1;
    m_dragStartX = m_dragStartY = m_dragStartZ = 0.0;
}

// 对外接口
void GridEditingWidget::setModel(const IiinGridModel& model)
{
    clearData();

    m_model = model;
    m_fileName = model.fileName();

    updateIdRangeLabels();
    buildOrUpdateGrid();

    updateFilterCombosFromModel();
    updateFilterSummaryLabel();
}

void GridEditingWidget::clearIiinGrid()
{
    clearData();

    if (ui)
    {
        ui->labelNodeRangeValue->setText("--");
        ui->labelElemRangeValue->setText("--");
        ui->lineEditMoveNodeId->clear();
        ui->lineEditElemIdForLabels->clear();
        ui->lineEditDeleteElemId->clear();
        ui->labelFilterSummary->setText(QString("当前过滤：全部"));
        if (ui->comboBoxColorMode)
            ui->comboBoxColorMode->setCurrentIndex(0);
    }

    if (m_visualWidgetService)
    {
        m_visualWidgetService->render();
    }
}

void GridEditingWidget::highlightMatypes(const QSet<int>& matypes, int selectedMatype,
                                         int previewMatype, int preDeleteMatype)
{
    m_highlightedMatypes = matypes;
    m_highlightedMatype = selectedMatype;

    if (!m_meshMapper || !m_grid || !m_visualWidgetService)
    {
        return;
    }

    const bool hasSelection =
        !matypes.isEmpty() || selectedMatype >= 0 || previewMatype >= 0 || preDeleteMatype >= 0;

    if (!hasSelection)
    {
        applyColorMode();
        return;
    }

    QSet<int> allMatypes = m_model.allMatypes();
    if (selectedMatype >= 0)
        allMatypes.insert(selectedMatype);
    if (previewMatype >= 0)
        allMatypes.insert(previewMatype);
    if (preDeleteMatype >= 0)
        allMatypes.insert(preDeleteMatype);
    allMatypes.unite(matypes);

    int maxMatype = -1;
    for (int v : std::as_const(allMatypes))
    {
        maxMatype = qMax(maxMatype, v);
    }
    if (maxMatype < 0)
        return;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxMatype + 1);
    lut->SetTableRange(0.0, maxMatype);
    lut->Build();

    for (int i = 0; i <= maxMatype; ++i)
    {
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);
    }
    for (int matype : std::as_const(matypes))
    {
        if (matype >= 0 && matype <= maxMatype)
        {
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
        }
    }
    if (selectedMatype >= 0 && selectedMatype <= maxMatype)
    {
        lut->SetTableValue(selectedMatype, 0.9, 0.2, 0.2, 1.0);
    }
    if (previewMatype >= 0 && previewMatype <= maxMatype)
    {
        lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
    }
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

// ----------- 过滤解析 -----------

QSet<int> GridEditingWidget::parseIntSetFromString(const QString& text) const
{
    QSet<int> s;
    const auto parts = text.split(',', Qt::SkipEmptyParts);
    for (const QString& part : parts)
    {
        QString v = part.trimmed();
        if (v.isEmpty())
            continue;
        bool ok = false;
        int n = v.toInt(&ok);
        if (ok)
            s.insert(n);
    }
    return s;
}

// ----------- VTK 构建 / 更新 -----------

int GridEditingWidget::expectedNodeCountForKetype(int ketype) const
{
    switch (ketype)
    {
    case 1:
        return 2;
    case 2:
        return 3;
    case 3:
        return 4;
    case 4:
        return 6;
    case 5:
        return 8;
    case 6:
        return 8;
    case 7:
        return 4;
    case 8:
        return 6;
    case 9:
        return 8;
    case 10:
        return 2;
    case 11:
        return 4;
    case 12:
        return 5;
    case 13:
        return 8;
    case 17:
        return 8;
    case 21:
        return 2;
    case 29:
        return 9;
    case 31:
        return 3;
    case 32:
        return 6;
    case 33:
        return 10;
    case 34:
        return 15;
    case 35:
        return 4;
    case 36:
        return 14;
    case 37:
        return 20;
    case 38:
        return 6;
    case 39:
        return 5;
    default:
        return m_model.manod();
    }
}

bool GridEditingWidget::elemMatchesFilter(const Elem& e) const
{
    if (!m_filterKetypes.isEmpty() && !m_filterKetypes.contains(e.ketype))
        return false;
    if (!m_filterMatypes.isEmpty() && !m_filterMatypes.contains(e.matype))
        return false;
    if (!m_filterLayers.isEmpty() && !m_filterLayers.contains(e.layer))
        return false;
    return true;
}

void GridEditingWidget::buildOrUpdateGrid()
{
    if (!m_visualWidgetService)
        return;

    const auto& nodes = m_model.nodes();
    const auto& elems = m_model.elems();
    if (nodes.isEmpty() || elems.isEmpty())
        return;

    auto spPoints = vtkSmartPointer<vtkPoints>::New();
    spPoints->SetNumberOfPoints(nodes.size());
    m_nodeIdToVtkId.clear();

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(nodes.size()); ++i)
    {
        const Node& n = nodes[static_cast<int>(i)];
        spPoints->SetPoint(i, n.x, n.y, n.z);
        m_nodeIdToVtkId.insert(n.id, i);
    }

    auto spGrid = vtkSmartPointer<vtkUnstructuredGrid>::New();
    spGrid->SetPoints(spPoints);

    auto elemIdArray = vtkSmartPointer<vtkIntArray>::New();
    elemIdArray->SetName("ElemId");
    elemIdArray->SetNumberOfComponents(1);

    auto ketypeArray = vtkSmartPointer<vtkIntArray>::New();
    ketypeArray->SetName("KETYPE");
    ketypeArray->SetNumberOfComponents(1);

    auto matypeArray = vtkSmartPointer<vtkIntArray>::New();
    matypeArray->SetName("MATYPE");
    matypeArray->SetNumberOfComponents(1);

    auto layerArray = vtkSmartPointer<vtkIntArray>::New();
    layerArray->SetName("LAYER");
    layerArray->SetNumberOfComponents(1);

    // NodeId 数组：用于 picking 后反查节点号
    auto nodeIdArray = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArray->SetName("NodeId");
    nodeIdArray->SetNumberOfComponents(1);
    nodeIdArray->SetNumberOfTuples(nodes.size());
    for (int i = 0; i < nodes.size(); ++i)
    {
        nodeIdArray->SetValue(i, nodes[i].id);
    }
    spGrid->GetPointData()->AddArray(nodeIdArray);

    vtkIdType validCellCount = 0;

    auto addCell = [&](int vtkCellType, const QVector<int>& localNodeLids, const Elem& e) -> bool
    {
        QVector<vtkIdType> cellPtIds;
        cellPtIds.reserve(localNodeLids.size());

        for (int lid : localNodeLids)
        {
            if (lid < 0 || lid >= e.nodeIds.size())
                return false;

            const int nid = e.nodeIds[lid];
            if (nid == 0)
                return false;
            if (!m_nodeIdToVtkId.contains(nid))
                return false;
            cellPtIds.push_back(m_nodeIdToVtkId.value(nid));
        }

        if (cellPtIds.size() != localNodeLids.size())
            return false;

        spGrid->InsertNextCell(vtkCellType, static_cast<vtkIdType>(cellPtIds.size()),
                               cellPtIds.data());

        elemIdArray->InsertNextValue(e.id);
        ketypeArray->InsertNextValue(e.ketype);
        matypeArray->InsertNextValue(e.matype);
        layerArray->InsertNextValue(e.layer);
        ++validCellCount;
        return true;
    };

    auto proxyPieces = [&](int ketype) -> QVector<QPair<int, QVector<int>>>
    {
        QVector<QPair<int, QVector<int>>> pieces;

        switch (ketype)
        {
        case 1:
        case 10:
        case 21:
            pieces.push_back({VTK_LINE, {0, 1}});
            break;
        case 2:
            pieces.push_back({VTK_QUADRATIC_EDGE, {0, 2, 1}});
            break;
        case 3:
            pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
            break;
        case 4:
            pieces.push_back({VTK_QUADRATIC_LINEAR_QUAD, {0, 2, 3, 5, 1, 4}});
            break;
        case 5:
            pieces.push_back({VTK_QUADRATIC_QUAD, {0, 2, 4, 6, 1, 3, 5, 7}});
            break;
        case 29:
            pieces.push_back({VTK_BIQUADRATIC_QUAD, {0, 2, 4, 6, 1, 3, 5, 7, 8}});
            break;
        case 11:
            pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
            break;
        case 12:
            pieces.push_back({VTK_TRIANGLE, {0, 1, 4}});
            pieces.push_back({VTK_TRIANGLE, {1, 2, 4}});
            pieces.push_back({VTK_TRIANGLE, {2, 3, 4}});
            pieces.push_back({VTK_TRIANGLE, {3, 0, 4}});
            break;
        case 31:
            pieces.push_back({VTK_TRIANGLE, {0, 1, 2}});
            break;
        case 32:
            pieces.push_back({VTK_QUADRATIC_TRIANGLE, {0, 2, 4, 1, 3, 5}});
            break;
        case 33:
            pieces.push_back({VTK_TRIANGLE, {0, 3, 6}});
            break;
        case 34:
            pieces.push_back({VTK_TRIANGLE, {0, 4, 8}});
            break;
        case 35:
            pieces.push_back({VTK_TETRA, {0, 1, 2, 3}});
            break;
        case 6:
        case 13:
            pieces.push_back({VTK_HEXAHEDRON, {0, 1, 2, 3, 4, 5, 6, 7}});
            break;
        case 36:
            pieces.push_back({VTK_HEXAHEDRON, {0, 1, 2, 3, 9, 10, 11, 12}});
            break;
        case 37:
            pieces.push_back({VTK_QUADRATIC_HEXAHEDRON, {0, 2, 4,  6,  12, 14, 16, 18, 1,  3,
                                                         5, 7, 13, 15, 17, 19, 8,  9,  10, 11}});
            break;
        case 9:
        case 17:
            pieces.push_back({VTK_QUAD, {0, 1, 2, 3}});
            pieces.push_back({VTK_QUAD, {4, 5, 6, 7}});
            break;
        case 38:
            pieces.push_back({VTK_WEDGE, {0, 1, 2, 3, 4, 5}});
            break;
        case 39:
            pieces.push_back({VTK_PYRAMID, {0, 1, 2, 3, 4}});
            break;
        default:
            break;
        }

        return pieces;
    };

    for (const Elem& e : elems)
    {
        if (!elemMatchesFilter(e))
            continue;

        auto pieces = proxyPieces(e.ketype);
        if (pieces.isEmpty())
            continue;

        for (const auto& piece : pieces)
        {
            addCell(piece.first, piece.second, e);
        }
    }

    if (validCellCount == 0)
    {
        qWarning() << "[GridEditingWidget] no cells after filter, nothing to show.";
    }

    spGrid->GetCellData()->AddArray(elemIdArray);
    spGrid->GetCellData()->AddArray(ketypeArray);
    spGrid->GetCellData()->AddArray(matypeArray);
    spGrid->GetCellData()->AddArray(layerArray);
    spGrid->GetCellData()->SetScalars(ketypeArray);

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
        if (m_meshMapper)
        {
            m_meshMapper->SetInputData(spGrid);
        }
    }
    else
    {
        m_meshMapper = nullptr;
        m_meshActor = nullptr;
    }

    m_points = spPoints;
    m_grid = spGrid;

    // interactor + picker
    if (!m_interactor || !m_renderer)
    {
        setupInteractor();
    }

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

    applyColorMode();
    m_visualWidgetService->render();
}

const Elem* GridEditingWidget::findElemById(int elemId) const
{
    const auto& elems = m_model.elems();
    for (const Elem& e : elems)
        if (e.id == elemId)
            return &e;
    return nullptr;
}

Elem* GridEditingWidget::findElemById(int elemId)
{
    auto& elems = m_model.elems();
    for (Elem& e : elems)
        if (e.id == elemId)
            return &e;
    return nullptr;
}

int GridEditingWidget::nodeIndexById(int nodeId) const
{
    return m_model.nodeIndexById(nodeId);
}

// ----------- 过滤按钮 -----------

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
    buildOrUpdateGrid();
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
    buildOrUpdateGrid();
}

// ----------- 节点移动 & 单元删除 -----------

void GridEditingWidget::moveNode(int nodeId, double* newX, double* newY, double* newZ)
{
    const double* px = newX;
    const double* py = newY;
    const double* pz = newZ;

    if (!m_model.moveNode(nodeId, px, py, pz))
    {
        qWarning() << "[GridEditingWidget] moveNode: node not found:" << nodeId;
        return;
    }

    if (m_points && m_nodeIdToVtkId.contains(nodeId))
    {
        vtkIdType pid = m_nodeIdToVtkId.value(nodeId);
        int idx = m_model.nodeIndexById(nodeId);
        if (idx >= 0 && idx < m_model.nodes().size())
        {
            const Node& n = m_model.nodes()[idx];
            m_points->SetPoint(pid, n.x, n.y, n.z);
            m_points->Modified();
        }
    }
}

void GridEditingWidget::deleteElemById(int elemId)
{
    if (!m_model.deleteElemById(elemId))
    {
        qWarning() << "[GridEditingWidget] deleteElemById: elem not found:" << elemId;
    }
    else
    {
        qDebug() << "[GridEditingWidget] delete elem" << elemId;
    }
}

void GridEditingWidget::on_pushButtonMoveNode_clicked()
{
    const auto& nodes = m_model.nodes();
    if (nodes.isEmpty())
        return;

    bool ok = false;
    int nodeId = ui->lineEditMoveNodeId->text().trimmed().toInt(&ok);
    if (!ok)
        return;

    QString sx = ui->lineEditNewX->text().trimmed();
    QString sy = ui->lineEditNewY->text().trimmed();
    QString sz = ui->lineEditNewZ->text().trimmed();

    double *px = nullptr, *py = nullptr, *pz = nullptr;
    double vx = 0, vy = 0, vz = 0;

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

    moveNode(nodeId, px, py, pz);

    m_dirty = true;
    buildOrUpdateGrid();
}

void GridEditingWidget::on_pushButtonDeleteElem_clicked()
{
    const auto& elems = m_model.elems();
    if (elems.isEmpty())
        return;

    bool ok = false;
    int elemId = ui->lineEditDeleteElemId->text().trimmed().toInt(&ok);
    if (!ok)
        return;

    deleteElemById(elemId);

    updateIdRangeLabels();
    m_dirty = true;

    buildOrUpdateGrid();
}

void GridEditingWidget::updateIdRangeLabels()
{
    const auto& nodes = m_model.nodes();
    const auto& elems = m_model.elems();

    if (nodes.isEmpty())
    {
        ui->labelNodeRangeValue->setText("--");
    }
    else
    {
        int minN = std::numeric_limits<int>::max();
        int maxN = std::numeric_limits<int>::min();
        for (const Node& n : nodes)
        {
            minN = std::min(minN, n.id);
            maxN = std::max(maxN, n.id);
        }
        m_minNodeId = minN;
        m_maxNodeId = maxN;
        ui->labelNodeRangeValue->setText(QString("%1 ~ %2").arg(minN).arg(maxN));
    }

    if (elems.isEmpty())
    {
        ui->labelElemRangeValue->setText("--");
    }
    else
    {
        int minE = std::numeric_limits<int>::max();
        int maxE = std::numeric_limits<int>::min();
        for (const Elem& e : elems)
        {
            minE = std::min(minE, e.id);
            maxE = std::max(maxE, e.id);
        }
        m_minElemId = minE;
        m_maxElemId = maxE;
        ui->labelElemRangeValue->setText(QString("%1 ~ %2").arg(minE).arg(maxE));
    }
}

// 透明度
void GridEditingWidget::on_horizontalSliderOpacity_valueChanged(int value)
{
    if (!m_visualWidgetService)
        return;

    double alpha = qBound(10, value, 100) / 100.0;

    const QStringList ids = availableMeshPropIds();
    if (ids.isEmpty())
        return;

    for (const QString& id : ids)
    {
        m_visualWidgetService->setOpacity(id, alpha, false);
    }
    m_visualWidgetService->render();
}

// 内外网格显示
void GridEditingWidget::on_checkBoxShowInternalGrid_toggled(bool checked)
{
    if (!m_meshActor || !m_visualWidgetService)
        return;

    if (checked)
    {
        m_meshActor->GetProperty()->SetRepresentationToWireframe();
        m_meshActor->GetProperty()->EdgeVisibilityOn();
    }
    else
    {
        m_meshActor->GetProperty()->SetRepresentationToSurface();
        m_meshActor->GetProperty()->EdgeVisibilityOn();
    }
    m_visualWidgetService->render();
}

void GridEditingWidget::on_checkBoxEnablePicking_toggled(bool checked)
{
    m_enableMousePicking = checked;
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

    applyColorMode();
}

// 保存
void GridEditingWidget::on_pushButtonSave_clicked()
{
    if (m_model.nodes().isEmpty() || m_model.elems().isEmpty())
    {
        qWarning() << "[GridEditingWidget] nothing to save.";
        return;
    }
    if (m_fileName.isEmpty())
    {
        qWarning() << "[GridEditingWidget] no fileName to save.";
        return;
    }

    QString err;
    if (!m_model.saveToFile(m_fileName, &err))
    {
        qWarning() << "[GridEditingWidget] save failed:" << err;
        return;
    }

    m_dirty = false;
    qDebug() << "[GridEditingWidget] saved changes to" << m_fileName;
}

void GridEditingWidget::setupInteractor()
{
    if (!m_visualWidgetService)
        return;
    if (m_interactor)
        return; // 已设置过

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

void GridEditingWidget::updateFilterCombosFromModel()
{
    if (!ui)
        return;

    auto fillCombo = [](QComboBox* combo, const QSet<int>& values)
    {
        if (!combo)
            return;
        combo->clear();
        combo->addItem(QString("全部"), QVariant());

        if (values.isEmpty())
            return;

        QList<int> list = values.values();
        std::sort(list.begin(), list.end());

        for (int v : list)
        {
            combo->addItem(QString::number(v), v);
        }
        combo->setCurrentIndex(0);
    };

    fillCombo(ui->comboBoxKetype, m_model.allKetypes());
    if (ui->comboBoxMatype)
    {
        ui->comboBoxMatype->clear();
        ui->comboBoxMatype->addItem(QString("全部"), QVariant());
        QList<int> list = m_model.allMatypes().values();
        std::sort(list.begin(), list.end());
        const MshPickMap pick = MeshGridIO::instance().getMeshMap();
        for (int v : list)
        {
            const QString name = pick.physicalName(v).trimmed();
            ui->comboBoxMatype->addItem(name.isEmpty() ? QString::number(v) : name, v);
        }
        ui->comboBoxMatype->setCurrentIndex(0);
    }
    fillCombo(ui->comboBoxLayer, m_model.allLayers());
}

void GridEditingWidget::updateFilterSummaryLabel()
{
    if (!ui)
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

void GridEditingWidget::applyColorMode()
{
    if (!m_meshMapper || !m_visualWidgetService)
        return;

    QString arrayName;
    QSet<int> values;

    switch (m_colorMode)
    {
    case ColorMode::Ketype:
        arrayName = QString("KETYPE");
        values = m_model.allKetypes();
        break;
    case ColorMode::Matype:
        arrayName = QString("MATYPE");
        values = m_model.allMatypes();
        break;
    case ColorMode::Layer:
        arrayName = QString("LAYER");
        values = m_model.allLayers();
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

    const QByteArray arrayNameBytes = arrayName.toUtf8();
    m_meshMapper->SetScalarModeToUseCellFieldData();
    m_meshMapper->SelectColorArray(arrayNameBytes.constData());
    m_meshMapper->SetLookupTable(lut);
    m_meshMapper->UseLookupTableScalarRangeOn();
    m_meshMapper->ScalarVisibilityOn();
    m_visualWidgetService->render();
}

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildLookupTable(const QSet<int>& values) const
{
    int maxValue = -1;
    for (int v : values)
    {
        maxValue = qMax(maxValue, v);
    }
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

// ------- 鼠标 picking 反馈 -------

void GridEditingWidget::onPicked(long long pickedPointId, long long pickedCellId)
{
    if (!m_grid || !ui)
        return;

    auto pointData = m_grid->GetPointData();
    auto cellData = m_grid->GetCellData();

    if (pickedPointId >= 0)
    {
        vtkIdType numPts = m_grid->GetNumberOfPoints();
        if (pickedPointId < numPts && pointData)
        {
            auto* nodeIdArr = vtkIntArray::SafeDownCast(pointData->GetArray("NodeId"));
            if (nodeIdArr && pickedPointId < nodeIdArr->GetNumberOfTuples())
            {
                int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pickedPointId));
                ui->lineEditMoveNodeId->setText(QString::number(nodeId));
            }
        }
    }

    if (pickedCellId >= 0)
    {
        vtkIdType numCells = m_grid->GetNumberOfCells();
        if (pickedCellId < numCells && cellData)
        {
            auto* elemIdArr = vtkIntArray::SafeDownCast(cellData->GetArray("ElemId"));
            if (elemIdArr && pickedCellId < elemIdArr->GetNumberOfTuples())
            {
                int elemId = elemIdArr->GetValue(static_cast<vtkIdType>(pickedCellId));
                ui->lineEditElemIdForLabels->setText(QString::number(elemId));
                ui->lineEditDeleteElemId->setText(QString::number(elemId));
            }
        }
    }
}

// ------- 拖动节点 -------

void GridEditingWidget::beginNodeDragByPointId(long long pointId)
{
    if (!m_grid || !m_points || pointId < 0)
        return;

    vtkIdType nPts = m_grid->GetNumberOfPoints();
    if (pointId >= nPts)
        return;

    auto* nodeIdArr = vtkIntArray::SafeDownCast(m_grid->GetPointData()->GetArray("NodeId"));
    if (!nodeIdArr)
        return;
    if (pointId >= nodeIdArr->GetNumberOfTuples())
        return;

    int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pointId));
    int idx = nodeIndexById(nodeId);
    if (idx < 0)
        return;

    const auto& nodes = m_model.nodes();
    const Node& n = nodes[idx];

    m_isDragging = true;
    m_dragVtkPointId = pointId;
    m_dragNodeId = nodeId;

    ui->lineEditMoveNodeId->setText(QString::number(nodeId));

    m_dragStartX = n.x;
    m_dragStartY = n.y;
    m_dragStartZ = n.z;

    ui->lineEditNodeX0->setText(QString::number(m_dragStartX));
    ui->lineEditNodeY0->setText(QString::number(m_dragStartY));
    ui->lineEditNodeZ0->setText(QString::number(m_dragStartZ));

    ui->lineEditNodeXCur->setText(QString::number(n.x));
    ui->lineEditNodeYCur->setText(QString::number(n.y));
    ui->lineEditNodeZCur->setText(QString::number(n.z));
}

void GridEditingWidget::dragNodeToWorldPosition(const double worldPos[3])
{
    if (!m_isDragging || !m_points)
        return;

    int idx = nodeIndexById(m_dragNodeId);
    if (idx < 0)
        return;

    auto& nodes = m_model.nodes();
    if (idx >= nodes.size())
        return;

    Node& n = nodes[idx];
    n.x = worldPos[0];
    n.y = worldPos[1];
    if (m_model.madim() == 3)
        n.z = worldPos[2];
    else
        n.z = 0.0;

    vtkIdType pid = static_cast<vtkIdType>(m_dragVtkPointId);
    if (pid >= 0 && pid < m_points->GetNumberOfPoints())
    {
        m_points->SetPoint(pid, n.x, n.y, n.z);
        m_points->Modified();
    }

    if (m_visualWidgetService)
    {
        m_visualWidgetService->render();
    }

    ui->lineEditNodeXCur->setText(QString::number(n.x));
    ui->lineEditNodeYCur->setText(QString::number(n.y));
    ui->lineEditNodeZCur->setText(QString::number(n.z));
}

void GridEditingWidget::beginNodeDragFromCell(long long cellId, int displayX, int displayY)
{
    if (!m_grid || !m_points || !m_renderer || cellId < 0)
        return;

    vtkIdType cid = static_cast<vtkIdType>(cellId);
    vtkCell* cell = m_grid->GetCell(cid);
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

void GridEditingWidget::endNodeDrag()
{
    if (!m_isDragging)
        return;

    m_isDragging = false;
    m_dragVtkPointId = -1;
    m_dragNodeId = -1;

    m_dirty = true;
}

void GridEditingWidget::beforeLoad()
{
    auto* meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshService)
        return;

    IiinGridModel model;
    if (!meshService->currentModel(model))
        return;

    setModel(model);
    setupInteractor();
}

void GridEditingWidget::beforeUnload()
{
    // 彻底解绑 interactor observer，避免重复注册
    if (m_interactor && m_mouseCallback)
    {
        m_interactor->RemoveObserver(m_mouseCallback);
    }
    m_mouseCallback = nullptr;

    m_cellPicker = nullptr;
    m_pointPicker = nullptr;

    m_interactor = nullptr;
    m_renderer = nullptr;

    m_enableMousePicking = false;
    m_isDragging = false;
}
