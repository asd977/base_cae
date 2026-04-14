#include "PickManager.h"

#include <PublicVariables.h>
#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <algorithm>
#include <cmath>
#include <limits>


// VTK
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCommand.h>
#include <vtkIdList.h>
#include <vtkLine.h>
#include <vtkMapper.h>
#include <vtkDataSetMapper.h>
#include <vtkCoordinate.h>
#include <vtkNew.h>
#include <vtkFieldData.h>
#include <vtkMath.h>
#include <vtkPlane.h>
#include <vtkPointData.h>
#include <vtkPolygon.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkInteractorStyle.h>
#include <vtkInteractorObserver.h>
#include <vtkInformation.h>
#include <vtkProperty2D.h>
#include <vtkRenderer.h>
#include <vtkSelection.h>
#include <vtkSelectionNode.h>

namespace
{
vtkDataSet* dataSetFromProp(vtkProp* prop)
{
    auto* actor = vtkActor::SafeDownCast(prop);
    if (!actor)
        return nullptr;

    auto* mapper = actor->GetMapper();
    if (!mapper)
        return nullptr;

    return vtkDataSet::SafeDownCast(mapper->GetInput());
}

vtkDataArray* findArray(vtkFieldData* fd, const char* a, const char* b = nullptr)
{
    if (!fd)
        return nullptr;
    if (a)
    {
        if (auto* arr = fd->GetArray(a))
            return arr;
    }
    if (b)
        return fd->GetArray(b);
    return nullptr;
}
} // namespace

static inline double dist2PointToSegment(const double x[3], const double a[3], const double b[3])
{
    double t = 0.0, closest[3];
    vtkLine::DistanceToLine(x, a, b, t, closest);
    return vtkMath::Distance2BetweenPoints(x, closest);
}

static inline quint64 makeEdgePairKey(vtkIdType a, vtkIdType b)
{
    const quint32 lo = quint32(std::min(a, b));
    const quint32 hi = quint32(std::max(a, b));
    return (quint64(hi) << 32) | quint64(lo);
}

PickManager& PickManager::instance()
{
    static PickManager s;
    return s;
}

PickManager::PickManager(QObject* parent) : QObject(parent)
{
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();

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
    m_hwSelector = vtkSmartPointer<vtkHardwareSelector>::New();

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

    m_previewSelPts = vtkSmartPointer<vtkPoints>::New();
    m_previewSelPoly = vtkSmartPointer<vtkPolyData>::New();
    m_previewSelPoly->SetPoints(m_previewSelPts);
    m_previewSelGlyph = vtkSmartPointer<vtkVertexGlyphFilter>::New();
    m_previewSelGlyph->SetInputData(m_previewSelPoly);
    m_previewSelMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_previewSelMapper->SetInputConnection(m_previewSelGlyph->GetOutputPort());
    m_previewSelActor = vtkSmartPointer<vtkActor>::New();
    m_previewSelActor->SetMapper(m_previewSelMapper);
    m_previewSelActor->GetProperty()->SetPointSize(10);
    m_previewSelActor->GetProperty()->SetColor(1.0, 0.6, 0.0);
    m_previewSelActor->GetProperty()->SetRenderPointsAsSpheres(true);
    m_previewSelActor->PickableOff();
    m_previewSelActor->SetVisibility(false);
    m_visual->removeProp("pick.node.preview", false);
    m_visual->addProp("pick.node.preview", m_previewSelActor, QString("{node} 预选点"),
                      QString("拾取预选点集合"), true, true);

           // ================== drag rectangle overlay ==================
    m_dragRectPts = vtkSmartPointer<vtkPoints>::New();
    m_dragRectLines = vtkSmartPointer<vtkCellArray>::New();
    m_dragRectPoly = vtkSmartPointer<vtkPolyData>::New();
    m_dragRectPoly->SetPoints(m_dragRectPts);
    m_dragRectPoly->SetLines(m_dragRectLines);
    m_dragRectMapper = vtkSmartPointer<vtkPolyDataMapper2D>::New();
    m_dragRectMapper->SetInputData(m_dragRectPoly);
    if (auto* tcoord = m_dragRectMapper->GetTransformCoordinate())
        tcoord->SetCoordinateSystemToDisplay();
    m_dragRectActor = vtkSmartPointer<vtkActor2D>::New();
    m_dragRectActor->SetMapper(m_dragRectMapper);
    m_dragRectActor->GetProperty()->SetColor(0.1, 0.8, 1.0);
    m_dragRectActor->GetProperty()->SetLineWidth(2.0);
    m_dragRectActor->SetVisibility(false);
    m_visual->removeProp("pick.drag.rect", false);
    m_visual->addProp("pick.drag.rect", m_dragRectActor, QString("{pick} 框选框"),
                      QString("拾取框选框"), true, true);

           // ================== Edge overlay (polyline) ==================
    m_edgePts = vtkSmartPointer<vtkPoints>::New();
    m_edgeLines = vtkSmartPointer<vtkCellArray>::New();
    m_edgePoly = vtkSmartPointer<vtkPolyData>::New();
    m_edgePoly->SetPoints(m_edgePts);
    m_edgePoly->SetLines(m_edgeLines);

    m_edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_edgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
    m_edgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -10.0);
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

    m_previewEdgePts = vtkSmartPointer<vtkPoints>::New();
    m_previewEdgeLines = vtkSmartPointer<vtkCellArray>::New();
    m_previewEdgePoly = vtkSmartPointer<vtkPolyData>::New();
    m_previewEdgePoly->SetPoints(m_previewEdgePts);
    m_previewEdgePoly->SetLines(m_previewEdgeLines);
    m_previewEdgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_previewEdgeMapper->SetResolveCoincidentTopologyToPolygonOffset();
    m_previewEdgeMapper->SetRelativeCoincidentTopologyLineOffsetParameters(-1.0, -10.0);
    m_previewEdgeMapper->SetInputData(m_previewEdgePoly);
    m_previewEdgeActor = vtkSmartPointer<vtkActor>::New();
    m_previewEdgeActor->SetMapper(m_previewEdgeMapper);
    m_previewEdgeActor->GetProperty()->SetColor(1.0, 0.5, 0.0);
    m_previewEdgeActor->GetProperty()->SetLineWidth(4.0);
    m_previewEdgeActor->GetProperty()->SetRepresentationToWireframe();
    m_previewEdgeActor->PickableOff();
    m_previewEdgeActor->SetVisibility(false);
    m_visual->removeProp("pick.edge.preview", false);
    m_visual->addProp("pick.edge.preview", m_previewEdgeActor, QString("{edge} 预选边"),
                      QString("拾取预选边集合"), true, true);

           // ================== Face overlay ==================
           // --- 数据初始化 ---
    m_facePts = vtkSmartPointer<vtkPoints>::New();
    m_facePolys = vtkSmartPointer<vtkCellArray>::New();
    m_facePoly = vtkSmartPointer<vtkPolyData>::New();

    m_facePoly->SetPoints(m_facePts);
    m_facePoly->SetPolys(m_facePolys);

           // --- Mapper 配置 ---
    m_faceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_faceMapper->SetResolveCoincidentTopologyToPolygonOffset();
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

    m_previewFacePts = vtkSmartPointer<vtkPoints>::New();
    m_previewFacePolys = vtkSmartPointer<vtkCellArray>::New();
    m_previewFacePoly = vtkSmartPointer<vtkPolyData>::New();
    m_previewFacePoly->SetPoints(m_previewFacePts);
    m_previewFacePoly->SetPolys(m_previewFacePolys);
    m_previewFaceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    m_previewFaceMapper->SetResolveCoincidentTopologyToPolygonOffset();
    m_previewFaceMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -20.0);
    m_previewFaceMapper->SetInputData(m_previewFacePoly);
    m_previewFaceActor = vtkSmartPointer<vtkActor>::New();
    m_previewFaceActor->GetProperty()->BackfaceCullingOn();
    m_previewFaceActor->SetMapper(m_previewFaceMapper);
    m_previewFaceActor->GetProperty()->SetColor(1.0, 0.6, 0.0);
    m_previewFaceActor->GetProperty()->SetOpacity(1);
    m_previewFaceActor->GetProperty()->SetRepresentationToSurface();
    m_previewFaceActor->SetVisibility(false);
    m_visual->removeProp("pick.face.preview", false);
    m_visual->addProp("pick.face.preview", m_previewFaceActor, QString("{face} 预选面"),
                      QString("拾取预选面集合"), true, true);

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

           // 优先采用当前常用网格 prop；若没有导入网格则回退到全局网格 prop
    m_solidProp = m_visual ? m_visual->getProp("mesh.imported.surface") : nullptr;
    if (!m_solidProp && m_visual)
        m_solidProp = m_visual->getProp("mesh.global.surf");
    m_lineProp = m_visual ? m_visual->getProp("mesh.imported.line") : nullptr;
    m_solidPickDataSet = dataSetFromProp(m_solidProp);
    m_linePickDataSet = dataSetFromProp(m_lineProp);
    rebuildEdgeCanonicalCache();

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

    if (m_styleDisabledByBoxSelect && m_visual)
    {
        if (auto* iren = m_visual->interactor())
        {
            if (auto* style = iren->GetInteractorStyle())
                style->SetEnabled(1);
        }
        m_styleDisabledByBoxSelect = false;
    }

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
    if (m_previewSelActor)
        m_previewSelActor->SetVisibility(false);
    if (m_previewEdgeActor)
        m_previewEdgeActor->SetVisibility(false);
    if (m_previewFaceActor)
        m_previewFaceActor->SetVisibility(false);

    clearPreviewSelection(domain, uuid, PickMode::Node);
    clearPreviewSelection(domain, uuid, PickMode::Edge);
    clearPreviewSelection(domain, uuid, PickMode::Face);

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
    if (m_previewSelActor)
        m_previewSelActor->SetVisibility(m_pickMode == PickMode::Node);
    if (m_previewEdgeActor)
        m_previewEdgeActor->SetVisibility(m_pickMode == PickMode::Edge);
    if (m_previewFaceActor)
        m_previewFaceActor->SetVisibility(m_pickMode == PickMode::Face);
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
    self->m_dragMoved = false;
    self->m_dragStart[0] = xy[0];
    self->m_dragStart[1] = xy[1];
    self->m_dragEnd[0] = xy[0];
    self->m_dragEnd[1] = xy[1];

    const bool shiftBox = iren->GetShiftKey() != 0;
    const bool ctrlBox = iren->GetControlKey() != 0;
    self->m_dragUseBox =
        ((self->m_pickMode == PickMode::Node || self->m_pickMode == PickMode::Edge ||
          self->m_pickMode == PickMode::Face) &&
         self->isBoxSelectEnabledForActive() && (shiftBox || ctrlBox));
    self->m_dragBoxDeselect = ctrlBox;
    if (!self->m_dragUseBox)
    {
        self->pickAt(xy[0], xy[1]);
        return;
    }

    self->m_cbLeftPress->AbortFlagOn();

    if (auto* style = iren->GetInteractorStyle())
    {
        style->SetEnabled(0);
        self->m_styleDisabledByBoxSelect = true;
    }

    self->updateDragRect(xy[0], xy[1], xy[0], xy[1], false);
}

void PickManager::OnMouseMove(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren || !self->m_leftDown)
        return;

    int xy[2];
    iren->GetEventPosition(xy);
    self->m_dragEnd[0] = xy[0];
    self->m_dragEnd[1] = xy[1];

    const int dx = std::abs(self->m_dragEnd[0] - self->m_dragStart[0]);
    const int dy = std::abs(self->m_dragEnd[1] - self->m_dragStart[1]);
    self->m_dragMoved = self->m_dragMoved || (dx >= 3 || dy >= 3);

    if (self->m_dragUseBox && self->m_dragMoved)
    {
        self->m_cbMouseMove->AbortFlagOn();
        self->updateDragRect(self->m_dragStart[0], self->m_dragStart[1], self->m_dragEnd[0],
                             self->m_dragEnd[1], true);
    }
}

void PickManager::OnLeftButtonRelease(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren)
        return;

    int xy[2];
    iren->GetEventPosition(xy);
    self->m_dragEnd[0] = xy[0];
    self->m_dragEnd[1] = xy[1];

    const bool useBox = self->m_dragUseBox;
    const bool dragged = self->m_dragMoved || (std::abs(self->m_dragEnd[0]-self->m_dragStart[0])>=3) ||
                         (std::abs(self->m_dragEnd[1]-self->m_dragStart[1])>=3);

    if (useBox && dragged)
    {
        self->m_cbLeftRelease->AbortFlagOn();
        self->hardwareBoxSelect(self->m_dragStart[0], self->m_dragStart[1], self->m_dragEnd[0],
                                self->m_dragEnd[1], self->m_dragBoxDeselect);
    }
    else if (!useBox)
        ;
    else
        self->pickAt(xy[0], xy[1]);

    if (self->m_styleDisabledByBoxSelect)
    {
        if (auto* style = iren->GetInteractorStyle())
            style->SetEnabled(1);
        self->m_styleDisabledByBoxSelect = false;
    }

    self->m_leftDown = false;
    self->m_dragMoved = false;
    self->m_dragUseBox = false;
    self->m_dragBoxDeselect = false;
    self->updateDragRect(0,0,0,0,false);
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
    auto isLineProp = [&](vtkProp* p) -> bool
    {
        if (!p)
            return false;
        return p == m_lineProp;
    };
    auto isSolidProp = [&](vtkProp* p) -> bool
    {
        if (!p)
            return false;
        return p == m_solidProp;
    };

    const bool isLine = isLineProp(pickedProp);
    const bool isSolid = isSolidProp(pickedProp);

    GridKind kind;
    if (isLine)
        kind = GridKind::Line;
    else if (isSolid)
        kind = GridKind::Solid;
    else
        return false;

    vtkDataSet* ds = m_cellPicker->GetDataSet();
    qDebug()<<ds;
    vtkCell* cell = ds ? ds->GetCell(cellId) : nullptr;
    if (!cell)
        return false;

    double pickPos[3];
    m_cellPicker->GetPickPosition(pickPos);

    if (m_pickMode == PickMode::Edge)
    {
        int edgeLocalId = 15;
        QVector<vtkIdType> edgePids;
        if (kind == GridKind::Solid)
        {
            auto* origCellArr = findArray(ds->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
            if (auto* ug = vtkUnstructuredGrid::SafeDownCast(ds))
            {
                vtkCell* pickedCell = ug->GetCell(cellId);
                if (pickedCell && pickedCell->GetCellDimension() <= 2 && !origCellArr)
                {
                    if (!computePickedEdge(ug, cellId, pickPos, edgeLocalId, edgePids))
                        return false;
                    toggleSelectEdge(kind, packEdgeKey(cellId, edgeLocalId));
                    return true;
                }
            }
            vtkIdType origCellId = -1;
            int origEdgeLocalId = 15;
            if (!computePickedEdgeFromBoundary(ds, cellId, pickPos, origCellId, origEdgeLocalId))
                return false;
            toggleSelectEdge(kind, packEdgeKey(origCellId, origEdgeLocalId));
        }
        else
        {
            auto* grid = vtkUnstructuredGrid::SafeDownCast(ds);
            if (!grid || !computePickedEdge(grid, cellId, pickPos, edgeLocalId, edgePids))
                return false;
            toggleSelectEdge(kind, packEdgeKey(cellId, edgeLocalId));
        }
        return true;
    }

    if (m_pickMode == PickMode::Face)
    {
        // 面拾取只针对 Solid；lineGrid 不参与面拾取。
        if (kind != GridKind::Solid)
            return false;

        int faceLocalId = 15;
        QVector<vtkIdType> facePids;
        auto* origCellArr = findArray(ds->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
        if (auto* ug = vtkUnstructuredGrid::SafeDownCast(ds))
        {
            vtkCell* pickedCell = ug->GetCell(cellId);
            if (pickedCell && pickedCell->GetCellDimension() == 2 && !origCellArr)
            {
                if (!computePickedFace(ug, cellId, pickPos, faceLocalId, facePids))
                    return false;
                toggleSelectFace(kind, packFaceKey(cellId, faceLocalId));
                return true;
            }
        }

        vtkIdType origCellId = -1;
        int origFaceLocalId = 15;
        if (!computePickedFaceFromBoundary(ds, cellId, origCellId, origFaceLocalId))
            return false;
        toggleSelectFace(kind, packFaceKey(origCellId, origFaceLocalId));
        return true;
    }

           // Node：在 cell 顶点里找最近 pid
    vtkIdList* ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() <= 0)
        return false;

    vtkIdType bestPid = -1;
    double bestD2 = std::numeric_limits<double>::max();

    vtkDataArray* origPointArr = nullptr;
    if (kind == GridKind::Solid)
        origPointArr = findArray(ds->GetPointData(), "vtkOriginalPointIds", "orig_point_id");

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = ids->GetId(i);
        double p[3];
        ds->GetPoint(pid, p);
        double d2 = vtkMath::Distance2BetweenPoints(p, pickPos);
        if (d2 < bestD2)
        {
            bestD2 = d2;
            bestPid = (origPointArr ? vtkIdType(origPointArr->GetTuple1(pid)) : pid);
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

void PickManager::rebuildEdgeCanonicalCache()
{
    m_solidCanonicalEdgeByPointPair.clear();
    m_lineCanonicalEdgeByPointPair.clear();

    auto buildOne = [this](vtkUnstructuredGrid* grid, QHash<quint64, vtkIdType>& outMap)
    {
        if (!grid)
            return;
        for (vtkIdType cid = 0; cid < grid->GetNumberOfCells(); ++cid)
        {
            vtkCell* cell = grid->GetCell(cid);
            if (!cell)
                continue;

            const int dim = cell->GetCellDimension();
            if (dim == 1)
            {
                auto* ids = cell->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                const vtkIdType edgeKey = packEdgeKey(cid, 15);
                const quint64 pairKey = makeEdgePairKey(ids->GetId(0), ids->GetId(ids->GetNumberOfIds() - 1));
                auto it = outMap.find(pairKey);
                if (it == outMap.end() || edgeKey < it.value())
                    outMap[pairKey] = edgeKey;
                continue;
            }

            const int nEdges = cell->GetNumberOfEdges();
            for (int ei = 0; ei < nEdges; ++ei)
            {
                vtkCell* e = cell->GetEdge(ei);
                if (!e)
                    continue;
                auto* ids = e->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                const vtkIdType edgeKey = packEdgeKey(cid, ei);
                const quint64 pairKey = makeEdgePairKey(ids->GetId(0), ids->GetId(ids->GetNumberOfIds() - 1));
                auto it = outMap.find(pairKey);
                if (it == outMap.end() || edgeKey < it.value())
                    outMap[pairKey] = edgeKey;
            }
        }
    };

    buildOne(m_gridSolid, m_solidCanonicalEdgeByPointPair);
    buildOne(m_gridLine, m_lineCanonicalEdgeByPointPair);
}

vtkIdType PickManager::canonicalizeEdgeKey(GridKind kind, vtkIdType edgeKey) const
{
    vtkUnstructuredGrid* grid = (kind == GridKind::Solid) ? m_gridSolid : m_gridLine;
    const auto& canonicalMap =
        (kind == GridKind::Solid) ? m_solidCanonicalEdgeByPointPair : m_lineCanonicalEdgeByPointPair;
    if (!grid || canonicalMap.isEmpty())
        return edgeKey;

    const vtkIdType cid = unpackEdgeCellId(edgeKey);
    const int ei = unpackEdgeLocalId(edgeKey);
    if (cid < 0 || cid >= grid->GetNumberOfCells())
        return edgeKey;

    vtkCell* cell = grid->GetCell(cid);
    if (!cell)
        return edgeKey;

    vtkIdType a = -1;
    vtkIdType b = -1;
    if (cell->GetCellDimension() == 1 || ei == 15)
    {
        auto* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 2)
            return edgeKey;
        a = ids->GetId(0);
        b = ids->GetId(ids->GetNumberOfIds() - 1);
    }
    else
    {
        if (ei < 0 || ei >= cell->GetNumberOfEdges())
            return edgeKey;
        vtkCell* e = cell->GetEdge(ei);
        if (!e)
            return edgeKey;
        auto* ids = e->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 2)
            return edgeKey;
        a = ids->GetId(0);
        b = ids->GetId(ids->GetNumberOfIds() - 1);
    }

    const quint64 pairKey = makeEdgePairKey(a, b);
    return canonicalMap.value(pairKey, edgeKey);
}

bool PickManager::computePickedEdgeFromBoundary(vtkDataSet* pickedDs, vtkIdType pickedCellId,
                                                const double pickPos[3],
                                                vtkIdType& outOrigCellId,
                                                int& outOrigEdgeLocalId) const
{
    outOrigCellId = -1;
    outOrigEdgeLocalId = 15;
    if (!pickedDs || !m_gridSolid)
        return false;

    auto* origCellArr = findArray(pickedDs->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
    auto* origPointArr =
        findArray(pickedDs->GetPointData(), "vtkOriginalPointIds", "orig_point_id");
    if (!origCellArr || !origPointArr)
        return false;
    if (pickedCellId < 0 || pickedCellId >= origCellArr->GetNumberOfTuples())
        return false;

    vtkCell* pickedCell = pickedDs->GetCell(pickedCellId);
    if (!pickedCell)
        return false;

    outOrigCellId = vtkIdType(origCellArr->GetTuple1(pickedCellId));
    if (outOrigCellId < 0 || outOrigCellId >= m_gridSolid->GetNumberOfCells())
        return false;
    vtkCell* origCell = m_gridSolid->GetCell(outOrigCellId);
    if (!origCell)
        return false;

    vtkIdList* pids = pickedCell->GetPointIds();
    if (!pids || pids->GetNumberOfIds() < 2)
        return false;

    vtkIdType a = -1;
    vtkIdType b = -1;
    double best = std::numeric_limits<double>::max();
    const vtkIdType nIds = pids->GetNumberOfIds();
    const bool closedLoop = (pickedCell->GetCellDimension() > 1);
    const vtkIdType nSegs = closedLoop ? nIds : (nIds - 1);
    for (vtkIdType i = 0; i < nSegs; ++i)
    {
        vtkIdType pa = pids->GetId(i);
        vtkIdType pb = pids->GetId((i + 1) % nIds);
        double p0[3], p1[3];
        pickedDs->GetPoint(pa, p0);
        pickedDs->GetPoint(pb, p1);
        const double d2 = dist2PointToSegment(pickPos, p0, p1);
        if (d2 < best)
        {
            best = d2;
            if (pa < 0 || pb < 0 || pa >= origPointArr->GetNumberOfTuples() ||
                pb >= origPointArr->GetNumberOfTuples())
                continue;
            a = vtkIdType(origPointArr->GetTuple1(pa));
            b = vtkIdType(origPointArr->GetTuple1(pb));
        }
    }
    if (a < 0 || b < 0)
        return false;

    const int nEdges = origCell->GetNumberOfEdges();
    for (int ei = 0; ei < nEdges; ++ei)
    {
        vtkCell* e = origCell->GetEdge(ei);
        if (!e)
            continue;
        vtkIdList* eids = e->GetPointIds();
        if (!eids || eids->GetNumberOfIds() < 2)
            continue;
        const vtkIdType e0 = eids->GetId(0);
        const vtkIdType e1 = eids->GetId(1);
        if ((e0 == a && e1 == b) || (e0 == b && e1 == a))
        {
            outOrigEdgeLocalId = ei;
            return true;
        }
    }
    return false;
}

void PickManager::toggleSelectEdge(GridKind kind, vtkIdType edgeKey)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    edgeKey = canonicalizeEdgeKey(kind, edgeKey);
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

bool PickManager::computePickedFaceFromBoundary(vtkDataSet* pickedDs, vtkIdType pickedCellId,
                                                vtkIdType& outOrigCellId,
                                                int& outOrigFaceLocalId) const
{
    outOrigCellId = -1;
    outOrigFaceLocalId = 15;
    if (!pickedDs || !m_gridSolid)
        return false;

    auto* origCellArr = findArray(pickedDs->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
    auto* origPointArr =
        findArray(pickedDs->GetPointData(), "vtkOriginalPointIds", "orig_point_id");
    if (!origCellArr || !origPointArr)
        return false;
    if (pickedCellId < 0 || pickedCellId >= origCellArr->GetNumberOfTuples())
        return false;

    vtkCell* pickedCell = pickedDs->GetCell(pickedCellId);
    if (!pickedCell)
        return false;

    outOrigCellId = vtkIdType(origCellArr->GetTuple1(pickedCellId));
    if (outOrigCellId < 0 || outOrigCellId >= m_gridSolid->GetNumberOfCells())
        return false;
    vtkCell* origCell = m_gridSolid->GetCell(outOrigCellId);
    if (!origCell)
        return false;

    QSet<vtkIdType> pickedOrigPids;
    vtkIdList* ids = pickedCell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() < 3)
        return false;
    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
    {
        const vtkIdType pid = ids->GetId(i);
        if (pid < 0 || pid >= origPointArr->GetNumberOfTuples())
            continue;
        pickedOrigPids.insert(vtkIdType(origPointArr->GetTuple1(pid)));
    }
    if (pickedOrigPids.isEmpty())
        return false;

           // 2D 原单元没有 faces：整片即一个“face”(localId=15)
    if (origCell->GetCellDimension() == 2)
    {
        vtkIdList* cids = origCell->GetPointIds();
        if (!cids || cids->GetNumberOfIds() != ids->GetNumberOfIds())
            return false;
        for (vtkIdType i = 0; i < cids->GetNumberOfIds(); ++i)
        {
            if (!pickedOrigPids.contains(cids->GetId(i)))
                return false;
        }
        outOrigFaceLocalId = 15;
        return true;
    }

    const int nFaces = origCell->GetNumberOfFaces();
    for (int fi = 0; fi < nFaces; ++fi)
    {
        vtkCell* face = origCell->GetFace(fi);
        if (!face)
            continue;
        vtkIdList* fids = face->GetPointIds();
        if (!fids || fids->GetNumberOfIds() != ids->GetNumberOfIds())
            continue;

        bool same = true;
        for (vtkIdType i = 0; i < fids->GetNumberOfIds(); ++i)
        {
            if (!pickedOrigPids.contains(fids->GetId(i)))
            {
                same = false;
                break;
            }
        }
        if (same)
        {
            outOrigFaceLocalId = fi;
            return true;
        }
    }
    return false;
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

void PickManager::setSelectedPointIds(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& pointIds, bool replace)
{
    if (replace)
        clearNodeSelection_NoRefresh(domain, uuid, kind);

    auto& owners =
        (kind == GridKind::Solid) ? m_ownerPointSolidByDomain[domain] : m_ownerPointLineByDomain[domain];
    auto& uuid2set = (kind == GridKind::Solid) ? m_pointsSolidByDomainUuid[domain]
                                               : m_pointsLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerPointSolidByDomain[domain]
                                                 : m_prevOwnerPointLineByDomain[domain];

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
        markDirty(DirtyNode);
}

void PickManager::setSelectedEdgeKeys(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& edgeKeys, bool replace)
{
    auto& owners =
        (kind == GridKind::Solid) ? m_ownerEdgeSolidByDomain[domain] : m_ownerEdgeLineByDomain[domain];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_edgesSolidByDomainUuid[domain] : m_edgesLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerEdgeSolidByDomain[domain]
                                                 : m_prevOwnerEdgeLineByDomain[domain];

    QSet<vtkIdType>& mySet = uuid2set[uuid];
    if (replace)
    {
        const auto oldKeys = mySet.values();
        for (vtkIdType k : oldKeys)
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

    for (vtkIdType edgeKey : edgeKeys)
    {
        edgeKey = canonicalizeEdgeKey(kind, edgeKey);
        if (mySet.contains(edgeKey))
            continue;
        const QString oldUid = owners.value(edgeKey);
        if (!oldUid.isEmpty() && oldUid != uuid)
        {
            prevOwners[edgeKey] = oldUid;
            uuid2set[oldUid].remove(edgeKey);
        }
        owners[edgeKey] = uuid;
        mySet.insert(edgeKey);
    }

    if (domain == m_currentDomain && uuid == m_currentUUid && m_pickMode == PickMode::Edge)
        markDirty(DirtyEdge);
}

void PickManager::setSelectedFaceKeys(const QString& domain, const QString& uuid, GridKind kind,
                                      const QVector<vtkIdType>& faceKeys, bool replace)
{
    auto& owners =
        (kind == GridKind::Solid) ? m_ownerFaceSolidByDomain[domain] : m_ownerFaceLineByDomain[domain];
    auto& uuid2set =
        (kind == GridKind::Solid) ? m_facesSolidByDomainUuid[domain] : m_facesLineByDomainUuid[domain];
    auto& prevOwners = (kind == GridKind::Solid) ? m_prevOwnerFaceSolidByDomain[domain]
                                                 : m_prevOwnerFaceLineByDomain[domain];

    QSet<vtkIdType>& mySet = uuid2set[uuid];
    if (replace)
    {
        const auto oldKeys = mySet.values();
        for (vtkIdType k : oldKeys)
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

    for (vtkIdType faceKey : faceKeys)
    {
        if (mySet.contains(faceKey))
            continue;
        const QString oldUid = owners.value(faceKey);
        if (!oldUid.isEmpty() && oldUid != uuid)
        {
            prevOwners[faceKey] = oldUid;
            uuid2set[oldUid].remove(faceKey);
        }
        owners[faceKey] = uuid;
        mySet.insert(faceKey);
    }

    if (domain == m_currentDomain && uuid == m_currentUUid && m_pickMode == PickMode::Face)
        markDirty(DirtyFace);
}

void PickManager::setPreviewPointIds(const QString& domain, const QString& uuid, GridKind kind,
                                     const QVector<vtkIdType>& pointIds)
{
    auto& mp = (kind == GridKind::Solid) ? m_previewPointsSolidByDomainUuid : m_previewPointsLineByDomainUuid;
    QSet<vtkIdType> s;
    for (auto id : pointIds)
        s.insert(id);
    mp[domain][uuid] = s;

    if (domain != m_currentDomain || uuid != m_currentUUid || m_pickMode != PickMode::Node)
        return;
    m_previewSelPts->Reset();
    const auto setS = m_previewPointsSolidByDomainUuid.value(domain).value(uuid);
    const auto setL = m_previewPointsLineByDomainUuid.value(domain).value(uuid);
    for (auto pid : setS)
    {
        if (!m_gridSolid)
            break;
        double p[3];
        m_gridSolid->GetPoint(pid, p);
        m_previewSelPts->InsertNextPoint(p);
    }
    for (auto pid : setL)
    {
        if (!m_gridLine)
            break;
        double p[3];
        m_gridLine->GetPoint(pid, p);
        m_previewSelPts->InsertNextPoint(p);
    }
    m_previewSelPts->Modified();
    m_previewSelPoly->Modified();
    if (m_visual)
        m_visual->render();
}

void PickManager::setPreviewEdgeKeys(const QString& domain, const QString& uuid, GridKind kind,
                                     const QVector<vtkIdType>& edgeKeys)
{
    auto& mp = (kind == GridKind::Solid) ? m_previewEdgesSolidByDomainUuid : m_previewEdgesLineByDomainUuid;
    QSet<vtkIdType> s;
    for (auto id : edgeKeys)
    {
        id = canonicalizeEdgeKey(kind, id);
        s.insert(id);
    }
    mp[domain][uuid] = s;
    if (domain != m_currentDomain || uuid != m_currentUUid || m_pickMode != PickMode::Edge)
        return;

    m_previewEdgePts->Reset();
    m_previewEdgeLines->Reset();
    auto buildOne = [&](vtkUnstructuredGrid* grid, const QSet<vtkIdType>& keys)
    {
        if (!grid)
            return;
        for (vtkIdType key : keys)
        {
            vtkIdType cid = unpackEdgeCellId(key);
            int ei = unpackEdgeLocalId(key);
            auto* cell = grid->GetCell(cid);
            if (!cell)
                continue;
            vtkCell* e = (cell->GetCellDimension() >= 3) ? cell->GetEdge(ei) : nullptr;
            QVector<vtkIdType> epids;
            if (e)
            {
                auto* ids = e->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    epids.push_back(ids->GetId(i));
            }
            else
            {
                auto* ids = cell->GetPointIds();
                if (!ids || ids->GetNumberOfIds() < 2)
                    continue;
                for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                    epids.push_back(ids->GetId(i));
            }
            const vtkIdType n = epids.size();
            m_previewEdgeLines->InsertNextCell(n);
            for (vtkIdType k = 0; k < n; ++k)
            {
                double p[3];
                grid->GetPoint(epids[int(k)], p);
                vtkIdType lpid = m_previewEdgePts->InsertNextPoint(p);
                m_previewEdgeLines->InsertCellPoint(lpid);
            }
        }
    };
    buildOne(m_gridSolid, m_previewEdgesSolidByDomainUuid.value(domain).value(uuid));
    buildOne(m_gridLine, m_previewEdgesLineByDomainUuid.value(domain).value(uuid));
    m_previewEdgePts->Modified();
    m_previewEdgeLines->Modified();
    m_previewEdgePoly->Modified();
    if (m_visual)
        m_visual->render();
}

void PickManager::setPreviewFaceKeys(const QString& domain, const QString& uuid, GridKind kind,
                                     const QVector<vtkIdType>& faceKeys)
{
    auto& mp = (kind == GridKind::Solid) ? m_previewFacesSolidByDomainUuid : m_previewFacesLineByDomainUuid;
    QSet<vtkIdType> s;
    for (auto id : faceKeys)
        s.insert(id);
    mp[domain][uuid] = s;
    if (domain != m_currentDomain || uuid != m_currentUUid || m_pickMode != PickMode::Face)
        return;

    m_previewFacePts->Reset();
    m_previewFacePolys->Reset();
    auto buildOne = [&](vtkUnstructuredGrid* grid, const QSet<vtkIdType>& keys)
    {
        if (!grid)
            return;
        for (vtkIdType key : keys)
        {
            vtkIdType cid = unpackFaceCellId(key);
            int fi = unpackFaceLocalId(key);
            auto* cell = grid->GetCell(cid);
            if (!cell)
                continue;
            vtkCell* f = (cell->GetCellDimension() >= 3) ? cell->GetFace(fi) : cell;
            if (!f)
                continue;
            auto* ids = f->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3)
                continue;
            const vtkIdType n = ids->GetNumberOfIds();
            m_previewFacePolys->InsertNextCell(n);
            for (vtkIdType i = 0; i < n; ++i)
            {
                double p[3];
                grid->GetPoint(ids->GetId(i), p);
                vtkIdType lpid = m_previewFacePts->InsertNextPoint(p);
                m_previewFacePolys->InsertCellPoint(lpid);
            }
        }
    };
    buildOne(m_gridSolid, m_previewFacesSolidByDomainUuid.value(domain).value(uuid));
    buildOne(m_gridLine, m_previewFacesLineByDomainUuid.value(domain).value(uuid));
    m_previewFacePts->Modified();
    m_previewFacePolys->Modified();
    m_previewFacePoly->Modified();
    if (m_visual)
        m_visual->render();
}

void PickManager::clearPreviewSelection(const QString& domain, const QString& uuid, PickMode mode)
{
    if (mode == PickMode::Node)
    {
        m_previewPointsSolidByDomainUuid[domain][uuid].clear();
        m_previewPointsLineByDomainUuid[domain][uuid].clear();
        m_previewSelPts->Reset();
        m_previewSelPts->Modified();
        m_previewSelPoly->Modified();
    }
    else if (mode == PickMode::Edge)
    {
        m_previewEdgesSolidByDomainUuid[domain][uuid].clear();
        m_previewEdgesLineByDomainUuid[domain][uuid].clear();
        m_previewEdgePts->Reset();
        m_previewEdgeLines->Reset();
        m_previewEdgePts->Modified();
        m_previewEdgeLines->Modified();
        m_previewEdgePoly->Modified();
    }
    else
    {
        m_previewFacesSolidByDomainUuid[domain][uuid].clear();
        m_previewFacesLineByDomainUuid[domain][uuid].clear();
        m_previewFacePts->Reset();
        m_previewFacePolys->Reset();
        m_previewFacePts->Modified();
        m_previewFacePolys->Modified();
        m_previewFacePoly->Modified();
    }
    if (m_visual)
        m_visual->render();
}

void PickManager::setBoxSelectEnabled(const QString& domain, const QString& uuid, bool enabled)
{
    m_boxSelectEnabledByDomainUuid[domain][uuid] = enabled;
    if (domain == m_currentDomain && uuid == m_currentUUid && !enabled)
        updateDragRect(0, 0, 0, 0, false);
}

bool PickManager::isBoxSelectEnabledForActive() const
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty())
        return false;
    return m_boxSelectEnabledByDomainUuid.value(m_currentDomain).value(m_currentUUid, true);
}

void PickManager::updateDragRect(int x0, int y0, int x1, int y1, bool visible)
{
    if (!m_dragRectActor || !m_visual || !m_visual->renderer())
        return;

    if (!visible)
    {
        m_dragRectActor->SetVisibility(false);
        if (m_visual)
            m_visual->render();
        return;
    }

    auto* ren = m_visual->renderer();
    auto* rw = ren->GetRenderWindow();
    if (!rw)
        return;
    const int* sz = rw->GetSize();
    if (!sz || sz[0] <= 0 || sz[1] <= 0)
        return;

    x0 = std::max(0, std::min(x0, sz[0] - 1));
    x1 = std::max(0, std::min(x1, sz[0] - 1));
    y0 = std::max(0, std::min(y0, sz[1] - 1));
    y1 = std::max(0, std::min(y1, sz[1] - 1));

    double p0[3] = {double(x0), double(y0), 0.0};
    double p1[3] = {double(x1), double(y0), 0.0};
    double p2[3] = {double(x1), double(y1), 0.0};
    double p3[3] = {double(x0), double(y1), 0.0};

    m_dragRectPts->Reset();
    vtkIdType id0 = m_dragRectPts->InsertNextPoint(p0);
    vtkIdType id1 = m_dragRectPts->InsertNextPoint(p1);
    vtkIdType id2 = m_dragRectPts->InsertNextPoint(p2);
    vtkIdType id3 = m_dragRectPts->InsertNextPoint(p3);

    m_dragRectLines->Reset();
    auto addSeg = [&](vtkIdType a, vtkIdType b)
    {
        vtkIdType ids[2] = {a, b};
        m_dragRectLines->InsertNextCell(2, ids);
    };
    addSeg(id0, id1);
    addSeg(id1, id2);
    addSeg(id2, id3);
    addSeg(id3, id0);

    m_dragRectPts->Modified();
    m_dragRectLines->Modified();
    m_dragRectPoly->Modified();
    m_dragRectActor->SetVisibility(true);
    m_visual->render();
}

bool PickManager::hardwareBoxSelect(int x0, int y0, int x1, int y1, bool deselect)
{
    if (!m_visual || !m_visual->renderer() || !m_hwSelector)
        return false;

    auto* ren = m_visual->renderer();
    const int xmin = std::min(x0, x1);
    const int xmax = std::max(x0, x1);
    const int ymin = std::min(y0, y1);
    const int ymax = std::max(y0, y1);
    if (xmax - xmin < 2 || ymax - ymin < 2)
        return false;

    QVector<QPair<vtkActor*, int>> actorVisBackup;
    auto hideActorForSelect = [&](vtkActor* actor)
    {
        if (!actor)
            return;
        actorVisBackup.append({actor, actor->GetVisibility()});
        actor->SetVisibility(false);
    };
    hideActorForSelect(m_selActor);
    hideActorForSelect(m_previewSelActor);
    hideActorForSelect(m_edgeActor);
    hideActorForSelect(m_previewEdgeActor);
    hideActorForSelect(m_faceActor);
    hideActorForSelect(m_previewFaceActor);

    QVector<QPair<vtkActor2D*, int>> actor2DVisBackup;
    auto hideActor2DForSelect = [&](vtkActor2D* actor)
    {
        if (!actor)
            return;
        actor2DVisBackup.append({actor, actor->GetVisibility()});
        actor->SetVisibility(false);
    };
    hideActor2DForSelect(m_dragRectActor);

    if (auto* rw = ren->GetRenderWindow())
        rw->Render();

    m_hwSelector->SetRenderer(ren);
    if (m_pickMode == PickMode::Face || m_pickMode == PickMode::Edge)
        m_hwSelector->SetFieldAssociation(vtkDataObject::FIELD_ASSOCIATION_CELLS);
    else
        m_hwSelector->SetFieldAssociation(vtkDataObject::FIELD_ASSOCIATION_POINTS);
    m_hwSelector->SetArea(xmin, ymin, xmax, ymax);

    vtkSmartPointer<vtkSelection> sel = m_hwSelector->Select();

    for (auto& it : actorVisBackup)
        it.first->SetVisibility(it.second);
    for (auto& it : actor2DVisBackup)
        it.first->SetVisibility(it.second);

    if (!sel || sel->GetNumberOfNodes() <= 0)
        return false;

    bool changed = false;
    int dirtyBits = DirtyNone;
    auto worldPointInRect = [&](const double p[3]) -> bool
    {
        vtkNew<vtkCoordinate> coord;
        coord->SetCoordinateSystemToWorld();
        coord->SetValue(p);
        double* dp = coord->GetComputedDoubleDisplayValue(ren);
        if (!dp)
            return false;
        const int sx = int(std::lround(dp[0]));
        const int sy = int(std::lround(dp[1]));
        return sx >= xmin && sx <= xmax && sy >= ymin && sy <= ymax;
    };

    for (unsigned int i = 0; i < sel->GetNumberOfNodes(); ++i)
    {
        auto* node = sel->GetNode(i);
        if (!node)
            continue;
        auto* prop = vtkProp::SafeDownCast(node->GetProperties()->Get(vtkSelectionNode::PROP()));
        if (!prop)
            continue;

        GridKind kind;
        vtkDataSet* pickedDs = dataSetFromProp(prop);
        const bool solidProp = (prop == m_solidProp);
        const bool lineProp = (prop == m_lineProp);

        if (solidProp)
            kind = GridKind::Solid;
        else if (lineProp)
            kind = GridKind::Line;
        else
            continue;

        vtkDataArray* selIds = vtkDataArray::SafeDownCast(node->GetSelectionList());
        if (!selIds)
            continue;

        if (m_pickMode == PickMode::Node)
        {
            vtkDataArray* origPointArr = nullptr;
            if (kind == GridKind::Solid && pickedDs && pickedDs != m_gridSolid)
                origPointArr =
                    findArray(pickedDs->GetPointData(), "vtkOriginalPointIds", "orig_point_id");

            for (vtkIdType j = 0; j < selIds->GetNumberOfTuples(); ++j)
            {
                const vtkIdType localPid = vtkIdType(selIds->GetComponent(j, 0));
                vtkIdType pid = localPid;
                if (origPointArr)
                {
                    if (localPid >= 0 && localPid < origPointArr->GetNumberOfTuples())
                        pid = vtkIdType(origPointArr->GetTuple1(localPid));
                    else if (!(kind == GridKind::Solid && m_gridSolid &&
                               localPid >= 0 && localPid < m_gridSolid->GetNumberOfPoints()))
                        continue;
                }
                setNodeSelected_NoRefresh(kind, pid, !deselect);
                changed = true;
            }
            dirtyBits |= DirtyNode;
            continue;
        }

        if (m_pickMode == PickMode::Edge)
        {
            if (!pickedDs)
                continue;

            auto edgeInRect = [&](const QVector<vtkIdType>& pids) -> bool
            {
                if (pids.size() < 2)
                    return false;

                for (vtkIdType pid : pids)
                {
                    double p[3];
                    pickedDs->GetPoint(pid, p);
                    if (worldPointInRect(p))
                        return true;
                }

                for (int si = 0; si + 1 < pids.size(); ++si)
                {
                    double p0[3], p1[3], pm[3];
                    pickedDs->GetPoint(pids[si], p0);
                    pickedDs->GetPoint(pids[si + 1], p1);
                    pm[0] = 0.5 * (p0[0] + p1[0]);
                    pm[1] = 0.5 * (p0[1] + p1[1]);
                    pm[2] = 0.5 * (p0[2] + p1[2]);
                    if (worldPointInRect(pm))
                        return true;
                }
                return false;
            };
            auto edgeInRectOnGrid = [&](vtkDataSet* dsForEdge, const QVector<vtkIdType>& pids) -> bool
            {
                if (!dsForEdge || pids.size() < 2)
                    return false;

                for (vtkIdType pid : pids)
                {
                    double p[3];
                    dsForEdge->GetPoint(pid, p);
                    if (worldPointInRect(p))
                        return true;
                }

                for (int si = 0; si + 1 < pids.size(); ++si)
                {
                    double p0[3], p1[3], pm[3];
                    dsForEdge->GetPoint(pids[si], p0);
                    dsForEdge->GetPoint(pids[si + 1], p1);
                    pm[0] = 0.5 * (p0[0] + p1[0]);
                    pm[1] = 0.5 * (p0[1] + p1[1]);
                    pm[2] = 0.5 * (p0[2] + p1[2]);
                    if (worldPointInRect(pm))
                        return true;
                }
                return false;
            };

            for (vtkIdType j = 0; j < selIds->GetNumberOfTuples(); ++j)
            {
                const vtkIdType pickedCellId = vtkIdType(selIds->GetComponent(j, 0));
                vtkCell* cell = pickedDs->GetCell(pickedCellId);

                // Solid surface pick dataset（来自 boundary filter）：需要映射回原始体单元 edgeLocalId。
                if (kind == GridKind::Solid && pickedDs != m_gridSolid)
                {
                    auto* origCellArr = findArray(pickedDs->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
                    auto* origPointArr =
                        findArray(pickedDs->GetPointData(), "vtkOriginalPointIds", "orig_point_id");
                    if (!m_gridSolid)
                        continue;
                    if (cell && origCellArr && origPointArr && pickedCellId >= 0 &&
                        pickedCellId < origCellArr->GetNumberOfTuples())
                    {
                        const vtkIdType origCellId = vtkIdType(origCellArr->GetTuple1(pickedCellId));
                        if (origCellId < 0 || origCellId >= m_gridSolid->GetNumberOfCells())
                            continue;
                        vtkCell* origCell = m_gridSolid->GetCell(origCellId);
                        if (!origCell)
                            continue;

                        vtkIdList* ids = cell->GetPointIds();
                        if (!ids || ids->GetNumberOfIds() < 2)
                            continue;
                        const vtkIdType nIds = ids->GetNumberOfIds();
                        const bool closedLoop = (cell->GetCellDimension() > 1);
                        const vtkIdType nSegs = closedLoop ? nIds : (nIds - 1);
                        for (vtkIdType si = 0; si < nSegs; ++si)
                        {
                            const vtkIdType pa = ids->GetId(si);
                            const vtkIdType pb = ids->GetId((si + 1) % nIds);

                            QVector<vtkIdType> segPids;
                            segPids << pa << pb;
                            if (!edgeInRect(segPids))
                                continue;

                            if (pa < 0 || pb < 0 || pa >= origPointArr->GetNumberOfTuples() ||
                                pb >= origPointArr->GetNumberOfTuples())
                                continue;
                            const vtkIdType a = vtkIdType(origPointArr->GetTuple1(pa));
                            const vtkIdType b = vtkIdType(origPointArr->GetTuple1(pb));
                            const int nEdges = origCell->GetNumberOfEdges();
                            for (int ei = 0; ei < nEdges; ++ei)
                            {
                                vtkCell* e = origCell->GetEdge(ei);
                                if (!e)
                                    continue;
                                vtkIdList* eids = e->GetPointIds();
                                if (!eids || eids->GetNumberOfIds() < 2)
                                    continue;
                                const vtkIdType e0 = eids->GetId(0);
                                const vtkIdType e1 = eids->GetId(eids->GetNumberOfIds() - 1);
                                if ((e0 == a && e1 == b) || (e0 == b && e1 == a))
                                {
                                    setEdgeSelected_NoRefresh(kind, packEdgeKey(origCellId, ei),
                                                              !deselect);
                                    changed = true;
                                    break;
                                }
                            }
                        }
                        continue;
                    }
                    vtkIdType origCellId = pickedCellId;
                    if (origCellArr && pickedCellId >= 0 && pickedCellId < origCellArr->GetNumberOfTuples())
                        origCellId = vtkIdType(origCellArr->GetTuple1(pickedCellId));
                    if (origCellId < 0 || origCellId >= m_gridSolid->GetNumberOfCells())
                        continue;
                    vtkCell* origCell = m_gridSolid->GetCell(origCellId);
                    if (!origCell)
                        continue;
                    const int nEdges = origCell->GetNumberOfEdges();
                    for (int ei = 0; ei < nEdges; ++ei)
                    {
                        vtkCell* e = origCell->GetEdge(ei);
                        if (!e)
                            continue;
                        vtkIdList* eids = e->GetPointIds();
                        if (!eids || eids->GetNumberOfIds() < 2)
                            continue;
                        QVector<vtkIdType> pids;
                        pids.reserve(int(eids->GetNumberOfIds()));
                        for (vtkIdType k = 0; k < eids->GetNumberOfIds(); ++k)
                            pids.push_back(eids->GetId(k));
                        if (!edgeInRectOnGrid(m_gridSolid, pids))
                            continue;
                        setEdgeSelected_NoRefresh(kind, packEdgeKey(origCellId, ei), !deselect);
                        changed = true;
                    }
                    continue;
                }
                if (!cell)
                    continue;

                if (cell->GetCellDimension() == 1)
                {
                    QVector<vtkIdType> pids;
                    vtkIdList* ids = cell->GetPointIds();
                    if (!ids || ids->GetNumberOfIds() < 2)
                        continue;
                    for (vtkIdType k = 0; k < ids->GetNumberOfIds(); ++k)
                        pids.push_back(ids->GetId(k));
                    if (!edgeInRect(pids))
                        continue;
                    setEdgeSelected_NoRefresh(kind, packEdgeKey(pickedCellId, 15), !deselect);
                    changed = true;
                    continue;
                }

                const int nEdges = cell->GetNumberOfEdges();
                for (int ei = 0; ei < nEdges; ++ei)
                {
                    vtkCell* e = cell->GetEdge(ei);
                    if (!e)
                        continue;
                    vtkIdList* eids = e->GetPointIds();
                    if (!eids || eids->GetNumberOfIds() < 2)
                        continue;
                    QVector<vtkIdType> pids;
                    for (vtkIdType k = 0; k < eids->GetNumberOfIds(); ++k)
                        pids.push_back(eids->GetId(k));
                    if (!edgeInRect(pids))
                        continue;
                    setEdgeSelected_NoRefresh(kind, packEdgeKey(pickedCellId, ei), !deselect);
                    changed = true;
                }
            }
            dirtyBits |= DirtyEdge;
            continue;
        }

               // PickMode::Face：仅支持 Solid（显示来自 surface filter），排除 lineGrid
        for (vtkIdType j = 0; j < selIds->GetNumberOfTuples(); ++j)
        {
            const vtkIdType pickedCellId = vtkIdType(selIds->GetComponent(j, 0));
            if (kind != GridKind::Solid || !pickedDs)
                continue;

            if (pickedDs != m_gridSolid)
            {
                vtkIdType origCellId = -1;
                int origFaceLocalId = 15;
                if (computePickedFaceFromBoundary(pickedDs, pickedCellId, origCellId, origFaceLocalId))
                {
                    setFaceSelected_NoRefresh(kind, packFaceKey(origCellId, origFaceLocalId),
                                              !deselect);
                    changed = true;
                    continue;
                }
                if (!m_gridSolid)
                    continue;
                auto* origCellArr = findArray(pickedDs->GetCellData(), "vtkOriginalCellIds", "orig_cell_id");
                origCellId = pickedCellId;
                if (origCellArr && pickedCellId >= 0 && pickedCellId < origCellArr->GetNumberOfTuples())
                    origCellId = vtkIdType(origCellArr->GetTuple1(pickedCellId));
                if (origCellId < 0 || origCellId >= m_gridSolid->GetNumberOfCells())
                    continue;
                vtkCell* solidCell = m_gridSolid->GetCell(origCellId);
                if (!solidCell)
                    continue;
                if (solidCell->GetCellDimension() == 2)
                {
                    setFaceSelected_NoRefresh(kind, packFaceKey(origCellId, 15), !deselect);
                    changed = true;
                    continue;
                }
                const int nFaces = solidCell->GetNumberOfFaces();
                for (int fi = 0; fi < nFaces; ++fi)
                {
                    setFaceSelected_NoRefresh(kind, packFaceKey(origCellId, fi), !deselect);
                    changed = true;
                }
                continue;
            }

            vtkCell* solidCell = m_gridSolid->GetCell(pickedCellId);
            if (!solidCell)
                continue;
            if (solidCell->GetCellDimension() == 2)
            {
                setFaceSelected_NoRefresh(kind, packFaceKey(pickedCellId, 15), !deselect);
                changed = true;
                continue;
            }

            const int nFaces = solidCell->GetNumberOfFaces();
            for (int fi = 0; fi < nFaces; ++fi)
            {
                setFaceSelected_NoRefresh(kind, packFaceKey(pickedCellId, fi), !deselect);
                changed = true;
            }
        }
        dirtyBits |= DirtyFace;
    }

    if (changed && dirtyBits != DirtyNone)
        markDirty(dirtyBits);
    return changed;
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

    edgeKey = canonicalizeEdgeKey(kind, edgeKey);
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
