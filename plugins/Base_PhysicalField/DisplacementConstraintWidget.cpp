#include "DisplacementConstraintWidget.h"
#include "ui_DisplacementConstraintWidget.h"
#include "DisplacementConstraintSelectionManager.h"

#include <QDebug>
#include <ServiceRegistry.h>

#include <services/MeshModuleControllerService.h>
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

static const char* kNodeIdArrayName = "node_id";
static const QString kSelectedPointsPropId = QStringLiteral("constraint.selected_points");

DisplacementConstraintWidget::DisplacementConstraintWidget(QWidget *parent)
    : BasePage("DisplacementConstraintWidget")
    , ui(new Ui::DisplacementConstraintWidget)
{
    ui->setupUi(this);
    ui->uxRadio->setChecked(true);
    DisplacementConstraintSelectionManager::instance().registerWidget(this);

    auto updateSelection = [this](bool checked) {
        if (!checked) return;
        applyConstraintToSelectedNodes();
    };
    connect(ui->uxRadio, &QRadioButton::toggled, this, updateSelection);
    connect(ui->uyRadio, &QRadioButton::toggled, this, updateSelection);
    connect(ui->uzRadio, &QRadioButton::toggled, this, updateSelection);
}

DisplacementConstraintWidget::~DisplacementConstraintWidget()
{
    teardownPicking();
    DisplacementConstraintSelectionManager::instance().unregisterWidget(this);
    delete ui;
}

void DisplacementConstraintWidget::beforeLoad()
{
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    m_grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;

    if (!m_visual) {
        qWarning() << "[DisplacementConstraintWidget] VisualWidgetService is null.";
        return;
    }
    if (!m_grid || m_grid->GetNumberOfPoints() == 0) {
        qWarning() << "[DisplacementConstraintWidget] grid is null or empty.";
        return;
    }

    // cache node_id array (file node ids)
    m_nodeIdArr = vtkIntArray::SafeDownCast(
        m_grid->GetPointData()->GetArray(kNodeIdArrayName)
        );
    if (!m_nodeIdArr) {
        qWarning() << "[DisplacementConstraintWidget] Missing PointData array 'node_id'. "
                      "Fallback nodeId = vtkPointId+1 (only correct if ids are 1..N).";
    }

    // picker/callback
    if (!m_picker) {
        m_picker = vtkSmartPointer<vtkCellPicker>::New();
        m_picker->SetTolerance(0.0005);
    }
    if (!m_cbLeftPress) {
        m_cbLeftPress = vtkSmartPointer<vtkCallbackCommand>::New();
        m_cbLeftPress->SetClientData(this);
        m_cbLeftPress->SetCallback(&DisplacementConstraintWidget::OnLeftButtonPress);
    }

    // create one actor for selected points (initially empty)
    ensureSelectedPointsActor();

    // build nodeId->vtkPid map once (for fast multi-select updates)
    buildNodeIdToVtkPidMap();

    rebuildSelectionIndex();
    rebuildSelectedPoints();
    updateLastSelectedFallback();

    // 如果你希望进入页面默认启用点选，可打开：
    // enablePicking(true);
}

void DisplacementConstraintWidget::beforeUnload()
{
    enablePicking(false);

    // 可选：离开页面就把选中点显示移除（保留也行）
    if (m_visual) {
        m_visual->removeProp(kSelectedPointsPropId, false);
    }

    m_selActor = nullptr;
    m_selMapper = nullptr;
    m_selGlyph = nullptr;
    m_selPoly = nullptr;
    m_selPts = nullptr;

    m_nodeId2VtkPid.clear();

    m_nodeIdArr = nullptr;
    m_grid = nullptr;
    m_visual = nullptr;
}

void DisplacementConstraintWidget::on_clickSelectionCheckBox_checkStateChanged(const Qt::CheckState &arg1)
{
    enablePicking(arg1 == Qt::Checked);
}

void DisplacementConstraintWidget::enablePicking(bool on)
{
    if (m_pickingEnabled == on) return;
    m_pickingEnabled = on;

    if (on) setupPicking();
    else teardownPicking();

    qDebug() << "[DisplacementConstraintWidget] picking enabled =" << m_pickingEnabled;
}

void DisplacementConstraintWidget::setupPicking()
{
    if (!m_visual) return;
    auto iren = m_visual->interactor(); // 你的 VisualWidgetService 需要提供
    if (!iren) {
        qWarning() << "[DisplacementConstraintWidget] interactor() is null.";
        return;
    }

    teardownPicking();
    m_leftPressTag = iren->AddObserver(vtkCommand::LeftButtonPressEvent, m_cbLeftPress);
}

void DisplacementConstraintWidget::teardownPicking()
{
    if (!m_visual) return;
    auto iren = m_visual->interactor();
    if (!iren) return;

    if (m_leftPressTag != 0) {
        iren->RemoveObserver(m_leftPressTag);
        m_leftPressTag = 0;
    }
}

void DisplacementConstraintWidget::OnLeftButtonPress(vtkObject* caller,
                                                     unsigned long,
                                                     void* clientData,
                                                     void*)
{
    auto* self = static_cast<DisplacementConstraintWidget*>(clientData);
    if (!self || !self->m_pickingEnabled) return;

    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!iren) return;

    int xy[2];
    iren->GetEventPosition(xy);
    self->pickAt(xy[0], xy[1]);
}

bool DisplacementConstraintWidget::pickAt(int x, int y)
{
    if (!m_visual || !m_grid || !m_picker) return false;

    auto ren = m_visual->renderer(); // 你的 VisualWidgetService 需要提供
    if (!ren) {
        qWarning() << "[DisplacementConstraintWidget] renderer() is null.";
        return false;
    }

    // 1) pick a cell
    if (!m_picker->Pick(x, y, 0.0, ren)) return false;

    vtkIdType cellId = m_picker->GetCellId();
    if (cellId < 0) return false;

    vtkCell* cell = m_grid->GetCell(cellId);
    if (!cell) return false;

    // 2) choose closest vertex among cell vertices
    double pickPos[3];
    m_picker->GetPickPosition(pickPos);

    vtkIdList* ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() <= 0) return false;

    vtkIdType bestPid = -1;
    double bestD2 = VTK_DOUBLE_MAX;

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i) {
        vtkIdType pid = ids->GetId(i);
        double p[3];
        m_grid->GetPoint(pid, p);
        double d2 = vtkMath::Distance2BetweenPoints(p, pickPos);
        if (d2 < bestD2) { bestD2 = d2; bestPid = pid; }
    }
    if (bestPid < 0) return false;

    // 3) vtkPid -> nodeId
    int nodeId = -1;
    if (m_nodeIdArr) nodeId = m_nodeIdArr->GetValue(bestPid);
    else nodeId = static_cast<int>(bestPid + 1);

    // 4) toggle selection and update one-actor rendering
    toggleSelectNode(nodeId, bestPid);

    // 5) debug output
    double pSel[3];
    m_grid->GetPoint(bestPid, pSel);
    qDebug().noquote()
        << QString("[Pick] nodeId=%1 vtkPid=%2 xyz=(%3,%4,%5)  selectedCount=%6")
               .arg(nodeId)
               .arg(static_cast<long long>(bestPid))
               .arg(pSel[0], 0, 'g', 16)
               .arg(pSel[1], 0, 'g', 16)
               .arg(pSel[2], 0, 'g', 16)
               .arg(m_selNodeIds.size());

    return true;
}

void DisplacementConstraintWidget::ensureSelectedPointsActor()
{
    if (m_selActor) return;
    if (!m_visual) return;

    m_selPts = vtkSmartPointer<vtkPoints>::New();
    m_selPoly = vtkSmartPointer<vtkPolyData>::New();
    m_selPoly->SetPoints(m_selPts);

    m_selGlyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_selGlyph->SetInputData(m_selPoly);

    m_selMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_selMapper->SetInputConnection(m_selGlyph->GetOutputPort());

    m_selActor = vtkSmartPointer<vtkActor>::New();
    m_selActor->SetMapper(m_selMapper);
    m_selActor->GetProperty()->SetPointSize(8); // 视觉反馈（不增加 actor 数量）
    m_selActor->GetProperty()->SetColor(1.0, 0.1, 0.1); // 亮红色（显眼）
    m_selActor->GetProperty()->SetOpacity(1.0);     // 不透明
    m_selActor->GetProperty()->SetRenderPointsAsSpheres(true);

    // add to scene ONCE
    m_visual->removeProp(kSelectedPointsPropId, false);
    m_visual->addProp(kSelectedPointsPropId,
                      m_selActor,
                      QStringLiteral("选中点"),
                      QStringLiteral("位移约束选中点集合"),
                      true,
                      true);

    // 初次渲染
    m_visual->render();
}

void DisplacementConstraintWidget::buildNodeIdToVtkPidMap()
{
    m_nodeId2VtkPid.clear();
    if (!m_grid || m_grid->GetNumberOfPoints() == 0) return;

    // 如果没有 node_id，就没必要建表（只能用 pid+1 退化）
    if (!m_nodeIdArr) return;

    const vtkIdType N = m_grid->GetNumberOfPoints();
    m_nodeId2VtkPid.reserve(static_cast<int>(N * 1.3));

    for (vtkIdType pid = 0; pid < N; ++pid) {
        int nodeId = m_nodeIdArr->GetValue(pid);
        m_nodeId2VtkPid.insert(nodeId, pid);
    }
}

void DisplacementConstraintWidget::toggleSelectNode(int nodeId, vtkIdType vtkPid)
{
    // 已选则取消，否则选中
    if (m_nodeId2SelIndex.contains(nodeId)) {
        removeSelected(nodeId);
        m_nodeConstraints.remove(nodeId);
        DisplacementConstraintSelectionManager::instance().releaseSelection(this, nodeId);
        if (m_lastSelectedNodeId == nodeId) {
            m_lastSelectedNodeId = -1;
            updateLastSelectedFallback();
        }
    } else {
        DisplacementConstraintSelectionManager::instance().requestSelection(this, nodeId);
        addSelected(nodeId, vtkPid);
        applyConstraintToNode(nodeId);
        m_lastSelectedNodeId = nodeId;
        updateLastSelectedLabels(nodeId);
    }

    // 让 VTK 知道数据变了
    if (m_selPts) m_selPts->Modified();
    if (m_selPoly) m_selPoly->Modified();

    if (m_visual) m_visual->render();
}

void DisplacementConstraintWidget::addSelected(int nodeId, vtkIdType vtkPid)
{
    if (!m_grid || !m_selPts) return;

    // 记录索引
    const int newIndex = m_selNodeIds.size();
    m_selNodeIds.push_back(nodeId);
    m_nodeId2SelIndex.insert(nodeId, newIndex);

    // 插入点坐标
    double p[3];
    m_grid->GetPoint(vtkPid, p);
    m_selPts->InsertNextPoint(p);
}

void DisplacementConstraintWidget::removeSelected(int nodeId)
{
    auto it = m_nodeId2SelIndex.find(nodeId);
    if (it == m_nodeId2SelIndex.end()) return;

    const int idx = it.value();
    const int lastIdx = m_selNodeIds.size() - 1;

    // swap-delete：用最后一个覆盖要删除的位置（O(1)）
    if (idx != lastIdx) {
        const int lastNodeId = m_selNodeIds[lastIdx];

        if (m_selPts) {
            // 把最后一个点坐标搬到 idx
            double lastP[3];
            m_selPts->GetPoint(lastIdx, lastP);
            m_selPts->SetPoint(idx, lastP);
        }

        // 更新 nodeId 列表与映射
        m_selNodeIds[idx] = lastNodeId;
        m_nodeId2SelIndex[lastNodeId] = idx;
    }

    // 删最后一个
    m_selNodeIds.pop_back();
    m_nodeId2SelIndex.remove(nodeId);

    // vtkPoints 没有 RemoveLastPoint，最稳的做法：缩小点数
    if (m_selPts) {
        m_selPts->SetNumberOfPoints(m_selNodeIds.size());
    }
}

void DisplacementConstraintWidget::rebuildSelectedPoints()
{
    if (!m_selPts || !m_grid) return;

    m_selPts->Reset();
    m_selPts->SetNumberOfPoints(0);

    for (int nodeId : m_selNodeIds) {
        vtkIdType pid = resolveVtkPid(nodeId);
        if (pid < 0) continue;
        double p[3];
        m_grid->GetPoint(pid, p);
        m_selPts->InsertNextPoint(p);
    }

    m_selPts->Modified();
    if (m_selPoly) m_selPoly->Modified();
    if (m_visual) m_visual->render();
}

vtkIdType DisplacementConstraintWidget::resolveVtkPid(int nodeId) const
{
    if (m_nodeId2VtkPid.contains(nodeId)) return m_nodeId2VtkPid.value(nodeId);
    if (!m_nodeIdArr && m_grid) {
        const vtkIdType pid = static_cast<vtkIdType>(nodeId - 1);
        if (pid >= 0 && pid < m_grid->GetNumberOfPoints()) return pid;
    }
    return -1;
}

void DisplacementConstraintWidget::rebuildSelectionIndex()
{
    m_nodeId2SelIndex.clear();
    for (int i = 0; i < m_selNodeIds.size(); ++i) {
        m_nodeId2SelIndex.insert(m_selNodeIds[i], i);
    }
}

DisplacementConstraintWidget::DisplacementConstraintData
DisplacementConstraintWidget::currentConstraintFromUi() const
{
    DisplacementConstraintData data;

    if (ui->uxRadio->isChecked()) {
        data.uxFixed = true;
    } else if (ui->uyRadio->isChecked()) {
        data.uyFixed = true;
    } else if (ui->uzRadio->isChecked()) {
        data.uzFixed = true;
    } else {
        data.uxFixed = true;
    }

    return data;
}

void DisplacementConstraintWidget::applyConstraintToNode(int nodeId)
{
    m_nodeConstraints.insert(nodeId, currentConstraintFromUi());
}

void DisplacementConstraintWidget::applyConstraintToSelectedNodes()
{
    if (m_selNodeIds.isEmpty()) return;

    const auto constraint = currentConstraintFromUi();
    for (int nodeId : m_selNodeIds) {
        m_nodeConstraints.insert(nodeId, constraint);
    }

    if (m_lastSelectedNodeId >= 0) {
        updateLastSelectedLabels(m_lastSelectedNodeId);
    } else {
        updateLastSelectedFallback();
    }
}

void DisplacementConstraintWidget::updateLastSelectedLabels(int nodeId)
{
    const auto it = m_nodeConstraints.find(nodeId);
    if (it == m_nodeConstraints.end()) {
        clearLastSelectedLabels();
        return;
    }

    const auto& data = it.value();
    const auto fixedText = [](bool fixed, double value) {
        return fixed ? QString::number(value, 'g', 16) : QStringLiteral("自由");
    };

    ui->IDLabel->setText(QString::number(nodeId));
    ui->UXLabel->setText(fixedText(data.uxFixed, data.uxValue));
    ui->UYLabel->setText(fixedText(data.uyFixed, data.uyValue));
    ui->UZLabel->setText(fixedText(data.uzFixed, data.uzValue));
}

void DisplacementConstraintWidget::updateLastSelectedFallback()
{
    if (m_lastSelectedNodeId >= 0 && m_nodeConstraints.contains(m_lastSelectedNodeId)) {
        updateLastSelectedLabels(m_lastSelectedNodeId);
        return;
    }

    if (!m_selNodeIds.isEmpty()) {
        m_lastSelectedNodeId = m_selNodeIds.last();
        updateLastSelectedLabels(m_lastSelectedNodeId);
        return;
    }

    clearLastSelectedLabels();
}

void DisplacementConstraintWidget::clearLastSelectedLabels()
{
    ui->IDLabel->clear();
    ui->UXLabel->clear();
    ui->UYLabel->clear();
    ui->UZLabel->clear();
}

void DisplacementConstraintWidget::removeSelectedByManager(int nodeId)
{
    if (!m_nodeId2SelIndex.contains(nodeId)) return;

    removeSelected(nodeId);
    m_nodeConstraints.remove(nodeId);
    if (m_lastSelectedNodeId == nodeId) {
        m_lastSelectedNodeId = -1;
        updateLastSelectedFallback();
    }

    if (m_selPts) m_selPts->Modified();
    if (m_selPoly) m_selPoly->Modified();
    if (m_visual) m_visual->render();
}
