#include "GridEditingWidget.h"
#include "ui_GridEditingWidget.h"

#include <QFileDialog>
#include <QStringList>
#include <QTextStream>
#include <QDebug>
#include <QComboBox>
#include <QColor>
#include <QRect>

#include <services/VisualWidgetService.h>
#include <services/MeshModuleControllerService.h>
#include <ServiceRegistry.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

// VTK
#include <vtkSmartPointer.h>
#include <vtkPoints.h>
#include <vtkUnstructuredGrid.h>
#include <vtkIntArray.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkDataSetMapper.h>
#include <vtkActor.h>
#include <vtkActor2D.h>
#include <vtkPolyData.h>
#include <vtkLabeledDataMapper.h>
#include <vtkCellType.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkCellPicker.h>
#include <vtkPointPicker.h>
#include <vtkCommand.h>
#include <vtkDoubleArray.h>
#include <vtkLookupTable.h>
#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkRendererCollection.h>
#include <math.h>
#include <cctype>
#include <algorithm>

namespace {
// registry ids
static const QString kIiinGroup         = QString("IIIN");
static const QString kImportedMeshSurfacePropId = QString("mesh.imported.surface");
static const QString kImportedMeshLinePropId = QString("mesh.imported.line");
static const QString kIiinNodeLblPropId = QString("iiin.node_labels");
static const QString kIiinElemLblPropId = QString("iiin.elem_labels");
} // namespace

// ------------------- 鼠标回调 -------------------

class GridEditingMouseCallback : public vtkCommand
{
public:
    static GridEditingMouseCallback* New() { return new GridEditingMouseCallback; }

    GridEditingWidget *Widget      = nullptr;
    vtkCellPicker     *CellPicker  = nullptr;
    vtkPointPicker    *PointPicker = nullptr;

    void Execute(vtkObject *caller, unsigned long eventId, void *callData) override
    {
        Q_UNUSED(callData);

        auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
        if (!iren || !Widget) return;

        if (!Widget->isMousePickingEnabled() && !Widget->isLabelBoxSelectionActive()) return;

        auto rw = iren->GetRenderWindow();
        if (!rw) return;

        auto rc = rw->GetRenderers();
        if (!rc || rc->GetNumberOfItems() == 0) return;

        vtkRenderer *renderer = rc->GetFirstRenderer();
        if (!renderer) return;

        int pos[2];
        iren->GetEventPosition(pos);
        int x = pos[0];
        int y = pos[1];

        // ===== 左键按下：选中节点 & 准备拖拽 =====
        if (eventId == vtkCommand::LeftButtonPressEvent)
        {
            if (Widget->isLabelBoxSelectionActive()) {
                Widget->startLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            long long pickedPoint = -1;
            long long pickedCell  = -1;

            // 1) cell picker
            if (CellPicker) {
                if (CellPicker->Pick(x, y, 0, renderer)) {
                    pickedCell = CellPicker->GetCellId();
                }
            }

            // 2) point picker（仅用于显示编号）
            if (PointPicker) {
                if (PointPicker->Pick(x, y, 0, renderer)) {
                    pickedPoint = PointPicker->GetPointId();
                }
            }

            Widget->onPicked(pickedPoint, pickedCell);

            // 3) 拖动：以 cell 为准，从 cell 内挑最近且最前的点
            if (pickedCell >= 0) {
                Widget->beginNodeDragFromCell(pickedCell, x, y);
                this->AbortFlagOn();
            }
            return;
        }

        // ===== 鼠标移动：拖拽更新 =====
        if (eventId == vtkCommand::MouseMoveEvent)
        {
            if (Widget->isLabelBoxSelectionActive() && Widget->isBoxSelecting()) {
                Widget->updateLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            if (!Widget->isDraggingNode())
                return;

            if (!CellPicker)
                return;

            if (CellPicker->Pick(x, y, 0, renderer)) {
                double wp[3];
                CellPicker->GetPickPosition(wp);
                Widget->dragNodeToWorldPosition(wp);
            }

            this->AbortFlagOn();
            return;
        }

        // ===== 左键抬起：结束拖拽 =====
        if (eventId == vtkCommand::LeftButtonReleaseEvent)
        {
            if (Widget->isLabelBoxSelectionActive() && Widget->isBoxSelecting()) {
                Widget->finishLabelBoxSelection(x, y);
                this->AbortFlagOn();
                return;
            }

            if (Widget->isDraggingNode()) {
                Widget->endNodeDrag();
                this->AbortFlagOn();
            }
            return;
        }
    }
};

// ------------------- GridEditingWidget 实现 -------------------

GridEditingWidget::GridEditingWidget(QWidget *parent)
    : BasePage("GridEditingWidget")
    , ui(new Ui::GridEditingWidget)
{
    ui->setupUi(this);
    updateSurfaceLabelSummary(QString("外表面编号：未生成"));

    m_visualWidgetService = ServiceRegistry::instance().getService<VisualWidgetService>();

    if (ui->comboBoxColorMode) {
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
    if (m_interactor && m_mouseCallback) {
        m_interactor->RemoveObserver(m_mouseCallback);
    }
    m_mouseCallback = nullptr;
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
    m_grid   = nullptr;

    m_labelSelectionMode = LabelSelectionMode::None;
    m_isBoxSelecting = false;

    m_colorMode = ColorMode::None;

    updateSurfaceLabelSummary(QString("外表面编号：未生成"));
}

// 对外接口
void GridEditingWidget::setModel(const IiinGridModel &model)
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
    if (m_visualWidgetService) {
        m_visualWidgetService->removeProp(kIiinNodeLblPropId, false);
        m_visualWidgetService->removeProp(kIiinElemLblPropId, false);
        m_visualWidgetService->render();
    }

    clearData();

    if (ui) {
        ui->labelNodeRangeValue->setText("--");
        ui->labelElemRangeValue->setText("--");
        ui->lineEditMoveNodeId->clear();
        ui->lineEditElemIdForLabels->clear();
        ui->lineEditDeleteElemId->clear();
        ui->labelFilterSummary->setText(QString("当前过滤：全部"));
        if (ui->comboBoxColorMode) ui->comboBoxColorMode->setCurrentIndex(0);
    }
}

void GridEditingWidget::highlightMatypes(const QSet<int> &matypes,
                                         int selectedMatype,
                                         int previewMatype,
                                         int preDeleteMatype)
{
    m_highlightedMatypes = matypes;
    m_highlightedMatype = selectedMatype;

    if (!m_meshMapper || !m_grid || !m_visualWidgetService) {
        return;
    }

    const bool hasSelection = !matypes.isEmpty()
        || selectedMatype >= 0
        || previewMatype >= 0
        || preDeleteMatype >= 0;

    if (!hasSelection) {
        applyColorMode();
        return;
    }

    QSet<int> allMatypes = m_model.allMatypes();
    if (selectedMatype >= 0) {
        allMatypes.insert(selectedMatype);
    }
    if (previewMatype >= 0) {
        allMatypes.insert(previewMatype);
    }
    if (preDeleteMatype >= 0) {
        allMatypes.insert(preDeleteMatype);
    }
    allMatypes.unite(matypes);

    int maxMatype = -1;
    for (int v : std::as_const(allMatypes)) {
        maxMatype = qMax(maxMatype, v);
    }
    if (maxMatype < 0) {
        return;
    }

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxMatype + 1);
    lut->SetTableRange(0.0, maxMatype);
    lut->Build();

    for (int i = 0; i <= maxMatype; ++i) {
        lut->SetTableValue(i, 0.75, 0.75, 0.75, 1.0);
    }
    for (int matype : std::as_const(matypes)) {
        if (matype >= 0 && matype <= maxMatype) {
            lut->SetTableValue(matype, 0.25, 0.75, 0.25, 1.0);
        }
    }
    if (selectedMatype >= 0 && selectedMatype <= maxMatype) {
        lut->SetTableValue(selectedMatype, 0.9, 0.2, 0.2, 1.0);
    }
    if (previewMatype >= 0 && previewMatype <= maxMatype) {
        lut->SetTableValue(previewMatype, 0.2, 0.4, 0.9, 1.0);
    }
    if (preDeleteMatype >= 0 && preDeleteMatype <= maxMatype) {
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

QSet<int> GridEditingWidget::parseIntSetFromString(const QString &text) const
{
    QSet<int> s;
    QString t = text;

#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
    const auto parts = t.split(',', QString::SkipEmptyParts);
#else
    const auto parts = t.split(',', QString::SkipEmptyParts);
#endif

    for (const QString &part : parts) {
        QString v = part.trimmed();
        if (v.isEmpty()) continue;
        bool ok = false;
        int n = v.toInt(&ok);
        if (ok) s.insert(n);
    }
    return s;
}

// ----------- VTK 构建 / 更新 -----------

int GridEditingWidget::expectedNodeCountForKetype(int ketype) const
{
    switch (ketype) {
    // 1D
    case 1:
    case 2:
    case 10:
    case 21:
        return 2;

    // 2D 四边形族
    case 3:
    case 4:
    case 5:
    case 7:
    case 8:
    case 11:
    case 12:
    case 29:
        return 4;

    // 2D 三角形族
    case 31:
    case 32:
    case 33:
    case 34:
        return 3;

    // 3D 六面体族
    case 6:
    case 9:
    case 13:
    case 17:
    case 36:
    case 37:
        return 8;

    // 3D 四面体
    case 35:
        return 4;

    // 3D 楔形 / 棱锥
    case 38:
        return 6;
    case 39:
        return 5;

    default:
        return m_model.manod();
    }
}

bool GridEditingWidget::elemMatchesFilter(const Elem &e) const
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

    const auto &nodes = m_model.nodes();
    const auto &elems = m_model.elems();
    if (nodes.isEmpty() || elems.isEmpty())
        return;

    auto spPoints = vtkSmartPointer<vtkPoints>::New();
    spPoints->SetNumberOfPoints(nodes.size());
    m_nodeIdToVtkId.clear();

    for (vtkIdType i = 0; i < static_cast<vtkIdType>(nodes.size()); ++i) {
        const Node &n = nodes[static_cast<int>(i)];
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
    for (int i = 0; i < nodes.size(); ++i) {
        nodeIdArray->SetValue(i, nodes[i].id);
    }
    spGrid->GetPointData()->AddArray(nodeIdArray);

    // 2D quad 顺序矫正
    auto reorderQuad2D = [&](QVector<vtkIdType> &ids) {
        if (ids.size() != 4) return;
        struct QuadPt { vtkIdType pid; double ang; };
        QuadPt qp[4];
        double cx = 0.0, cy = 0.0;
        double p[3];

        for (int i = 0; i < 4; ++i) {
            spPoints->GetPoint(ids[i], p);
            cx += p[0];
            cy += p[1];
        }
        cx /= 4.0;
        cy /= 4.0;

        for (int i = 0; i < 4; ++i) {
            spPoints->GetPoint(ids[i], p);
            double dx = p[0] - cx;
            double dy = p[1] - cy;
            qp[i].pid = ids[i];
            qp[i].ang = std::atan2(dy, dx);
        }

        std::sort(qp, qp + 4,
                  [](const QuadPt &a, const QuadPt &b) {
                      return a.ang < b.ang;
                  });

        for (int i = 0; i < 4; ++i)
            ids[i] = qp[i].pid;
    };

    vtkIdType validCellCount = 0;

    for (const Elem &e : elems) {
        if (!elemMatchesFilter(e))
            continue;

        int ketype      = e.ketype;
        int vtkCellType = -1;
        int nNodes      = expectedNodeCountForKetype(ketype);

        QVector<vtkIdType> cellPtIds;
        cellPtIds.reserve(nNodes);

        auto addIds = [&](int count) -> bool {
            for (int k = 0; k < count; ++k) {
                if (k >= e.nodeIds.size())
                    return false;
                int nid = e.nodeIds[k];
                if (nid == 0) return false;
                if (!m_nodeIdToVtkId.contains(nid))
                    return false;
                cellPtIds.push_back(m_nodeIdToVtkId.value(nid));
            }
            return true;
        };

        switch (ketype) {
        // 1D
        case 1:
        case 2:
        case 10:
        case 21:
            vtkCellType = VTK_LINE;
            if (!addIds(2)) continue;
            break;

        // 2D quad
        case 3:
        case 4:
        case 5:
        case 7:
        case 8:
        case 11:
        case 12:
        case 29:
            vtkCellType = VTK_QUAD;
            if (!addIds(4)) continue;
            if (m_model.madim() == 2)
                reorderQuad2D(cellPtIds);
            break;

        // 2D tri
        case 31:
        case 32:
        case 33:
        case 34:
            vtkCellType = VTK_TRIANGLE;
            if (!addIds(3)) continue;
            break;

        // 3D hex
        case 6:
        case 9:
        case 13:
        case 17:
        case 36:
        case 37:
            vtkCellType = VTK_HEXAHEDRON;
            if (!addIds(8)) continue;
            break;

        // 3D tet
        case 35:
            vtkCellType = VTK_TETRA;
            if (!addIds(4)) continue;
            break;

        case 38:
            vtkCellType = VTK_WEDGE;
            if (!addIds(6)) continue;
            break;

        case 39:
            vtkCellType = VTK_PYRAMID;
            if (!addIds(5)) continue;
            break;

        default:
            continue;
        }

        spGrid->InsertNextCell(vtkCellType,
                               cellPtIds.size(),
                               cellPtIds.data());

        elemIdArray->InsertNextValue(e.id);
        ketypeArray->InsertNextValue(e.ketype);
        matypeArray->InsertNextValue(e.matype);
        layerArray->InsertNextValue(e.layer);

        ++validCellCount;
    }

    if (validCellCount == 0) {
        qWarning() << "[GridEditingWidget] no cells after filter, nothing to show.";
    }

    spGrid->GetCellData()->AddArray(elemIdArray);
    spGrid->GetCellData()->AddArray(ketypeArray);
    spGrid->GetCellData()->AddArray(matypeArray);
    spGrid->GetCellData()->AddArray(layerArray);
    spGrid->GetCellData()->SetScalars(ketypeArray);

    vtkActor *existingActor = nullptr;
    if (m_visualWidgetService) {
        existingActor = vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshSurfacePropId));
        if (!existingActor) {
            existingActor = vtkActor::SafeDownCast(m_visualWidgetService->getProp(kImportedMeshLinePropId));
        }
    }

    if (existingActor) {
        m_meshActor = existingActor;
        m_meshMapper = vtkDataSetMapper::SafeDownCast(existingActor->GetMapper());
        if (m_meshMapper) {
            m_meshMapper->SetInputData(spGrid);
        }
    } else {
        m_meshMapper = nullptr;
        m_meshActor = nullptr;
    }

    m_points = spPoints;
    m_grid   = spGrid;

    ensureLabelActorsCreated();

    // interactor + picker
    if (!m_interactor || !m_renderer) {
        setupInteractor();
    }

    if (m_cellPicker && m_meshActor) {
        m_cellPicker->InitializePickList();
        m_cellPicker->AddPickList(m_meshActor);
        m_cellPicker->PickFromListOn();
    }
    if (m_pointPicker && m_meshActor) {
        m_pointPicker->InitializePickList();
        m_pointPicker->AddPickList(m_meshActor);
        m_pointPicker->PickFromListOn();
    }

    applyColorMode();

    m_visualWidgetService->render();
}

void GridEditingWidget::ensureLabelActorsCreated()
{
    if (!m_visualWidgetService)
        return;

    if (!m_nodeLabelMapper) {
        auto mapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
        auto actor  = vtkSmartPointer<vtkActor2D>::New();

        mapper->SetLabelModeToLabelFieldData();
        mapper->SetLabelFormat("N%d");
        actor->SetMapper(mapper);

        m_nodeLabelMapper = mapper;
        m_nodeLabelActor  = actor;

        if (m_visualWidgetService->getProp(kIiinNodeLblPropId) == nullptr) {
            m_visualWidgetService->addProp(kIiinNodeLblPropId, m_nodeLabelActor, kIiinGroup,
                                           QString("节点号"),
                                           /*resetCam*/false, /*renderNow*/false);
        }
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, false);
    }

    if (!m_elemLabelMapper) {
        auto mapper = vtkSmartPointer<vtkLabeledDataMapper>::New();
        auto actor  = vtkSmartPointer<vtkActor2D>::New();

        mapper->SetLabelModeToLabelFieldData();
        mapper->SetLabelFormat("E%d");
        actor->SetMapper(mapper);

        m_elemLabelMapper = mapper;
        m_elemLabelActor  = actor;

        if (m_visualWidgetService->getProp(kIiinElemLblPropId) == nullptr) {
            m_visualWidgetService->addProp(kIiinElemLblPropId, m_elemLabelActor, kIiinGroup,
                                           QString("单元号"),
                                           /*resetCam*/false, /*renderNow*/false);
        }
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, false);
    }

    applyLabelTextStyles();
}

void GridEditingWidget::applyLabelTextStyles()
{
    if (m_nodeLabelMapper && !m_nodeLabelTextProperty) {
        m_nodeLabelTextProperty = vtkSmartPointer<vtkTextProperty>::New();
        m_nodeLabelTextProperty->SetColor(0.09, 0.36, 0.89);
        m_nodeLabelTextProperty->SetBold(true);
        m_nodeLabelTextProperty->SetBackgroundColor(0.95, 0.97, 1.0);
        m_nodeLabelTextProperty->SetBackgroundOpacity(0.35);
    }
    if (m_elemLabelMapper && !m_elemLabelTextProperty) {
        m_elemLabelTextProperty = vtkSmartPointer<vtkTextProperty>::New();
        m_elemLabelTextProperty->SetColor(0.93, 0.42, 0.08);
        m_elemLabelTextProperty->SetBold(true);
        m_elemLabelTextProperty->SetBackgroundColor(1.0, 0.97, 0.93);
        m_elemLabelTextProperty->SetBackgroundOpacity(0.35);
    }

    if (m_nodeLabelMapper && m_nodeLabelTextProperty) {
        m_nodeLabelMapper->SetLabelTextProperty(m_nodeLabelTextProperty);
    }
    if (m_elemLabelMapper && m_elemLabelTextProperty) {
        m_elemLabelMapper->SetLabelTextProperty(m_elemLabelTextProperty);
    }
}

const Elem* GridEditingWidget::findElemById(int elemId) const
{
    const auto &elems = m_model.elems();
    for (const Elem &e : elems)
        if (e.id == elemId) return &e;
    return nullptr;
}

Elem* GridEditingWidget::findElemById(int elemId)
{
    auto &elems = m_model.elems();
    for (Elem &e : elems)
        if (e.id == elemId) return &e;
    return nullptr;
}

int GridEditingWidget::nodeIndexById(int nodeId) const
{
    return m_model.nodeIndexById(nodeId);
}

void GridEditingWidget::showNodeLabelsForElem(int elemId)
{
    if (!m_visualWidgetService || !m_nodeLabelMapper || !m_nodeLabelActor)
        return;

    applyLabelTextStyles();

    const Elem *e = findElemById(elemId);
    if (!e) {
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, true);
        return;
    }

    const auto &nodes = m_model.nodes();

    auto poly   = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto idArr  = vtkSmartPointer<vtkIntArray>::New();
    idArr->SetName("NodeId");
    idArr->SetNumberOfComponents(1);

    for (int nid : e->nodeIds) {
        if (nid == 0) continue;
        int idx = nodeIndexById(nid);
        if (idx < 0) continue;
        const Node &n = nodes[idx];
        points->InsertNextPoint(n.x, n.y, n.z);
        idArr->InsertNextValue(nid);
    }

    poly->SetPoints(points);
    poly->GetPointData()->AddArray(idArr);

    m_nodeLabelMapper->SetInputData(poly);
    m_nodeLabelMapper->SetFieldDataName("NodeId");

    m_visualWidgetService->setVisible(kIiinNodeLblPropId, points->GetNumberOfPoints() > 0, true);
}

void GridEditingWidget::showElemLabelForElem(int elemId)
{
    if (!m_visualWidgetService || !m_elemLabelMapper || !m_elemLabelActor)
        return;

    applyLabelTextStyles();

    const Elem *e = findElemById(elemId);
    if (!e) {
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, true);
        return;
    }

    const auto &nodes = m_model.nodes();

    double cx = 0, cy = 0, cz = 0;
    int cnt = 0;
    for (int nid : e->nodeIds) {
        if (nid == 0) continue;
        int idx = nodeIndexById(nid);
        if (idx < 0) continue;
        const Node &n = nodes[idx];
        cx += n.x; cy += n.y; cz += n.z;
        ++cnt;
    }
    if (cnt == 0) {
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, true);
        return;
    }
    cx /= cnt; cy /= cnt; cz /= cnt;

    auto poly   = vtkSmartPointer<vtkPolyData>::New();
    auto points = vtkSmartPointer<vtkPoints>::New();
    auto idArr  = vtkSmartPointer<vtkIntArray>::New();
    idArr->SetName("ElemId");
    idArr->SetNumberOfComponents(1);

    points->InsertNextPoint(cx, cy, cz);
    idArr->InsertNextValue(elemId);

    poly->SetPoints(points);
    poly->GetPointData()->AddArray(idArr);

    m_elemLabelMapper->SetInputData(poly);
    m_elemLabelMapper->SetFieldDataName("ElemId");

    m_visualWidgetService->setVisible(kIiinElemLblPropId, true, true);
}

void GridEditingWidget::showSurfaceLabels()
{
    if (!m_visualWidgetService) {
        updateSurfaceLabelSummary(QString("外表面编号：渲染服务不可用"));
        return;
    }

    ensureLabelActorsCreated();
    applyLabelTextStyles();

    vtkRenderer* renderer = m_visualWidgetService->renderer();
    if (!renderer) {
        updateSurfaceLabelSummary(QString("外表面编号：缺少渲染器"));
        return;
    }

    const double binSize = 80.0;
    updateSurfaceLabelSummary(QString("外表面编号：统计中..."));

    auto projectToDisplay = [&](const double world[3], double &sx, double &sy, double &sz) -> bool {
        renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
        renderer->WorldToDisplay();
        double *dp = renderer->GetDisplayPoint();
        sx = dp[0];
        sy = dp[1];
        sz = dp[2];
        return std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sz);
    };

    const auto &nodes = m_model.nodes();
    const auto &elems = m_model.elems();
    if (nodes.isEmpty() || elems.isEmpty()) {
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, false);
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, true);
        return;
    }

    const double xmin = m_model.xmin();
    const double xmax = m_model.xmax();
    const double ymin = m_model.ymin();
    const double ymax = m_model.ymax();
    const double zmin = m_model.zmin();
    const double zmax = m_model.zmax();

    const double span = std::max({xmax - xmin, ymax - ymin, zmax - zmin});
    const double eps  = std::max(1e-6, span * 1e-6);

    auto isBoundaryNode = [&](const Node &n) {
        return (std::abs(n.x - xmin) <= eps) || (std::abs(n.x - xmax) <= eps)
        || (std::abs(n.y - ymin) <= eps) || (std::abs(n.y - ymax) <= eps)
            || (std::abs(n.z - zmin) <= eps) || (std::abs(n.z - zmax) <= eps);
    };

    QSet<int> boundaryNodeIds;

    struct ElemCenter { double x; double y; double z; int id; };
    QVector<ElemCenter> surfaceElemCenters;

    struct ScreenCandidate {
        double x;
        double y;
        double z;
        double depth;
        int    id;
    };

    QMap<QPair<int, int>, ScreenCandidate> nodeBuckets;
    QMap<QPair<int, int>, ScreenCandidate> elemBuckets;

    for (const Elem &e : elems) {
        if (!elemMatchesFilter(e))
            continue;

        bool onSurface = false;
        double cx = 0, cy = 0, cz = 0;
        int cnt = 0;

        for (int nid : e.nodeIds) {
            if (nid == 0) continue;
            int idx = nodeIndexById(nid);
            if (idx < 0 || idx >= nodes.size()) continue;
            const Node &n = nodes[idx];
            cx += n.x; cy += n.y; cz += n.z;
            ++cnt;
            if (isBoundaryNode(n)) {
                boundaryNodeIds.insert(n.id);
                onSurface = true;
            }
        }

        if (cnt == 0 || !onSurface) continue;
        surfaceElemCenters.push_back({cx / cnt, cy / cnt, cz / cnt, e.id});
    }

    QList<int> boundaryNodeList = boundaryNodeIds.values();
    std::sort(boundaryNodeList.begin(), boundaryNodeList.end());
    const int totalSurfaceNodes = boundaryNodeList.size();
    const int totalSurfaceElems = surfaceElemCenters.size();

    auto selectFrontmost = [&](QMap<QPair<int, int>, ScreenCandidate> &buckets,
                               int bx, int by, const ScreenCandidate &candidate) {
        const QPair<int, int> key(bx, by);
        auto it = buckets.find(key);
        if (it == buckets.end() || candidate.depth < it->depth) {
            buckets.insert(key, candidate);
        }
    };

    for (int nid : boundaryNodeList) {
        int idx = nodeIndexById(nid);
        if (idx < 0 || idx >= nodes.size()) continue;
        const Node &n = nodes[idx];

        double sx = 0, sy = 0, sz = 0;
        double world[3] = {n.x, n.y, n.z};
        if (!projectToDisplay(world, sx, sy, sz)) continue;

        const int bx = static_cast<int>(std::floor(sx / binSize));
        const int by = static_cast<int>(std::floor(sy / binSize));
        selectFrontmost(nodeBuckets, bx, by, {n.x, n.y, n.z, sz, n.id});
    }

    for (const ElemCenter &c : surfaceElemCenters) {
        double sx = 0, sy = 0, sz = 0;
        double world[3] = {c.x, c.y, c.z};
        if (!projectToDisplay(world, sx, sy, sz)) continue;

        const int bx = static_cast<int>(std::floor(sx / binSize));
        const int by = static_cast<int>(std::floor(sy / binSize));
        selectFrontmost(elemBuckets, bx, by, {c.x, c.y, c.z, sz, c.id});
    }

    auto downsample = [&](QVector<ScreenCandidate> &candidates) {
        const int maxLabels = maxSurfaceLabelCount();
        if (candidates.size() > maxLabels) {
            const int step = std::max<int>(1, (candidates.size() + maxLabels - 1) / maxLabels);
            QVector<ScreenCandidate> trimmed;
            trimmed.reserve((candidates.size() + step - 1) / step);
            for (int i = 0; i < candidates.size(); i += step) {
                trimmed.push_back(candidates[i]);
            }
            candidates.swap(trimmed);
        }
    };

    QVector<ScreenCandidate> nodeCandidates = nodeBuckets.values().toVector();
    const int bucketedNodes = nodeCandidates.size();
    std::sort(nodeCandidates.begin(), nodeCandidates.end(), [](const ScreenCandidate &a, const ScreenCandidate &b) {
        return a.id < b.id;
    });
    downsample(nodeCandidates);

    auto nodePoly   = vtkSmartPointer<vtkPolyData>::New();
    auto nodePoints = vtkSmartPointer<vtkPoints>::New();
    auto nodeIdArr  = vtkSmartPointer<vtkIntArray>::New();
    nodeIdArr->SetName("NodeId");
    nodeIdArr->SetNumberOfComponents(1);

    for (const ScreenCandidate &c : nodeCandidates) {
        nodePoints->InsertNextPoint(c.x, c.y, c.z);
        nodeIdArr->InsertNextValue(c.id);
    }

    nodePoly->SetPoints(nodePoints);
    nodePoly->GetPointData()->AddArray(nodeIdArr);
    m_nodeLabelMapper->SetInputData(nodePoly);
    m_nodeLabelMapper->SetFieldDataName("NodeId");

    m_visualWidgetService->setVisible(kIiinNodeLblPropId, nodePoints->GetNumberOfPoints() > 0, false);

    QVector<ScreenCandidate> elemCandidates = elemBuckets.values().toVector();
    const int bucketedElems = elemCandidates.size();
    std::sort(elemCandidates.begin(), elemCandidates.end(), [](const ScreenCandidate &a, const ScreenCandidate &b) {
        return a.id < b.id;
    });
    downsample(elemCandidates);

    auto elemPoly   = vtkSmartPointer<vtkPolyData>::New();
    auto elemPoints = vtkSmartPointer<vtkPoints>::New();
    auto elemIdArr  = vtkSmartPointer<vtkIntArray>::New();
    elemIdArr->SetName("ElemId");
    elemIdArr->SetNumberOfComponents(1);

    for (const ScreenCandidate &c : elemCandidates) {
        elemPoints->InsertNextPoint(c.x, c.y, c.z);
        elemIdArr->InsertNextValue(c.id);
    }

    elemPoly->SetPoints(elemPoints);
    elemPoly->GetPointData()->AddArray(elemIdArr);
    m_elemLabelMapper->SetInputData(elemPoly);
    m_elemLabelMapper->SetFieldDataName("ElemId");

    m_visualWidgetService->setVisible(kIiinElemLblPropId, elemPoints->GetNumberOfPoints() > 0, false);

    const QString summary = QString(
                                "外表面编号：节点 %1/%2（桶后 %3），单元 %4/%5（桶后 %6）；分桶 %7px，上限 %8")
                                .arg(nodeCandidates.size())
                                .arg(totalSurfaceNodes)
                                .arg(bucketedNodes)
                                .arg(elemCandidates.size())
                                .arg(totalSurfaceElems)
                                .arg(bucketedElems)
                                .arg(binSize)
                                .arg(maxSurfaceLabelCount());
    updateSurfaceLabelSummary(summary);

    m_visualWidgetService->render();
}

// ----------- 过滤按钮 -----------

void GridEditingWidget::on_pushButtonApplyFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    if (ui->comboBoxKetype) {
        QVariant data = ui->comboBoxKetype->currentData();
        if (data.isValid()) m_filterKetypes.insert(data.toInt());
    }
    if (ui->comboBoxMatype) {
        QVariant data = ui->comboBoxMatype->currentData();
        if (data.isValid()) m_filterMatypes.insert(data.toInt());
    }
    if (ui->comboBoxLayer) {
        QVariant data = ui->comboBoxLayer->currentData();
        if (data.isValid()) m_filterLayers.insert(data.toInt());
    }

    updateFilterSummaryLabel();
    buildOrUpdateGrid();
}

void GridEditingWidget::on_pushButtonResetFilter_clicked()
{
    m_filterKetypes.clear();
    m_filterMatypes.clear();
    m_filterLayers.clear();

    if (ui->comboBoxKetype) ui->comboBoxKetype->setCurrentIndex(0);
    if (ui->comboBoxMatype) ui->comboBoxMatype->setCurrentIndex(0);
    if (ui->comboBoxLayer)  ui->comboBoxLayer->setCurrentIndex(0);

    updateFilterSummaryLabel();
    buildOrUpdateGrid();
}

// ----------- 显示节点号 / 单元号 -----------

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

// ----------- 节点移动 & 单元删除 -----------

void GridEditingWidget::moveNode(int nodeId, double *newX, double *newY, double *newZ)
{
    const double *px = newX;
    const double *py = newY;
    const double *pz = newZ;

    if (!m_model.moveNode(nodeId, px, py, pz)) {
        qWarning() << "[GridEditingWidget] moveNode: node not found:" << nodeId;
        return;
    }

    if (m_points && m_nodeIdToVtkId.contains(nodeId)) {
        vtkIdType pid = m_nodeIdToVtkId.value(nodeId);
        int idx = m_model.nodeIndexById(nodeId);
        if (idx >= 0 && idx < m_model.nodes().size()) {
            const Node &n = m_model.nodes()[idx];
            m_points->SetPoint(pid, n.x, n.y, n.z);
            m_points->Modified();
        }
    }
}

void GridEditingWidget::deleteElemById(int elemId)
{
    if (!m_model.deleteElemById(elemId)) {
        qWarning() << "[GridEditingWidget] deleteElemById: elem not found:" << elemId;
    } else {
        qDebug() << "[GridEditingWidget] delete elem" << elemId;
    }
}

void GridEditingWidget::on_pushButtonMoveNode_clicked()
{
    const auto &nodes = m_model.nodes();
    if (nodes.isEmpty()) return;

    bool ok = false;
    int nodeId = ui->lineEditMoveNodeId->text().trimmed().toInt(&ok);
    if (!ok) return;

    QString sx = ui->lineEditNewX->text().trimmed();
    QString sy = ui->lineEditNewY->text().trimmed();
    QString sz = ui->lineEditNewZ->text().trimmed();

    double *px = nullptr, *py = nullptr, *pz = nullptr;
    double vx = 0, vy = 0, vz = 0;

    if (!sx.isEmpty()) { vx = sx.toDouble(&ok); if (!ok) return; px = &vx; }
    if (!sy.isEmpty()) { vy = sy.toDouble(&ok); if (!ok) return; py = &vy; }
    if (!sz.isEmpty()) { vz = sz.toDouble(&ok); if (!ok) return; pz = &vz; }

    moveNode(nodeId, px, py, pz);

    m_dirty = true;
    buildOrUpdateGrid();
}

void GridEditingWidget::on_pushButtonDeleteElem_clicked()
{
    const auto &elems = m_model.elems();
    if (elems.isEmpty()) return;

    bool ok = false;
    int elemId = ui->lineEditDeleteElemId->text().trimmed().toInt(&ok);
    if (!ok) return;

    deleteElemById(elemId);

    updateIdRangeLabels();
    m_dirty = true;

    buildOrUpdateGrid();
}

void GridEditingWidget::updateIdRangeLabels()
{
    const auto &nodes = m_model.nodes();
    const auto &elems = m_model.elems();

    if (nodes.isEmpty()) {
        ui->labelNodeRangeValue->setText("--");
    } else {
        int minN = std::numeric_limits<int>::max();
        int maxN = std::numeric_limits<int>::min();
        for (const Node &n : nodes) {
            minN = std::min(minN, n.id);
            maxN = std::max(maxN, n.id);
        }
        m_minNodeId = minN;
        m_maxNodeId = maxN;
        ui->labelNodeRangeValue->setText(QString("%1 ~ %2").arg(minN).arg(maxN));
    }

    if (elems.isEmpty()) {
        ui->labelElemRangeValue->setText("--");
    } else {
        int minE = std::numeric_limits<int>::max();
        int maxE = std::numeric_limits<int>::min();
        for (const Elem &e : elems) {
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
    if (!m_visualWidgetService) return;

    // qBound(min, current, max)
    double alpha = qBound(10, value, 100) / 100.0;

    const QStringList ids = availableMeshPropIds();
    if (ids.isEmpty())
        return;
    for (const QString &id : ids) {
        m_visualWidgetService->setOpacity(id, alpha, false);
    }
    m_visualWidgetService->render();
}

// 内外网格显示
void GridEditingWidget::on_checkBoxShowInternalGrid_toggled(bool checked)
{
    if (!m_meshActor || !m_visualWidgetService) return;

    if (checked) {
        m_meshActor->GetProperty()->SetRepresentationToWireframe();
        m_meshActor->GetProperty()->EdgeVisibilityOn();
    } else {
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
    if (data.isValid()) {
        m_colorMode = static_cast<ColorMode>(data.toInt());
    } else {
        m_colorMode = ColorMode::None;
    }

    applyColorMode();
}

// 保存
void GridEditingWidget::on_pushButtonSave_clicked()
{
    if (m_model.nodes().isEmpty() || m_model.elems().isEmpty()) {
        qWarning() << "[GridEditingWidget] nothing to save.";
        return;
    }
    if (m_fileName.isEmpty()) {
        qWarning() << "[GridEditingWidget] no fileName to save.";
        return;
    }

    QString err;
    if (!m_model.saveToFile(m_fileName, &err)) {
        qWarning() << "[GridEditingWidget] save failed:" << err;
        return;
    }

    m_dirty = false;
    qDebug() << "[GridEditingWidget] saved changes to" << m_fileName;
}

void GridEditingWidget::setupInteractor()
{
    if (!m_visualWidgetService) return;
    if (m_interactor) return; // 已设置过

    m_interactor = m_visualWidgetService->interactor();
    m_renderer   = m_visualWidgetService->renderer();
    if (!m_interactor || !m_renderer) return;

    m_cellPicker  = vtkSmartPointer<vtkCellPicker>::New();
    m_pointPicker = vtkSmartPointer<vtkPointPicker>::New();

    auto cb = vtkSmartPointer<GridEditingMouseCallback>::New();
    cb->Widget      = this;
    cb->CellPicker  = m_cellPicker;
    cb->PointPicker = m_pointPicker;

    m_mouseCallback = cb;

    double priority = 1.0;
    m_interactor->AddObserver(vtkCommand::LeftButtonPressEvent,   cb, priority);
    m_interactor->AddObserver(vtkCommand::MouseMoveEvent,         cb, priority);
    m_interactor->AddObserver(vtkCommand::LeftButtonReleaseEvent, cb, priority);
}

void GridEditingWidget::updateFilterCombosFromModel()
{
    if (!ui) return;

    auto fillCombo = [](QComboBox *combo, const QSet<int> &values) {
        if (!combo) return;
        combo->clear();
        combo->addItem(QString("全部"), QVariant());

        if (values.isEmpty()) return;

        QList<int> list = values.values();
        std::sort(list.begin(), list.end());

        for (int v : list) {
            combo->addItem(QString::number(v), v);
        }
        combo->setCurrentIndex(0);
    };

    fillCombo(ui->comboBoxKetype, m_model.allKetypes());
    fillCombo(ui->comboBoxMatype, m_model.allMatypes());
    fillCombo(ui->comboBoxLayer,  m_model.allLayers());
}

void GridEditingWidget::updateFilterSummaryLabel()
{
    if (!ui) return;

    auto describeSet = [](const QSet<int> &values) {
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

void GridEditingWidget::updateSurfaceLabelSummary(const QString &text)
{
    if (ui && ui->labelSurfaceLabelSummary) {
        ui->labelSurfaceLabelSummary->setText(text);
    }
}

void GridEditingWidget::beginLabelBoxSelection(LabelSelectionMode mode)
{
    m_labelSelectionMode = mode;
    m_isBoxSelecting = false;

    if (m_visualWidgetService) {
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, false);
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, false);
        m_visualWidgetService->render();
    }

    QString hint;
    switch (mode) {
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

void GridEditingWidget::startLabelBoxSelection(int x, int y)
{
    if (m_labelSelectionMode == LabelSelectionMode::None)
        return;

    m_isBoxSelecting = true;
    m_boxStart[0] = x;
    m_boxStart[1] = y;
    m_boxEnd[0] = x;
    m_boxEnd[1] = y;
}

void GridEditingWidget::updateLabelBoxSelection(int x, int y)
{
    if (!m_isBoxSelecting)
        return;
    m_boxEnd[0] = x;
    m_boxEnd[1] = y;
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

    showSurfaceLabelsInRect(rect, m_labelSelectionMode);
    m_labelSelectionMode = LabelSelectionMode::None;
}

void GridEditingWidget::showSurfaceLabelsInRect(const QRect &rect, LabelSelectionMode mode)
{
    if (!m_visualWidgetService) {
        updateSurfaceLabelSummary(QString("外表面编号：渲染服务不可用"));
        return;
    }

    ensureLabelActorsCreated();
    applyLabelTextStyles();

    vtkRenderer* renderer = m_visualWidgetService->renderer();
    if (!renderer) {
        updateSurfaceLabelSummary(QString("外表面编号：缺少渲染器"));
        return;
    }

    const bool showNodes = (mode == LabelSelectionMode::SurfaceNode || mode == LabelSelectionMode::SurfaceBoth);
    const bool showElems = (mode == LabelSelectionMode::SurfaceElem || mode == LabelSelectionMode::SurfaceBoth);

    const double binSize = 60.0;
    const auto &nodes = m_model.nodes();
    const auto &elems = m_model.elems();
    if (nodes.isEmpty() || elems.isEmpty()) {
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, false);
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, true);
        return;
    }

    auto projectToDisplay = [&](const double world[3], double &sx, double &sy, double &sz) -> bool {
        renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
        renderer->WorldToDisplay();
        double *dp = renderer->GetDisplayPoint();
        sx = dp[0];
        sy = dp[1];
        sz = dp[2];
        return std::isfinite(sx) && std::isfinite(sy) && std::isfinite(sz);
    };

    const double xmin = m_model.xmin();
    const double xmax = m_model.xmax();
    const double ymin = m_model.ymin();
    const double ymax = m_model.ymax();
    const double zmin = m_model.zmin();
    const double zmax = m_model.zmax();

    const double span = std::max({xmax - xmin, ymax - ymin, zmax - zmin});
    const double eps  = std::max(1e-6, span * 1e-6);

    auto isBoundaryNode = [&](const Node &n) {
        return (std::abs(n.x - xmin) <= eps) || (std::abs(n.x - xmax) <= eps)
        || (std::abs(n.y - ymin) <= eps) || (std::abs(n.y - ymax) <= eps)
            || (std::abs(n.z - zmin) <= eps) || (std::abs(n.z - zmax) <= eps);
    };

    QSet<int> boundaryNodeIds;
    struct ElemCenter { double x; double y; double z; int id; };
    QVector<ElemCenter> surfaceElemCenters;

    for (const Elem &e : elems) {
        if (!elemMatchesFilter(e))
            continue;

        bool onSurface = false;
        double cx = 0, cy = 0, cz = 0;
        int cnt = 0;

        for (int nid : e.nodeIds) {
            if (nid == 0) continue;
            int idx = nodeIndexById(nid);
            if (idx < 0 || idx >= nodes.size()) continue;
            const Node &n = nodes[idx];
            cx += n.x; cy += n.y; cz += n.z;
            ++cnt;
            if (isBoundaryNode(n)) {
                boundaryNodeIds.insert(n.id);
                onSurface = true;
            }
        }

        if (cnt == 0 || !onSurface) continue;
        surfaceElemCenters.push_back({cx / cnt, cy / cnt, cz / cnt, e.id});
    }

    const int minX = rect.left();
    const int maxX = rect.right();
    const int minY = rect.top();
    const int maxY = rect.bottom();

    auto inRect = [&](double sx, double sy) {
        return sx >= minX && sx <= maxX && sy >= minY && sy <= maxY;
    };

    struct ScreenCandidate {
        double x;
        double y;
        double z;
        double depth;
        int    id;
    };

    QMap<QPair<int, int>, ScreenCandidate> nodeBuckets;
    QMap<QPair<int, int>, ScreenCandidate> elemBuckets;

    auto selectFrontmost = [&](QMap<QPair<int, int>, ScreenCandidate> &buckets,
                               int bx, int by, const ScreenCandidate &candidate) {
        const QPair<int, int> key(bx, by);
        auto it = buckets.find(key);
        if (it == buckets.end() || candidate.depth < it->depth) {
            buckets.insert(key, candidate);
        }
    };

    if (showNodes) {
        QList<int> boundaryNodeList = boundaryNodeIds.values();
        std::sort(boundaryNodeList.begin(), boundaryNodeList.end());
        for (int nid : boundaryNodeList) {
            int idx = nodeIndexById(nid);
            if (idx < 0 || idx >= nodes.size()) continue;
            const Node &n = nodes[idx];

            double sx = 0, sy = 0, sz = 0;
            double world[3] = {n.x, n.y, n.z};
            if (!projectToDisplay(world, sx, sy, sz)) continue;
            if (!inRect(sx, sy)) continue;

            const int bx = static_cast<int>(std::floor(sx / binSize));
            const int by = static_cast<int>(std::floor(sy / binSize));
            selectFrontmost(nodeBuckets, bx, by, {n.x, n.y, n.z, sz, n.id});
        }
    }

    if (showElems) {
        for (const ElemCenter &c : surfaceElemCenters) {
            double sx = 0, sy = 0, sz = 0;
            double world[3] = {c.x, c.y, c.z};
            if (!projectToDisplay(world, sx, sy, sz)) continue;
            if (!inRect(sx, sy)) continue;

            const int bx = static_cast<int>(std::floor(sx / binSize));
            const int by = static_cast<int>(std::floor(sy / binSize));
            selectFrontmost(elemBuckets, bx, by, {c.x, c.y, c.z, sz, c.id});
        }
    }

    auto downsample = [&](QVector<ScreenCandidate> &candidates) {
        const int maxLabels = maxSurfaceLabelCount();
        if (candidates.size() > maxLabels) {
            const int step = std::max<int>(1, (candidates.size() + maxLabels - 1) / maxLabels);
            QVector<ScreenCandidate> trimmed;
            trimmed.reserve((candidates.size() + step - 1) / step);
            for (int i = 0; i < candidates.size(); i += step) {
                trimmed.push_back(candidates[i]);
            }
            candidates.swap(trimmed);
        }
    };

    if (showNodes) {
        QVector<ScreenCandidate> nodeCandidates = nodeBuckets.values().toVector();
        std::sort(nodeCandidates.begin(), nodeCandidates.end(), [](const ScreenCandidate &a, const ScreenCandidate &b) {
            return a.id < b.id;
        });
        downsample(nodeCandidates);

        auto nodePoly   = vtkSmartPointer<vtkPolyData>::New();
        auto nodePoints = vtkSmartPointer<vtkPoints>::New();
        auto nodeIdArr  = vtkSmartPointer<vtkIntArray>::New();
        nodeIdArr->SetName("NodeId");
        nodeIdArr->SetNumberOfComponents(1);

        for (const ScreenCandidate &c : nodeCandidates) {
            nodePoints->InsertNextPoint(c.x, c.y, c.z);
            nodeIdArr->InsertNextValue(c.id);
        }

        nodePoly->SetPoints(nodePoints);
        nodePoly->GetPointData()->AddArray(nodeIdArr);
        m_nodeLabelMapper->SetInputData(nodePoly);
        m_nodeLabelMapper->SetFieldDataName("NodeId");
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, nodePoints->GetNumberOfPoints() > 0, false);
    } else {
        m_visualWidgetService->setVisible(kIiinNodeLblPropId, false, false);
    }

    if (showElems) {
        QVector<ScreenCandidate> elemCandidates = elemBuckets.values().toVector();
        std::sort(elemCandidates.begin(), elemCandidates.end(), [](const ScreenCandidate &a, const ScreenCandidate &b) {
            return a.id < b.id;
        });
        downsample(elemCandidates);

        auto elemPoly   = vtkSmartPointer<vtkPolyData>::New();
        auto elemPoints = vtkSmartPointer<vtkPoints>::New();
        auto elemIdArr  = vtkSmartPointer<vtkIntArray>::New();
        elemIdArr->SetName("ElemId");
        elemIdArr->SetNumberOfComponents(1);

        for (const ScreenCandidate &c : elemCandidates) {
            elemPoints->InsertNextPoint(c.x, c.y, c.z);
            elemIdArr->InsertNextValue(c.id);
        }

        elemPoly->SetPoints(elemPoints);
        elemPoly->GetPointData()->AddArray(elemIdArr);
        m_elemLabelMapper->SetInputData(elemPoly);
        m_elemLabelMapper->SetFieldDataName("ElemId");
        m_visualWidgetService->setVisible(kIiinElemLblPropId, elemPoints->GetNumberOfPoints() > 0, false);
    } else {
        m_visualWidgetService->setVisible(kIiinElemLblPropId, false, false);
    }

    const int nodeCount = showNodes ? nodeBuckets.size() : 0;
    const int elemCount = showElems ? elemBuckets.size() : 0;
    const QString summary = QString("外表面编号：框选节点 %1，框选单元 %2（分桶 %3px）")
                                .arg(nodeCount)
                                .arg(elemCount)
                                .arg(binSize);
    updateSurfaceLabelSummary(summary);

    m_visualWidgetService->render();
}

void GridEditingWidget::applyColorMode()
{
    if (!m_meshMapper || !m_visualWidgetService)
        return;

    QString arrayName;
    QSet<int> values;

    switch (m_colorMode) {
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

    if (values.isEmpty()) {
        m_meshMapper->ScalarVisibilityOff();
        m_visualWidgetService->render();
        return;
    }

    auto lut = buildLookupTable(values);
    if (!lut) {
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

vtkSmartPointer<vtkLookupTable> GridEditingWidget::buildLookupTable(const QSet<int> &values) const
{
    int maxValue = -1;
    for (int v : values) {
        maxValue = qMax(maxValue, v);
    }
    if (maxValue < 0)
        return nullptr;

    auto lut = vtkSmartPointer<vtkLookupTable>::New();
    lut->SetNumberOfTableValues(maxValue + 1);
    lut->SetTableRange(0.0, maxValue);
    lut->Build();

    for (int i = 0; i <= maxValue; ++i) {
        if (values.contains(i)) {
            QColor c = QColor::fromHsv((i * 37) % 360, 170, 220);
            lut->SetTableValue(i, c.redF(), c.greenF(), c.blueF(), 1.0);
        } else {
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
    if (!m_grid || !ui) return;

    auto pointData = m_grid->GetPointData();
    auto cellData  = m_grid->GetCellData();

    if (pickedPointId >= 0) {
        vtkIdType numPts = m_grid->GetNumberOfPoints();
        if (pickedPointId < numPts && pointData) {
            auto *nodeIdArr = vtkIntArray::SafeDownCast(pointData->GetArray("NodeId"));
            if (nodeIdArr && pickedPointId < nodeIdArr->GetNumberOfTuples()) {
                int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pickedPointId));
                ui->lineEditMoveNodeId->setText(QString::number(nodeId));
            }
        }
    }

    if (pickedCellId >= 0) {
        vtkIdType numCells = m_grid->GetNumberOfCells();
        if (pickedCellId < numCells && cellData) {
            auto *elemIdArr = vtkIntArray::SafeDownCast(cellData->GetArray("ElemId"));
            if (elemIdArr && pickedCellId < elemIdArr->GetNumberOfTuples()) {
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
    if (pointId >= nPts) return;

    auto *nodeIdArr = vtkIntArray::SafeDownCast(m_grid->GetPointData()->GetArray("NodeId"));
    if (!nodeIdArr) return;
    if (pointId >= nodeIdArr->GetNumberOfTuples()) return;

    int nodeId = nodeIdArr->GetValue(static_cast<vtkIdType>(pointId));
    int idx = nodeIndexById(nodeId);
    if (idx < 0) return;

    const auto &nodes = m_model.nodes();
    const Node &n = nodes[idx];

    m_isDragging     = true;
    m_dragVtkPointId = pointId;
    m_dragNodeId     = nodeId;

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
    if (!m_isDragging || !m_points) return;

    int idx = nodeIndexById(m_dragNodeId);
    if (idx < 0) return;

    auto &nodes = m_model.nodes();
    if (idx >= nodes.size()) return;

    Node &n = nodes[idx];
    n.x = worldPos[0];
    n.y = worldPos[1];
    if (m_model.madim() == 3) n.z = worldPos[2];
    else                      n.z = 0.0;

    vtkIdType pid = static_cast<vtkIdType>(m_dragVtkPointId);
    if (pid >= 0 && pid < m_points->GetNumberOfPoints()) {
        m_points->SetPoint(pid, n.x, n.y, n.z);
        m_points->Modified();
    }

    if (m_visualWidgetService) {
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
    vtkCell *cell = m_grid->GetCell(cid);
    if (!cell) return;

    vtkIdList *ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() == 0) return;

    double bestDepth = 1e9;
    vtkIdType bestPid = -1;

    const double pixelTol2 = 15.0 * 15.0;

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
        vtkIdType pid = ids->GetId(i);

        double world[3];
        m_points->GetPoint(pid, world);

        m_renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
        m_renderer->WorldToDisplay();

        double disp[3];
        m_renderer->GetDisplayPoint(disp);

        double dx = disp[0] - displayX;
        double dy = disp[1] - displayY;
        double d2 = dx*dx + dy*dy;

        if (d2 > pixelTol2) continue;

        if (disp[2] < bestDepth) {
            bestDepth = disp[2];
            bestPid   = pid;
        }
    }

    if (bestPid >= 0) {
        beginNodeDragByPointId(static_cast<long long>(bestPid));
    }
}

void GridEditingWidget::endNodeDrag()
{
    if (!m_isDragging) return;

    m_isDragging     = false;
    m_dragVtkPointId = -1;
    m_dragNodeId     = -1;

    m_dirty = true;
}

void GridEditingWidget::beforeLoad()
{
    auto *meshService = ServiceRegistry::instance().getService<MeshModuleControllerService>();
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

}
