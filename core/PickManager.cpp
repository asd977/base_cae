#include "PickManager.h"

#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>
#include <algorithm>
#include <limits>

// VTK
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkHardwareSelector.h>
#include <vtkIdList.h>
#include <vtkLine.h>
#include <vtkMapper.h>
#include <vtkMath.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPolygon.h>
#include <vtkProperty.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSelectionNode.h>

static inline double dist2PointToSegment(const double x[3], const double a[3], const double b[3])
{
    double t = 0.0, closest[3];
    vtkLine::DistanceToLine(x, a, b, t, closest);
    return vtkMath::Distance2BetweenPoints(x, closest);
}

static inline void sortRect(int& x0, int& y0, int& x1, int& y1)
{
    if (x0 > x1)
        std::swap(x0, x1);
    if (y0 > y1)
        std::swap(y0, y1);
}

PickManager& PickManager::instance()
{
    static PickManager s;
    return s;
}

PickManager::PickManager(QObject* parent) : QObject(parent)
{
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();

    // 全局解决共面闪烁（面/边 overlay）
    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
    vtkMapper::SetResolveCoincidentTopologyPolygonOffsetParameters(+1.0, +1000.0);

    // callbacks
    m_cbLeftPress = vtkSmartPointer<vtkCallbackCommand>::New();
    m_cbLeftPress->SetClientData(this);
    m_cbLeftPress->SetCallback(&PickManager::OnLeftButtonPress);

    m_cbLeftRelease = vtkSmartPointer<vtkCallbackCommand>::New();
    m_cbLeftRelease->SetClientData(this);
    m_cbLeftRelease->SetCallback(&PickManager::OnLeftButtonRelease);

    m_cbMouseMove = vtkSmartPointer<vtkCallbackCommand>::New();
    m_cbMouseMove->SetClientData(this);
    m_cbMouseMove->SetCallback(&PickManager::OnMouseMove);

    m_cellPicker = vtkSmartPointer<vtkCellPicker>::New();
    m_cellPicker->SetTolerance(0.01);

    m_areaPicker = vtkSmartPointer<vtkAreaPicker>::New();

    if (!m_visual)
        return;

    // ================== Node overlay ==================
    m_selPts = vtkSmartPointer<vtkPoints>::New();
    m_selPoly = vtkSmartPointer<vtkPolyData>::New();
    m_selPoly->SetPoints(m_selPts);

    m_selGlyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_selGlyph->SetInputData(m_selPoly);

    m_selMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_selMapper->SetInputConnection(m_selGlyph->GetOutputPort());

    m_selActor = vtkSmartPointer<vtkActor>::New();
    m_selActor->SetMapper(m_selMapper);
    m_selActor->GetProperty()->SetPointSize(8);
    m_selActor->GetProperty()->SetColor(1.0, 0.1, 0.1);
    m_selActor->GetProperty()->SetOpacity(1.0);
    m_selActor->GetProperty()->SetRenderPointsAsSpheres(true);
    m_selActor->PickableOff();

    m_visual->removeProp("pick.node", false);
    m_visual->addProp("pick.node", m_selActor, QString("{node} 选中点"), QString("拾取选中点集合"),
                      true, true);

    // ================== Edge overlay (polyline) ==================
    m_edgePts = vtkSmartPointer<vtkPoints>::New();
    m_edgeLines = vtkSmartPointer<vtkCellArray>::New();
    m_edgePoly = vtkSmartPointer<vtkPolyData>::New();
    m_edgePoly->SetPoints(m_edgePts);
    m_edgePoly->SetLines(m_edgeLines);

    m_edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_edgeMapper->SetInputData(m_edgePoly);

    m_edgeActor = vtkSmartPointer<vtkActor>::New();
    m_edgeActor->SetMapper(m_edgeMapper);
    m_edgeActor->GetProperty()->SetColor(1.0, 1.0, 0.1);
    m_edgeActor->GetProperty()->SetLineWidth(3.0);
    m_edgeActor->GetProperty()->SetRepresentationToWireframe();
    m_edgeActor->PickableOff();
    m_edgeActor->SetVisibility(false);

    m_visual->removeProp("pick.edge", false);
    m_visual->addProp("pick.edge", m_edgeActor, QString("{edge} 选中边"),
                      QString("拾取选中边集合（edgeKey）"), true, true);

    // ================== Face overlay ==================
    // --- 数据初始化 ---
    m_facePts = vtkSmartPointer<vtkPoints>::New();
    m_facePolys = vtkSmartPointer<vtkCellArray>::New();
    m_facePoly = vtkSmartPointer<vtkPolyData>::New();

    m_facePoly->SetPoints(m_facePts);
    m_facePoly->SetPolys(m_facePolys);

    // --- Mapper 配置 ---
    m_faceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_faceMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -20.0);
    m_faceMapper->SetInputData(m_facePoly);

    // --- Actor 属性设置 ---
    m_faceActor = vtkSmartPointer<vtkActor>::New();
    m_faceActor->GetProperty()->BackfaceCullingOn();
    m_faceActor->SetMapper(m_faceMapper);
    m_faceActor->GetProperty()->SetColor(0.2, 0.6, 1.0);
    m_faceActor->GetProperty()->SetOpacity(1);
    m_faceActor->GetProperty()->SetRepresentationToSurface();
    m_faceActor->SetVisibility(false);

    // --- 交互控制 (高亮 actor 不参与 pick) ---
    m_selActor->PickableOff();
    m_edgeActor->PickableOff();
    m_faceActor->PickableOff();

    m_visual->removeProp("pick.face", false);
    m_visual->addProp("pick.face", m_faceActor, QString("{face} 选中面"), QString("拾取选中面集合"),
                      true, true);

    // ================== Line node hints ==================
    m_lineNodePts = vtkSmartPointer<vtkPoints>::New();
    m_lineNodePoly = vtkSmartPointer<vtkPolyData>::New();
    m_lineNodePoly->SetPoints(m_lineNodePts);

    m_lineNodeGlyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_lineNodeGlyph->SetInputData(m_lineNodePoly);

    m_lineNodeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_lineNodeMapper->SetInputConnection(m_lineNodeGlyph->GetOutputPort());
    m_lineNodeMapper->ScalarVisibilityOff();

    m_lineNodeActor = vtkSmartPointer<vtkActor>::New();
    m_lineNodeActor->SetMapper(m_lineNodeMapper);
    m_lineNodeActor->GetProperty()->SetPointSize(5);
    m_lineNodeActor->GetProperty()->SetColor(0.0, 0.0, 0.0);
    m_lineNodeActor->GetProperty()->SetOpacity(1.0);
    m_lineNodeActor->GetProperty()->SetRenderPointsAsSpheres(true);
    m_lineNodeActor->PickableOff();
    m_lineNodeActor->SetVisibility(false);

    m_visual->removeProp("pick.line.nodes", false);
    m_visual->addProp("pick.line.nodes", m_lineNodeActor, QString("{line} 节点提示"),
                      QString("线网格节点提示"), true, true);

    // ================== Rubber band box (2D overlay) ==================
    m_boxPts = vtkSmartPointer<vtkPoints>::New();
    m_boxLines = vtkSmartPointer<vtkCellArray>::New();
    m_boxPoly = vtkSmartPointer<vtkPolyData>::New();
    m_boxPoly->SetPoints(m_boxPts);
    m_boxPoly->SetLines(m_boxLines);

    // 4个点，4条线（矩形）
    m_boxPts->SetNumberOfPoints(4);
    m_boxLines->Reset();
    m_boxLines->InsertNextCell(5); // 闭合 polyline：0-1-2-3-0
    m_boxLines->InsertCellPoint(0);
    m_boxLines->InsertCellPoint(1);
    m_boxLines->InsertCellPoint(2);
    m_boxLines->InsertCellPoint(3);
    m_boxLines->InsertCellPoint(0);

    m_boxCoord = vtkSmartPointer<vtkCoordinate>::New();
    m_boxCoord->SetCoordinateSystemToDisplay(); // 关键：用屏幕坐标

    m_boxMapper2D = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    m_boxMapper2D->SetInputData(m_boxPoly);
    m_boxMapper2D->SetTransformCoordinate(m_boxCoord);

    m_boxActor2D = vtkSmartPointer<vtkActor2D>::New();
    m_boxActor2D->SetMapper(m_boxMapper2D);
    m_boxActor2D->GetProperty()->SetColor(1.0, 0.5, 0.0);
    m_boxActor2D->GetProperty()->SetLineWidth(2.0);
    m_boxActor2D->SetVisibility(false);
    m_boxActor2D->PickableOff();

    // 加到场景里（两种方式二选一）：
    if (m_visual)
    {
        m_visual->removeProp("pick.box", false);
        m_visual->addProp("pick.box", m_boxActor2D, QString("{box} 框选显示"),
                          QString("橡皮筋框选矩形"), true, true);
    }
    else
    {
        // fallback: 直接加到 renderer
        // if (auto ren = m_visual->renderer()) ren->AddActor2D(m_boxActor2D);
    }

    m_visual->render();
}

void PickManager::clearNodeSelection_NoRefresh(const QString& domain, const QString& uuid,
                                               GridKind kind)
{
    // owners: pid -> ownerUuid
    // uuid2set: uuid -> set<pid>
    // prevOwners: pid -> previousOwnerUuid （用于“抢占后可回退”）
    auto& owners = (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[domain]
                                             : m_ownerPointLineByDomain[domain];

    auto& uuid2set = (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[domain]
                                               : m_pointsLineByDomainUuid[domain];

    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[domain]
                                                 : m_prevOwnerPointLineByDomain[domain];

    QSet<vtkIdType>& mySet = uuid2set[uuid];
    if (mySet.isEmpty())
        return;

    const auto keys = mySet.values(); // copy
    for (vtkIdType pid : keys)
    {
        mySet.remove(pid);

        // 如果当前 owner 就是我，先移除 owner
        if (owners.value(pid) == uuid)
            owners.remove(pid);

        // 尝试恢复之前 owner（如果存在）
        const QString oldUid = prevOwners.take(pid);
        if (!oldUid.isEmpty())
        {
            owners[pid] = oldUid;
            uuid2set[oldUid].insert(pid);
        }
    }
}

void PickManager::updateStructure()
{
    auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    if (!meshSvc)
        return;

    m_gridSolid = meshSvc->getVtkUnstructuredGridSolid();
    m_gridLine = meshSvc->getVtkUnstructuredGridLine();

    m_nodeIdArrSolid =
        (m_gridSolid) ? vtkIntArray::SafeDownCast(m_gridSolid->GetPointData()->GetArray("node_id"))
                      : nullptr;
    m_elemIdArrSolid =
        (m_gridSolid) ? vtkIntArray::SafeDownCast(m_gridSolid->GetCellData()->GetArray("elem_id"))
                      : nullptr;

    m_nodeIdArrLine =
        (m_gridLine) ? vtkIntArray::SafeDownCast(m_gridLine->GetPointData()->GetArray("node_id"))
                     : nullptr;
    m_elemIdArrLine =
        (m_gridLine) ? vtkIntArray::SafeDownCast(m_gridLine->GetCellData()->GetArray("elem_id"))
                     : nullptr;

    // 你项目里导入网格 prop 的 key（如不同请改这里）
    m_solidProp = m_visual ? m_visual->getProp("mesh.imported.surface") : nullptr;
    m_lineProp = m_visual ? m_visual->getProp("mesh.imported.line") : nullptr;

    if (m_gridLine)
        rebuildLineNodeOverlayAll();
}

void PickManager::refreshStructureIfNeeded()
{
    const qint64 rev = PublicVariables::instance().get("pick_structure_revision").toLongLong();
    const bool needRefresh = (rev != m_structureRevisionSeen) ||
                             (m_gridSolid == nullptr && m_gridLine == nullptr) ||
                             (m_visual && (m_solidProp == nullptr && m_lineProp == nullptr));

    if (needRefresh)
    {
        updateStructure();
        m_structureRevisionSeen = rev;
    }
}

void PickManager::activate(const QString& domain, const QString& uuid, PickMode mode)
{
    m_currentDomain = domain;
    m_currentUUid = uuid;
    m_pickMode = mode;

    refreshStructureIfNeeded();
    updateActorVisibility();

    // line node hints 仅 node 模式显示
    if (m_lineNodeActor)
    {
        const bool hasLine = (m_gridLine && m_gridLine->GetNumberOfPoints() > 0);
        const bool show = m_showLineNodes && hasLine && (m_pickMode == PickMode::Node);
        m_lineNodeActor->SetVisibility(show);
    }

    // 只标脏一次，统一 rebuild/render
    if (mode == PickMode::Node)
        markDirty(DirtyNode);
    if (mode == PickMode::Edge)
        markDirty(DirtyEdge);
    if (mode == PickMode::Face)
        markDirty(DirtyFace);

    setupPicking();
}

void PickManager::deactivate(const QString& domain, const QString& uuid, PickMode mode)
{
    if (domain != m_currentDomain || uuid != m_currentUUid)
        return;

    if (mode == PickMode::Node)
        commitNodeExclusive(domain, uuid);
    if (mode == PickMode::Edge)
        commitEdgeExclusive(domain, uuid);
    if (mode == PickMode::Face)
        commitFaceExclusive(domain, uuid);

    teardownPicking();

    m_currentDomain.clear();
    m_currentUUid.clear();

    if (m_selActor)
        m_selActor->SetVisibility(false);
    if (m_edgeActor)
        m_edgeActor->SetVisibility(false);
    if (m_faceActor)
        m_faceActor->SetVisibility(false);
    if (m_lineNodeActor)
        m_lineNodeActor->SetVisibility(false);

    // 释放 overlay 内容（避免长时间占内存）
    if (m_selPts)
    {
        m_selPts->Reset();
        m_selPts->Modified();
    }
    if (m_selPoly)
        m_selPoly->Modified();

    if (m_edgePts)
    {
        m_edgePts->Reset();
        m_edgePts->Modified();
    }
    if (m_edgeLines)
    {
        m_edgeLines->Reset();
        m_edgeLines->Modified();
    }
    if (m_edgePoly)
        m_edgePoly->Modified();

    if (m_facePts)
    {
        m_facePts->Reset();
        m_facePts->Modified();
    }
    if (m_facePolys)
    {
        m_facePolys->Reset();
        m_facePolys->Modified();
    }
    if (m_facePoly)
        m_facePoly->Modified();

    if (m_visual)
        m_visual->render();
}

void PickManager::updateActorVisibility()
{
    if (!m_selActor || !m_edgeActor || !m_faceActor)
        return;
    m_selActor->SetVisibility(m_pickMode == PickMode::Node);
    m_edgeActor->SetVisibility(m_pickMode == PickMode::Edge);
    m_faceActor->SetVisibility(m_pickMode == PickMode::Face);
}

void PickManager::setupPicking()
{
    if (!m_visual)
        return;
    auto iren = m_visual->interactor();
    if (!iren)
        return;

    teardownPicking();

    // 全装，逻辑在回调里分流：不按Shift拖拽=相机；Shift拖拽=框选
    m_leftPressTag = iren->AddObserver(vtkCommand::LeftButtonPressEvent, m_cbLeftPress, 1.0);
    m_leftReleaseTag = iren->AddObserver(vtkCommand::LeftButtonReleaseEvent, m_cbLeftRelease, 1.0);
    m_mouseMoveTag = iren->AddObserver(vtkCommand::MouseMoveEvent, m_cbMouseMove, 1.0);
}

void PickManager::teardownPicking()
{
    if (!m_visual)
        return;
    auto iren = m_visual->interactor();
    if (!iren)
        return;

    if (m_leftPressTag)
    {
        iren->RemoveObserver(m_leftPressTag);
        m_leftPressTag = 0;
    }
    if (m_leftReleaseTag)
    {
        iren->RemoveObserver(m_leftReleaseTag);
        m_leftReleaseTag = 0;
    }
    if (m_mouseMoveTag)
    {
        iren->RemoveObserver(m_mouseMoveTag);
        m_mouseMoveTag = 0;
    }

    m_boxing = false;
    m_mouseDown = false;
    m_dragging = false;
    setBoxRectVisible(false);
}

void PickManager::OnLeftButtonPress(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren)
        return;

    int xy[2];
    iren->GetEventPosition(xy);

    self->m_leftDown = true;
    self->m_dragging = false; // 关键：每次按下重置
    self->m_leftDownPos[0] = xy[0];
    self->m_leftDownPos[1] = xy[1];

    // 恢复单击即选中（避免出现需要双击才生效）
    self->pickAt(xy[0], xy[1]);
}

void PickManager::OnMouseMove(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren)
        return;

    int xy[2];
    iren->GetEventPosition(xy);

    Q_UNUSED(xy);
}

void PickManager::OnLeftButtonRelease(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren)
        return;

    int xy[2];
    iren->GetEventPosition(xy);

    self->m_leftDown = false;

    // OnLeftButtonPress 已处理点选，这里不再重复 pick，避免双触发
}

bool PickManager::pickAt(int x, int y)
{
    if (!m_visual || !m_cellPicker)
        return false;
    auto ren = m_visual->renderer();
    if (!ren)
        return false;

    if (!m_cellPicker->Pick(x, y, 0.0, ren))
        return false;

    vtkIdType cellId = m_cellPicker->GetCellId();
    if (cellId < 0)
        return false;

    vtkProp* pickedProp = m_cellPicker->GetViewProp();
    const bool isLine = (pickedProp && pickedProp == m_lineProp);
    const bool isSolid = (pickedProp && pickedProp == m_solidProp);

    GridKind kind;
    if (isLine)
        kind = GridKind::Line;
    else if (isSolid)
        kind = GridKind::Solid;
    else
        return false;

    vtkDataSet* ds = m_cellPicker->GetDataSet();
    auto* grid = vtkUnstructuredGrid::SafeDownCast(ds);
    if (!grid)
        return false;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return false;

    double pickPos[3];
    m_cellPicker->GetPickPosition(pickPos);

    if (m_pickMode == PickMode::Edge)
    {
        int edgeLocalId = 15;
        QVector<vtkIdType> edgePids;
        if (!computePickedEdge(grid, cellId, pickPos, edgeLocalId, edgePids))
            return false;

        toggleSelectEdge(kind, packEdgeKey(cellId, edgeLocalId));
        return true;
    }

    if (m_pickMode == PickMode::Face)
    {
        int faceLocalId = 15;
        QVector<vtkIdType> facePids;
        if (!computePickedFace(grid, cellId, pickPos, faceLocalId, facePids))
            return false;

        toggleSelectFace(kind, packFaceKey(cellId, faceLocalId));
        return true;
    }

    // Node：在 cell 顶点里找最近 pid
    vtkIdList* ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() <= 0)
        return false;

    vtkIdType bestPid = -1;
    double bestD2 = std::numeric_limits<double>::max();

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = ids->GetId(i);
        double p[3];
        grid->GetPoint(pid, p);
        double d2 = vtkMath::Distance2BetweenPoints(p, pickPos);
        if (d2 < bestD2)
        {
            bestD2 = d2;
            bestPid = pid;
        }
    }
    if (bestPid < 0)
        return false;

    toggleSelectNode(kind, bestPid);
    return true;
}

// ===================== coalesced rebuild/render =====================
void PickManager::markDirty(int bits)
{
    m_dirtyBits |= bits;
    if (m_rebuildScheduled)
        return;

    m_rebuildScheduled = true;
    QTimer::singleShot(0, this,
                       [this]()
                       {
                           flushRebuild();
                       });
}

void PickManager::flushRebuild()
{
    m_rebuildScheduled = false;

    if (!m_visual)
    {
        m_dirtyBits = DirtyNone;
        return;
    }

    updateActorVisibility();

    if (m_dirtyBits & DirtyNode)
        rebuildPointsForActive();
    if (m_dirtyBits & DirtyEdge)
        rebuildEdgePolyForActive();
    if (m_dirtyBits & DirtyFace)
        rebuildFacePolyForActive();

    m_dirtyBits = DirtyNone;
    m_visual->render();
}

// ===================== Node =====================
void PickManager::toggleSelectNode(GridKind kind, vtkIdType pid)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[dom] : m_ownerPointLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[dom] : m_pointsLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[dom]
                                                 : m_prevOwnerPointLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];
    QString prevUuid;

    if (mySet.contains(pid))
    {
        mySet.remove(pid);
        if (owners.value(pid) == uid)
            owners.remove(pid);

        const QString oldUid = prevOwners.take(pid);
        if (!oldUid.isEmpty())
        {
            owners[pid] = oldUid;
            uuid2set[oldUid].insert(pid);
        }
    }
    else
    {
        const QString oldUid = owners.value(pid);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevUuid = oldUid;
            prevOwners[pid] = oldUid;
            uuid2set[oldUid].remove(pid);
        }
        owners[pid] = uid;
        mySet.insert(pid);
    }

    // 发信号（单点选）
    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? m_gridSolid : m_gridLine;
    vtkIntArray* nodeIdArr = (kind == GridKind::Solid) ? m_nodeIdArrSolid : m_nodeIdArrLine;
    if (!nodeIdArr && grid)
        nodeIdArr = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));

    if (grid && nodeIdArr)
    {
        const vtkIdType nPts = grid->GetNumberOfPoints();
        if (pid >= 0 && pid < nPts)
        {
            double p[3];
            grid->GetPoint(pid, p);
            const int nodeId = nodeIdArr->GetValue(int(pid));
            emit nodePicked(dom, uid, kind, pid, nodeId, p[0], p[1], p[2], prevUuid);
        }
    }

    markDirty(DirtyNode);
}

void PickManager::rebuildPointsForActive()
{
    if (!m_selPts)
        return;

    m_selPts->Reset();

    // 预估点数，减少内部扩容
    vtkIdType approx = 0;
    if (m_gridSolid)
        approx += m_pointsSolidByDomainUuid.value(m_currentDomain).value(m_currentUUid).size();
    if (m_gridLine)
        approx += m_pointsLineByDomainUuid.value(m_currentDomain).value(m_currentUUid).size();
    if (approx > 0)
        m_selPts->Allocate(approx);

    if (m_gridSolid)
    {
        const auto setS = m_pointsSolidByDomainUuid.value(m_currentDomain).value(m_currentUUid);
        for (vtkIdType pid : setS)
        {
            if (pid < 0 || pid >= m_gridSolid->GetNumberOfPoints())
                continue;
            double p[3];
            m_gridSolid->GetPoint(pid, p);
            m_selPts->InsertNextPoint(p);
        }
    }

    if (m_gridLine)
    {
        const auto setL = m_pointsLineByDomainUuid.value(m_currentDomain).value(m_currentUUid);
        for (vtkIdType pid : setL)
        {
            if (pid < 0 || pid >= m_gridLine->GetNumberOfPoints())
                continue;
            double p[3];
            m_gridLine->GetPoint(pid, p);
            m_selPts->InsertNextPoint(p);
        }
    }

    m_selPts->Modified();
    if (m_selPoly)
        m_selPoly->Modified();
}

// ===================== Edge =====================
bool PickManager::computePickedEdge(vtkUnstructuredGrid* grid, vtkIdType cellId,
                                    const double pickPos[3], int& outEdgeLocalId,
                                    QVector<vtkIdType>& outEdgePids) const
{
    outEdgePids.clear();
    if (!grid)
        return false;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return false;

    const int dim = cell->GetCellDimension();

    // 1D：整条线就是 edge
    if (dim == 1)
    {
        vtkIdList* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 2)
            return false;

        outEdgeLocalId = 15; // sentinel
        outEdgePids.reserve(ids->GetNumberOfIds());
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            outEdgePids.push_back(ids->GetId(i));
        return true;
    }

    const int nEdges = cell->GetNumberOfEdges();
    if (nEdges <= 0)
        return false;

    int bestEi = -1;
    double bestD2 = std::numeric_limits<double>::max();
    QVector<vtkIdType> bestPids;

    for (int ei = 0; ei < nEdges; ++ei)
    {
        vtkCell* e = cell->GetEdge(ei);
        if (!e)
            continue;

        vtkIdList* eids = e->GetPointIds();
        if (!eids || eids->GetNumberOfIds() < 2)
            continue;

        const int n = int(eids->GetNumberOfIds());
        QVector<vtkIdType> epids;
        epids.reserve(n);
        for (int k = 0; k < n; ++k)
            epids.push_back(eids->GetId(k));

        double minSegD2 = std::numeric_limits<double>::max();
        for (int k = 0; k < n - 1; ++k)
        {
            double pa[3], pb[3];
            grid->GetPoint(epids[k], pa);
            grid->GetPoint(epids[k + 1], pb);

            const double d2 = dist2PointToSegment(pickPos, pa, pb);
            if (d2 < minSegD2)
                minSegD2 = d2;
        }

        if (minSegD2 < bestD2)
        {
            bestD2 = minSegD2;
            bestEi = ei;
            bestPids = epids;
        }
    }

    if (bestEi < 0)
        return false;
    outEdgeLocalId = bestEi;
    outEdgePids = bestPids;
    return true;
}

void PickManager::toggleSelectEdge(GridKind kind, vtkIdType edgeKey)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerEdgeSolidByDomain[dom] : m_ownerEdgeLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_edgesSolidByDomainUuid[dom] : m_edgesLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerEdgeSolidByDomain[dom]
                                                 : m_prevOwnerEdgeLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (mySet.contains(edgeKey))
    {
        mySet.remove(edgeKey);
        if (owners.value(edgeKey) == uid)
            owners.remove(edgeKey);

        const QString oldUid = prevOwners.take(edgeKey);
        if (!oldUid.isEmpty())
        {
            owners[edgeKey] = oldUid;
            uuid2set[oldUid].insert(edgeKey);
        }
    }
    else
    {
        const QString oldUid = owners.value(edgeKey);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevOwners[edgeKey] = oldUid;
            uuid2set[oldUid].remove(edgeKey);
        }
        owners[edgeKey] = uid;
        mySet.insert(edgeKey);
    }

    markDirty(DirtyEdge);
}

void PickManager::rebuildEdgePolyForActive()
{
    if (!m_edgePts || !m_edgeLines || !m_edgePoly)
        return;

    m_edgePts->Reset();
    m_edgeLines->Reset();

    const auto setS = m_edgesSolidByDomainUuid.value(m_currentDomain).value(m_currentUUid);
    const auto setL = m_edgesLineByDomainUuid.value(m_currentDomain).value(m_currentUUid);

    // 预估：每条边至少2点
    vtkIdType approxPts = vtkIdType(setS.size() + setL.size()) * 2;
    if (approxPts > 0)
        m_edgePts->Allocate(approxPts);

    auto buildOne = [&](vtkUnstructuredGrid* grid, const QSet<vtkIdType>& set)
    {
        if (!grid)
            return;

        for (vtkIdType edgeKey : set)
        {
            vtkIdType cellId = unpackEdgeCellId(edgeKey);
            int edgeLocalId = unpackEdgeLocalId(edgeKey);
            if (cellId < 0 || cellId >= grid->GetNumberOfCells())
                continue;

            vtkCell* cell = grid->GetCell(cellId);
            if (!cell)
                continue;

            QVector<vtkIdType> epids;

            if (cell->GetCellDimension() == 1 || edgeLocalId == 15)
            {
                vtkIdList* ids = cell->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    epids.push_back(ids->GetId(i));
            }
            else
            {
                if (edgeLocalId < 0 || edgeLocalId >= cell->GetNumberOfEdges())
                    continue;
                vtkCell* e = cell->GetEdge(edgeLocalId);
                if (!e)
                    continue;
                vtkIdList* ids = e->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    epids.push_back(ids->GetId(i));
            }

            const vtkIdType n = epids.size();
            m_edgeLines->InsertNextCell(n);
            for (vtkIdType k = 0; k < n; ++k)
            {
                double p[3];
                grid->GetPoint(epids[int(k)], p);
                vtkIdType lpid = m_edgePts->InsertNextPoint(p);
                m_edgeLines->InsertCellPoint(lpid);
            }
        }
    };

    buildOne(m_gridSolid, setS);
    buildOne(m_gridLine, setL);

    m_edgePts->Modified();
    m_edgeLines->Modified();
    m_edgePoly->Modified();
}

// ===================== Face =====================
bool PickManager::computePickedFace(vtkUnstructuredGrid* grid, vtkIdType cellId,
                                    const double pickPos[3], int& outFaceLocalId,
                                    QVector<vtkIdType>& outFacePids) const
{
    outFacePids.clear();
    if (!grid)
        return false;

    vtkCell* cell = grid->GetCell(cellId);
    if (!cell)
        return false;

    const int dim = cell->GetCellDimension();

    // 2D：整片就是 face
    if (dim == 2)
    {
        vtkIdList* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 3)
            return false;
        outFaceLocalId = 15;
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            outFacePids.push_back(ids->GetId(i));
        return true;
    }

    if (dim != 3)
        return false;

    const int nFaces = cell->GetNumberOfFaces();
    if (nFaces <= 0)
        return false;

    int bestFace = -1;
    double bestScore = std::numeric_limits<double>::max();
    QVector<vtkIdType> bestPids;

    for (int fi = 0; fi < nFaces; ++fi)
    {
        vtkCell* face = cell->GetFace(fi);
        if (!face)
            continue;

        vtkIdList* fids = face->GetPointIds();
        if (!fids || fids->GetNumberOfIds() < 3)
            continue;

        const int npts = int(fids->GetNumberOfIds());
        QVector<double> pts(npts * 3);

        double bounds[6] = {
            +std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
            +std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(),
            +std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()};

        for (int i = 0; i < npts; ++i)
        {
            double p[3];
            grid->GetPoint(fids->GetId(i), p);
            pts[i * 3 + 0] = p[0];
            pts[i * 3 + 1] = p[1];
            pts[i * 3 + 2] = p[2];

            bounds[0] = std::min(bounds[0], p[0]);
            bounds[1] = std::max(bounds[1], p[0]);
            bounds[2] = std::min(bounds[2], p[1]);
            bounds[3] = std::max(bounds[3], p[1]);
            bounds[4] = std::min(bounds[4], p[2]);
            bounds[5] = std::max(bounds[5], p[2]);
        }

        double n[3] = {0, 0, 0};
        vtkPolygon::ComputeNormal(npts, pts.data(), n);
        if (vtkMath::Norm(n) < 1e-12)
            continue;

        double p0[3] = {pts[0], pts[1], pts[2]};

        const double dist =
            std::abs(vtkPlane::DistanceToPlane(const_cast<double*>(pickPos), n, p0));
        const int inside =
            vtkPolygon::PointInPolygon(const_cast<double*>(pickPos), npts, pts.data(), bounds, n);
        const double score = inside ? dist : (dist + 1e3);

        if (score < bestScore)
        {
            bestScore = score;
            bestFace = fi;
            bestPids.clear();
            for (int i = 0; i < npts; ++i)
                bestPids.push_back(fids->GetId(i));
        }
    }

    if (bestFace < 0 || bestPids.isEmpty())
        return false;
    outFaceLocalId = bestFace;
    outFacePids = bestPids;
    return true;
}

void PickManager::toggleSelectFace(GridKind kind, vtkIdType faceKey)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerFaceSolidByDomain[dom] : m_ownerFaceLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_facesSolidByDomainUuid[dom] : m_facesLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerFaceSolidByDomain[dom]
                                                 : m_prevOwnerFaceLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (mySet.contains(faceKey))
    {
        mySet.remove(faceKey);
        if (owners.value(faceKey) == uid)
            owners.remove(faceKey);

        const QString oldUid = prevOwners.take(faceKey);
        if (!oldUid.isEmpty())
        {
            owners[faceKey] = oldUid;
            uuid2set[oldUid].insert(faceKey);
        }
    }
    else
    {
        const QString oldUid = owners.value(faceKey);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevOwners[faceKey] = oldUid;
            uuid2set[oldUid].remove(faceKey);
        }
        owners[faceKey] = uid;
        mySet.insert(faceKey);
    }

    markDirty(DirtyFace);
}

void PickManager::rebuildFacePolyForActive()
{
    if (!m_facePts || !m_facePolys || !m_facePoly)
        return;

    m_facePts->Reset();
    m_facePolys->Reset();

    const auto setS = m_facesSolidByDomainUuid.value(m_currentDomain).value(m_currentUUid);
    const auto setL = m_facesLineByDomainUuid.value(m_currentDomain).value(m_currentUUid);

    // 粗略预估：每个面至少3点
    vtkIdType approxPts = vtkIdType(setS.size() + setL.size()) * 3;
    if (approxPts > 0)
        m_facePts->Allocate(approxPts);

    auto buildOne = [&](vtkUnstructuredGrid* grid, const QSet<vtkIdType>& set)
    {
        if (!grid)
            return;

        for (vtkIdType faceKey : set)
        {
            vtkIdType cellId = unpackFaceCellId(faceKey);
            int faceLocalId = unpackFaceLocalId(faceKey);
            if (cellId < 0 || cellId >= grid->GetNumberOfCells())
                continue;

            vtkCell* cell = grid->GetCell(cellId);
            if (!cell)
                continue;

            QVector<vtkIdType> fpids;

            if (cell->GetCellDimension() == 2 || faceLocalId == 15)
            {
                vtkIdList* ids = cell->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 3)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    fpids.push_back(ids->GetId(i));
            }
            else
            {
                if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
                    continue;
                vtkCell* f = cell->GetFace(faceLocalId);
                if (!f)
                    continue;
                vtkIdList* ids = f->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 3)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    fpids.push_back(ids->GetId(i));
            }

            vtkIdType n = fpids.size();
            m_facePolys->InsertNextCell(n);
            for (vtkIdType i = 0; i < n; ++i)
            {
                double p[3];
                grid->GetPoint(fpids[int(i)], p);
                vtkIdType lpid = m_facePts->InsertNextPoint(p);
                m_facePolys->InsertCellPoint(lpid);
            }
        }
    };

    buildOne(m_gridSolid, setS);
    buildOne(m_gridLine, setL);

    m_facePts->Modified();
    m_facePolys->Modified();
    m_facePoly->Modified();
}

// ===================== clearSelection =====================
void PickManager::clearSelection(const QString& domain, const QString& uuid, PickMode mode)
{
    auto clearOneKind = [&](GridKind kind)
    {
        if (mode == PickMode::Node)
        {
            auto& owners = (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[domain]
                                                     : m_ownerPointLineByDomain[domain];
            auto& uuid2set = (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[domain]
                                                       : m_pointsLineByDomainUuid[domain];
            auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[domain]
                                                         : m_prevOwnerPointLineByDomain[domain];

            QSet<vtkIdType>& mySet = uuid2set[uuid];
            const auto keys = mySet.values(); // copy
            for (vtkIdType k : keys)
            {
                mySet.remove(k);
                if (owners.value(k) == uuid)
                    owners.remove(k);

                const QString oldUid = prevOwners.take(k);
                if (!oldUid.isEmpty())
                {
                    owners[k] = oldUid;
                    uuid2set[oldUid].insert(k);
                }
            }
            return;
        }

        if (mode == PickMode::Edge)
        {
            auto& owners = (kind == GridKind::Solid) ? m_ownerEdgeSolidByDomain[domain]
                                                     : m_ownerEdgeLineByDomain[domain];
            auto& uuid2set = (kind == GridKind::Solid) ? m_edgesSolidByDomainUuid[domain]
                                                       : m_edgesLineByDomainUuid[domain];
            auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerEdgeSolidByDomain[domain]
                                                         : m_prevOwnerEdgeLineByDomain[domain];

            QSet<vtkIdType>& mySet = uuid2set[uuid];
            const auto keys = mySet.values();
            for (vtkIdType k : keys)
            {
                mySet.remove(k);
                if (owners.value(k) == uuid)
                    owners.remove(k);

                const QString oldUid = prevOwners.take(k);
                if (!oldUid.isEmpty())
                {
                    owners[k] = oldUid;
                    uuid2set[oldUid].insert(k);
                }
            }
            return;
        }

        if (mode == PickMode::Face)
        {
            auto& owners = (kind == GridKind::Solid) ? m_ownerFaceSolidByDomain[domain]
                                                     : m_ownerFaceLineByDomain[domain];
            auto& uuid2set = (kind == GridKind::Solid) ? m_facesSolidByDomainUuid[domain]
                                                       : m_facesLineByDomainUuid[domain];
            auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerFaceSolidByDomain[domain]
                                                         : m_prevOwnerFaceLineByDomain[domain];

            QSet<vtkIdType>& mySet = uuid2set[uuid];
            const auto keys = mySet.values();
            for (vtkIdType k : keys)
            {
                mySet.remove(k);
                if (owners.value(k) == uuid)
                    owners.remove(k);

                const QString oldUid = prevOwners.take(k);
                if (!oldUid.isEmpty())
                {
                    owners[k] = oldUid;
                    uuid2set[oldUid].insert(k);
                }
            }
            return;
        }
    };

    clearOneKind(GridKind::Solid);
    clearOneKind(GridKind::Line);

    if (domain == m_currentDomain && uuid == m_currentUUid && mode == m_pickMode)
    {
        if (mode == PickMode::Node)
            markDirty(DirtyNode);
        if (mode == PickMode::Edge)
            markDirty(DirtyEdge);
        if (mode == PickMode::Face)
            markDirty(DirtyFace);
    }
}

// ===================== box select (batch) =====================
void PickManager::setNodeSelected_NoRefresh(GridKind kind, vtkIdType pid, bool selected)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[dom] : m_ownerPointLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[dom] : m_pointsLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[dom]
                                                 : m_prevOwnerPointLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (selected)
    {
        if (mySet.contains(pid))
            return;
        const QString oldUid = owners.value(pid);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevOwners[pid] = oldUid;
            uuid2set[oldUid].remove(pid);
        }
        owners[pid] = uid;
        mySet.insert(pid);
    }
    else
    {
        if (!mySet.contains(pid))
            return;
        mySet.remove(pid);
        if (owners.value(pid) == uid)
            owners.remove(pid);

        const QString oldUid = prevOwners.take(pid);
        if (!oldUid.isEmpty())
        {
            owners[pid] = oldUid;
            uuid2set[oldUid].insert(pid);
        }
    }
}

void PickManager::setEdgeSelected_NoRefresh(GridKind kind, vtkIdType edgeKey, bool selected)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerEdgeSolidByDomain[dom] : m_ownerEdgeLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_edgesSolidByDomainUuid[dom] : m_edgesLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerEdgeSolidByDomain[dom]
                                                 : m_prevOwnerEdgeLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (selected)
    {
        if (mySet.contains(edgeKey))
            return;
        const QString oldUid = owners.value(edgeKey);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevOwners[edgeKey] = oldUid;
            uuid2set[oldUid].remove(edgeKey);
        }
        owners[edgeKey] = uid;
        mySet.insert(edgeKey);
    }
    else
    {
        if (!mySet.contains(edgeKey))
            return;
        mySet.remove(edgeKey);
        if (owners.value(edgeKey) == uid)
            owners.remove(edgeKey);

        const QString oldUid = prevOwners.take(edgeKey);
        if (!oldUid.isEmpty())
        {
            owners[edgeKey] = oldUid;
            uuid2set[oldUid].insert(edgeKey);
        }
    }
}

void PickManager::setFaceSelected_NoRefresh(GridKind kind, vtkIdType faceKey, bool selected)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerFaceSolidByDomain[dom] : m_ownerFaceLineByDomain[dom];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_facesSolidByDomainUuid[dom] : m_facesLineByDomainUuid[dom];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerFaceSolidByDomain[dom]
                                                 : m_prevOwnerFaceLineByDomain[dom];

    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (selected)
    {
        if (mySet.contains(faceKey))
            return;
        const QString oldUid = owners.value(faceKey);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            prevOwners[faceKey] = oldUid;
            uuid2set[oldUid].remove(faceKey);
        }
        owners[faceKey] = uid;
        mySet.insert(faceKey);
    }
    else
    {
        if (!mySet.contains(faceKey))
            return;
        mySet.remove(faceKey);
        if (owners.value(faceKey) == uid)
            owners.remove(faceKey);

        const QString oldUid = prevOwners.take(faceKey);
        if (!oldUid.isEmpty())
        {
            owners[faceKey] = oldUid;
            uuid2set[oldUid].insert(faceKey);
        }
    }
}

void PickManager::boxSelect(int x0, int y0, int x1, int y1, BoxOp op)
{
    if (!m_visual)
        return;
    auto* ren = m_visual->renderer();
    if (!ren)
        return;

    if (!m_areaPicker)
        m_areaPicker = vtkSmartPointer<vtkAreaPicker>::New();
    m_areaPicker->AreaPick(x0, y0, x1, y1, ren);

    vtkPlanes* frustum = m_areaPicker->GetFrustum();
    if (!frustum)
        return;

    auto extractCellIds = [&](vtkUnstructuredGrid* grid) -> QVector<vtkIdType>
    {
        QVector<vtkIdType> ids;
        if (!grid || grid->GetNumberOfCells() <= 0)
            return ids;

        auto ex = vtkSmartPointer<vtkExtractSelectedFrustum>::New();
        ex->SetFrustum(frustum);
        ex->PreserveTopologyOff(); // 输出 vtkOriginalCellIds
        ex->SetInputData(grid);
        ex->Update();

        vtkUnstructuredGrid* out = (vtkUnstructuredGrid*)ex->GetOutput();
        if (!out || out->GetNumberOfCells() <= 0)
            return ids;

        vtkDataArray* orig = out->GetCellData()->GetArray("vtkOriginalCellIds");
        if (!orig)
            return ids;

        const vtkIdType n = out->GetNumberOfCells();
        ids.reserve(int(n));
        for (vtkIdType i = 0; i < n; ++i)
            ids.push_back(vtkIdType(orig->GetTuple1(i)));

        std::sort(ids.begin(), ids.end());
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        return ids;
    };

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    auto applyOnGrid = [&](GridKind kind, vtkUnstructuredGrid* grid)
    {
        if (!grid)
            return;

        const auto cellIds = extractCellIds(grid);
        if (cellIds.isEmpty())
            return;

        if (m_pickMode == PickMode::Node)
        {
            for (vtkIdType cellId : cellIds)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell)
                    continue;
                vtkIdList* ids = cell->GetPointIds();
                if (!ids)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                {
                    vtkIdType pid = ids->GetId(i);
                    bool cur = (kind == GridKind::Solid)
                                   ? m_pointsSolidByDomainUuid[dom][uid].contains(pid)
                                   : m_pointsLineByDomainUuid[dom][uid].contains(pid);

                    if (op == BoxOp::Add)
                        setNodeSelected_NoRefresh(kind, pid, true);
                    else if (op == BoxOp::Remove)
                        setNodeSelected_NoRefresh(kind, pid, false);
                    else
                        setNodeSelected_NoRefresh(kind, pid, !cur);
                }
            }
            return;
        }

        if (m_pickMode == PickMode::Edge)
        {
            for (vtkIdType cellId : cellIds)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell)
                    continue;

                if (cell->GetCellDimension() == 1)
                {
                    vtkIdType k = packEdgeKey(cellId, 15);
                    bool cur = (kind == GridKind::Solid)
                                   ? m_edgesSolidByDomainUuid[dom][uid].contains(k)
                                   : m_edgesLineByDomainUuid[dom][uid].contains(k);

                    if (op == BoxOp::Add)
                        setEdgeSelected_NoRefresh(kind, k, true);
                    else if (op == BoxOp::Remove)
                        setEdgeSelected_NoRefresh(kind, k, false);
                    else
                        setEdgeSelected_NoRefresh(kind, k, !cur);
                }
                else
                {
                    const int nEdges = cell->GetNumberOfEdges();
                    for (int ei = 0; ei < nEdges; ++ei)
                    {
                        vtkIdType k = packEdgeKey(cellId, ei);
                        bool cur = (kind == GridKind::Solid)
                                       ? m_edgesSolidByDomainUuid[dom][uid].contains(k)
                                       : m_edgesLineByDomainUuid[dom][uid].contains(k);

                        if (op == BoxOp::Add)
                            setEdgeSelected_NoRefresh(kind, k, true);
                        else if (op == BoxOp::Remove)
                            setEdgeSelected_NoRefresh(kind, k, false);
                        else
                            setEdgeSelected_NoRefresh(kind, k, !cur);
                    }
                }
            }
            return;
        }

        if (m_pickMode == PickMode::Face)
        {
            for (vtkIdType cellId : cellIds)
            {
                vtkCell* cell = grid->GetCell(cellId);
                if (!cell)
                    continue;

                const int dim = cell->GetCellDimension();
                if (dim == 2)
                {
                    vtkIdType k = packFaceKey(cellId, 15);
                    bool cur = (kind == GridKind::Solid)
                                   ? m_facesSolidByDomainUuid[dom][uid].contains(k)
                                   : m_facesLineByDomainUuid[dom][uid].contains(k);

                    if (op == BoxOp::Add)
                        setFaceSelected_NoRefresh(kind, k, true);
                    else if (op == BoxOp::Remove)
                        setFaceSelected_NoRefresh(kind, k, false);
                    else
                        setFaceSelected_NoRefresh(kind, k, !cur);
                }
                else if (dim == 3)
                {
                    const int nFaces = cell->GetNumberOfFaces();
                    for (int fi = 0; fi < nFaces; ++fi)
                    {
                        vtkIdType k = packFaceKey(cellId, fi);
                        bool cur = (kind == GridKind::Solid)
                                       ? m_facesSolidByDomainUuid[dom][uid].contains(k)
                                       : m_facesLineByDomainUuid[dom][uid].contains(k);

                        if (op == BoxOp::Add)
                            setFaceSelected_NoRefresh(kind, k, true);
                        else if (op == BoxOp::Remove)
                            setFaceSelected_NoRefresh(kind, k, false);
                        else
                            setFaceSelected_NoRefresh(kind, k, !cur);
                    }
                }
            }
            return;
        }
    };

    applyOnGrid(GridKind::Solid, m_gridSolid);
    applyOnGrid(GridKind::Line, m_gridLine);

    if (m_pickMode == PickMode::Node)
        markDirty(DirtyNode);
    if (m_pickMode == PickMode::Edge)
        markDirty(DirtyEdge);
    if (m_pickMode == PickMode::Face)
        markDirty(DirtyFace);
}

// ===================== line node overlay =====================
void PickManager::rebuildLineNodeOverlayAll()
{
    if (!m_lineNodeActor)
        return;

    if (!m_showLineNodes || !m_gridLine || m_gridLine->GetNumberOfPoints() <= 0)
    {
        m_lineNodeActor->SetVisibility(false);
        return;
    }

    // 直接引用 points（不复制）
    m_lineNodePoly->SetPoints(m_gridLine->GetPoints());
    m_lineNodePoly->Modified();

    m_lineNodeActor->SetVisibility(m_pickMode == PickMode::Node);
}

// ===================== commit exclusive =====================
void PickManager::commitNodeExclusive(const QString& domain, const QString& uuid)
{
    {
        auto itPrev = m_prevOwnerPointSolidByDomain.find(domain);
        if (itPrev != m_prevOwnerPointSolidByDomain.end())
        {
            const QSet<vtkIdType> setS = m_pointsSolidByDomainUuid.value(domain).value(uuid);
            for (vtkIdType pid : setS)
                itPrev.value().remove(pid);
        }
    }
    {
        auto itPrev = m_prevOwnerPointLineByDomain.find(domain);
        if (itPrev != m_prevOwnerPointLineByDomain.end())
        {
            const QSet<vtkIdType> setL = m_pointsLineByDomainUuid.value(domain).value(uuid);
            for (vtkIdType pid : setL)
                itPrev.value().remove(pid);
        }
    }
}

void PickManager::commitEdgeExclusive(const QString& domain, const QString& uuid)
{
    {
        auto itPrev = m_prevOwnerEdgeSolidByDomain.find(domain);
        if (itPrev != m_prevOwnerEdgeSolidByDomain.end())
        {
            const QSet<vtkIdType> setS = m_edgesSolidByDomainUuid.value(domain).value(uuid);
            for (vtkIdType k : setS)
                itPrev.value().remove(k);
        }
    }
    {
        auto itPrev = m_prevOwnerEdgeLineByDomain.find(domain);
        if (itPrev != m_prevOwnerEdgeLineByDomain.end())
        {
            const QSet<vtkIdType> setL = m_edgesLineByDomainUuid.value(domain).value(uuid);
            for (vtkIdType k : setL)
                itPrev.value().remove(k);
        }
    }
}

void PickManager::commitFaceExclusive(const QString& domain, const QString& uuid)
{
    {
        auto itPrev = m_prevOwnerFaceSolidByDomain.find(domain);
        if (itPrev != m_prevOwnerFaceSolidByDomain.end())
        {
            const QSet<vtkIdType> setS = m_facesSolidByDomainUuid.value(domain).value(uuid);
            for (vtkIdType k : setS)
                itPrev.value().remove(k);
        }
    }
    {
        auto itPrev = m_prevOwnerFaceLineByDomain.find(domain);
        if (itPrev != m_prevOwnerFaceLineByDomain.end())
        {
            const QSet<vtkIdType> setL = m_facesLineByDomainUuid.value(domain).value(uuid);
            for (vtkIdType k : setL)
                itPrev.value().remove(k);
        }
    }
}

QVector<vtkIdType> PickManager::getSelectedPointIds(const QString& domain, const QString& uuid,
                                                    GridKind kind) const
{
    const QSet<vtkIdType> set = (kind == GridKind::Solid)
                                    ? m_pointsSolidByDomainUuid.value(domain).value(uuid)
                                    : m_pointsLineByDomainUuid.value(domain).value(uuid);

    QVector<vtkIdType> out;
    out.reserve(set.size());
    for (vtkIdType pid : set)
        out.push_back(pid);
    std::sort(out.begin(), out.end());
    return out;
}

// ===================== getIdList =====================
QVector<vtkIdType> PickManager::getIdList(const QString& domain, const QString& uuid,
                                          PickMode mode) const
{
    return getIdList(domain, uuid, mode, GridKind::Solid);
}

QVector<vtkIdType> PickManager::getIdList(const QString& domain, const QString& uuid, PickMode mode,
                                          GridKind kind) const
{
    QVector<vtkIdType> out;

    if (mode == PickMode::Node)
    {
        const QSet<vtkIdType> set = (kind == GridKind::Solid)
                                        ? m_pointsSolidByDomainUuid.value(domain).value(uuid)
                                        : m_pointsLineByDomainUuid.value(domain).value(uuid);

        out.reserve(set.size());

        vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? m_gridSolid : m_gridLine;
        vtkIntArray* nodeIdArr = (kind == GridKind::Solid) ? m_nodeIdArrSolid : m_nodeIdArrLine;
        if (!nodeIdArr && grid)
            nodeIdArr = vtkIntArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));

        if (!grid || !nodeIdArr)
            return out;

        const vtkIdType nPts = grid->GetNumberOfPoints();
        for (vtkIdType pid : set)
        {
            if (pid < 0 || pid >= nPts)
                continue;
            out.push_back(vtkIdType(nodeIdArr->GetValue(int(pid))));
        }
        return out;
    }

    if (mode == PickMode::Edge)
    {
        const QSet<vtkIdType> set = (kind == GridKind::Solid)
                                        ? m_edgesSolidByDomainUuid.value(domain).value(uuid)
                                        : m_edgesLineByDomainUuid.value(domain).value(uuid);

        out.reserve(set.size());
        for (vtkIdType k : set)
            out.push_back(k); // edgeKey
        return out;
    }

    if (mode == PickMode::Face)
    {
        const QSet<vtkIdType> set = (kind == GridKind::Solid)
                                        ? m_facesSolidByDomainUuid.value(domain).value(uuid)
                                        : m_facesLineByDomainUuid.value(domain).value(uuid);

        out.reserve(set.size());
        for (vtkIdType k : set)
            out.push_back(k); // faceKey
        return out;
    }

    return out;
}

// ===================== getEdgeNodesByKe / getFaceNodesByKe
// =====================
QHash<int, QVector<int>> PickManager::getEdgeNodesByKe(const QString& domain, const QString& uuid,
                                                       GridKind kind) const
{
    QHash<int, QVector<int>> nodesByKe;
    QHash<int, QSet<int>> seenByKe;

    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? m_gridSolid : m_gridLine;
    vtkDataArray* nodeIdArr = (kind == GridKind::Solid)
                                  ? static_cast<vtkDataArray*>(m_nodeIdArrSolid)
                                  : static_cast<vtkDataArray*>(m_nodeIdArrLine);
    vtkDataArray* elemIdArr = (kind == GridKind::Solid)
                                  ? static_cast<vtkDataArray*>(m_elemIdArrSolid)
                                  : static_cast<vtkDataArray*>(m_elemIdArrLine);

    if (!nodeIdArr && grid)
        nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    if (!elemIdArr && grid)
        elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

    if (!grid || !nodeIdArr)
        return nodesByKe;

    const QSet<vtkIdType> edgeKeys = (kind == GridKind::Solid)
                                         ? m_edgesSolidByDomainUuid.value(domain).value(uuid)
                                         : m_edgesLineByDomainUuid.value(domain).value(uuid);

    QVector<vtkIdType> keys = edgeKeys.values().toVector();
    std::sort(keys.begin(), keys.end(),
              [&](vtkIdType a, vtkIdType b)
              {
                  const vtkIdType ca = unpackEdgeCellId(a), cb = unpackEdgeCellId(b);
                  if (ca != cb)
                      return ca < cb;
                  return unpackEdgeLocalId(a) < unpackEdgeLocalId(b);
              });

    const vtkIdType nCells = grid->GetNumberOfCells();
    const vtkIdType nPts = grid->GetNumberOfPoints();

    for (vtkIdType edgeKey : keys)
    {
        const vtkIdType cellId = unpackEdgeCellId(edgeKey);
        const int edgeLocalId = unpackEdgeLocalId(edgeKey);
        if (cellId < 0 || cellId >= nCells)
            continue;

        const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : 0;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        QVector<vtkIdType> epids;

        if (cell->GetCellDimension() == 1 || edgeLocalId == 15)
        {
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2)
                continue;
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                epids.push_back(ids->GetId(i));
        }
        else
        {
            if (edgeLocalId < 0 || edgeLocalId >= cell->GetNumberOfEdges())
                continue;
            vtkCell* e = cell->GetEdge(edgeLocalId);
            if (!e)
                continue;
            vtkIdList* ids = e->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 2)
                continue;
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                epids.push_back(ids->GetId(i));
        }

        QVector<int>& vec = nodesByKe[ke];
        QSet<int>& seen = seenByKe[ke];

        for (vtkIdType pid : epids)
        {
            if (pid < 0 || pid >= nPts)
                continue;
            const int nodeId = int(nodeIdArr->GetTuple1(pid));
            if (seen.contains(nodeId))
                continue;
            seen.insert(nodeId);
            vec.push_back(nodeId);
        }
    }

    return nodesByKe;
}

QHash<int, QVector<int>> PickManager::getFaceNodesByKe(const QString& domain, const QString& uuid,
                                                       GridKind kind) const
{
    QHash<int, QVector<int>> nodesByKe;
    QHash<int, QSet<int>> seenByKe;

    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? m_gridSolid : m_gridLine;
    vtkDataArray* nodeIdArr = (kind == GridKind::Solid)
                                  ? static_cast<vtkDataArray*>(m_nodeIdArrSolid)
                                  : static_cast<vtkDataArray*>(m_nodeIdArrLine);
    vtkDataArray* elemIdArr = (kind == GridKind::Solid)
                                  ? static_cast<vtkDataArray*>(m_elemIdArrSolid)
                                  : static_cast<vtkDataArray*>(m_elemIdArrLine);

    if (!nodeIdArr && grid)
        nodeIdArr = vtkDataArray::SafeDownCast(grid->GetPointData()->GetArray("node_id"));
    if (!elemIdArr && grid)
        elemIdArr = vtkDataArray::SafeDownCast(grid->GetCellData()->GetArray("elem_id"));

    if (!grid || !nodeIdArr)
        return nodesByKe;

    const QSet<vtkIdType> faceKeys = (kind == GridKind::Solid)
                                         ? m_facesSolidByDomainUuid.value(domain).value(uuid)
                                         : m_facesLineByDomainUuid.value(domain).value(uuid);

    QVector<vtkIdType> keys = faceKeys.values().toVector();
    std::sort(keys.begin(), keys.end(),
              [&](vtkIdType a, vtkIdType b)
              {
                  const vtkIdType ca = unpackFaceCellId(a), cb = unpackFaceCellId(b);
                  if (ca != cb)
                      return ca < cb;
                  return unpackFaceLocalId(a) < unpackFaceLocalId(b);
              });

    const vtkIdType nCells = grid->GetNumberOfCells();
    const vtkIdType nPts = grid->GetNumberOfPoints();

    for (vtkIdType faceKey : keys)
    {
        const vtkIdType cellId = unpackFaceCellId(faceKey);
        const int faceLocalId = unpackFaceLocalId(faceKey);
        if (cellId < 0 || cellId >= nCells)
            continue;

        const int ke = elemIdArr ? int(elemIdArr->GetTuple1(cellId)) : 0;

        vtkCell* cell = grid->GetCell(cellId);
        if (!cell)
            continue;

        QVector<vtkIdType> fpids;

        if (cell->GetCellDimension() == 2 || faceLocalId == 15)
        {
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                fpids.push_back(ids->GetId(i));
        }
        else
        {
            if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces())
                continue;
            vtkCell* f = cell->GetFace(faceLocalId);
            if (!f)
                continue;
            vtkIdList* ids = f->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                fpids.push_back(ids->GetId(i));
        }

        QVector<int>& vec = nodesByKe[ke];
        QSet<int>& seen = seenByKe[ke];

        for (vtkIdType pid : fpids)
        {
            if (pid < 0 || pid >= nPts)
                continue;
            const int nodeId = int(nodeIdArr->GetTuple1(pid));
            if (seen.contains(nodeId))
                continue;
            seen.insert(nodeId);
            vec.push_back(nodeId);
        }
    }

    return nodesByKe;
}

void PickManager::updateBoxRectActor(int x0, int y0, int x1, int y1)
{
    if (!m_boxPts || !m_boxPoly)
        return;

    // 注意：VTK display 坐标原点在左下，GetEventPosition() 也是左下，匹配
    m_boxPts->SetPoint(0, x0, y0, 0.0);
    m_boxPts->SetPoint(1, x1, y0, 0.0);
    m_boxPts->SetPoint(2, x1, y1, 0.0);
    m_boxPts->SetPoint(3, x0, y1, 0.0);

    m_boxPts->Modified();
    m_boxPoly->Modified();
}

void PickManager::setBoxRectVisible(bool on)
{
    if (!m_boxActor2D)
        return;
    if (!m_showBoxRect)
        on = false;
    m_boxActor2D->SetVisibility(on);
}

void PickManager::setShowBoxRect(bool on)
{
    m_showBoxRect = on;
    if (!on)
        setBoxRectVisible(false);
    if (m_visual)
        m_visual->render();
}

void PickManager::setSelectedPointIds(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& pointIds, bool replace)
{
    if (domain.isEmpty() || uuid.isEmpty())
        return;

    auto& owners = (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[domain]
                                             : m_ownerPointLineByDomain[domain];
    auto& uuid2set = (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[domain]
                                               : m_pointsLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[domain]
                                                 : m_prevOwnerPointLineByDomain[domain];

    if (replace)
    {
        clearNodeSelection_NoRefresh(domain, uuid, kind);
    }

    QSet<vtkIdType>& mySet = uuid2set[uuid];
    for (vtkIdType pid : pointIds)
    {
        if (mySet.contains(pid))
            continue;

        const QString oldUid = owners.value(pid);
        if (!oldUid.isEmpty() && oldUid != uuid)
        {
            prevOwners[pid] = oldUid;
            uuid2set[oldUid].remove(pid);
        }

        owners[pid] = uuid;
        mySet.insert(pid);
    }

    if (domain == m_currentDomain && uuid == m_currentUUid && m_pickMode == PickMode::Node)
    {
        markDirty(DirtyNode);
    }
}

void PickManager::setSelectedEdgeKeys(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& edgeKeys, bool replace)
{
    if (domain.isEmpty() || uuid.isEmpty())
        return;

    // 选边的数据结构（按 kind 分流）
    auto& owners = (kind == GridKind::Solid) ? m_ownerEdgeSolidByDomain[domain]
                                             : m_ownerEdgeLineByDomain[domain];
    auto& uuid2set = (kind == GridKind::Solid) ? m_edgesSolidByDomainUuid[domain]
                                               : m_edgesLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerEdgeSolidByDomain[domain]
                                                 : m_prevOwnerEdgeLineByDomain[domain];

    // -------- replace：清空该 uuid 的旧选择（并恢复被抢占的归属）--------
    if (replace)
    {
        QSet<vtkIdType>& mySet = uuid2set[uuid];
        const auto keys = mySet.values(); // copy（遍历时会删）
        for (vtkIdType k : keys)
        {
            mySet.remove(k);

            // 只有当前 owner 还是我，才移除
            if (owners.value(k) == uuid)
                owners.remove(k);

            // 如果当时我“抢占”了别人，要恢复回去
            const QString oldUid = prevOwners.take(k);
            if (!oldUid.isEmpty())
            {
                owners[k] = oldUid;
                uuid2set[oldUid].insert(k);
            }
        }
    }

    // -------- 追加：把 edgeKeys 全部选上（带抢占机制）--------
    QVector<vtkIdType> keys = edgeKeys;
    if (!keys.isEmpty())
    {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }

    QSet<vtkIdType>& mySet = uuid2set[uuid];

    for (vtkIdType k : keys)
    {
        if (mySet.contains(k))
            continue;

        const QString oldUid = owners.value(k);
        if (!oldUid.isEmpty() && oldUid != uuid)
        {
            // 记录“原归属”，便于以后取消时恢复
            prevOwners[k] = oldUid;
            uuid2set[oldUid].remove(k);
        }

        owners[k] = uuid;
        mySet.insert(k);
    }

    // -------- 如果刚好是当前激活对象，并且处于 Edge 模式，则刷新高亮 --------
    if (domain == m_currentDomain && uuid == m_currentUUid && m_pickMode == PickMode::Edge)
    {
        markDirty(DirtyEdge);
    }
}

void PickManager::setSelectedFaceKeys(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& faceKeys, bool replace)
{
    if (domain.isEmpty() || uuid.isEmpty())
        return;

    // 选面的数据结构（按 kind 分流）
    auto& owners = (kind == GridKind::Solid) ? m_ownerFaceSolidByDomain[domain]
                                             : m_ownerFaceLineByDomain[domain];
    auto& uuid2set = (kind == GridKind::Solid) ? m_facesSolidByDomainUuid[domain]
                                               : m_facesLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerFaceSolidByDomain[domain]
                                                 : m_prevOwnerFaceLineByDomain[domain];

    // -------- replace：清空该 uuid 的旧选择（并恢复被抢占的归属）--------
    if (replace)
    {
        QSet<vtkIdType>& mySet = uuid2set[uuid];
        const auto keys = mySet.values(); // copy
        for (vtkIdType k : keys)
        {
            mySet.remove(k);

            if (owners.value(k) == uuid)
                owners.remove(k);

            const QString oldUid = prevOwners.take(k);
            if (!oldUid.isEmpty())
            {
                owners[k] = oldUid;
                uuid2set[oldUid].insert(k);
            }
        }
    }

    // -------- 追加：把 faceKeys 全部选上（带抢占机制）--------
    QVector<vtkIdType> keys = faceKeys;
    if (!keys.isEmpty())
    {
        std::sort(keys.begin(), keys.end());
        keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    }

    QSet<vtkIdType>& mySet = uuid2set[uuid];

    for (vtkIdType k : keys)
    {
        if (mySet.contains(k))
            continue;

        const QString oldUid = owners.value(k);
        if (!oldUid.isEmpty() && oldUid != uuid)
        {
            prevOwners[k] = oldUid;
            uuid2set[oldUid].remove(k);
        }

        owners[k] = uuid;
        mySet.insert(k);
    }

    // -------- 如果刚好是当前激活对象，并且处于 Face 模式，则刷新高亮 --------
    if (domain == m_currentDomain && uuid == m_currentUUid && m_pickMode == PickMode::Face)
    {
        markDirty(DirtyFace);
    }
}
