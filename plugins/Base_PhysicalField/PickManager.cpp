#include "PickManager.h"

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>

#include <QDebug>

// VTK
#include <vtkCommand.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkPointData.h>
#include <vtkCellData.h>
#include <vtkCell.h>
#include <vtkIdList.h>
#include <vtkMath.h>
#include <vtkProperty.h>
#include <vtkMapper.h>
#include <vtkPolygon.h>
#include <vtkPlane.h>

PickManager& PickManager::instance()
{
    static PickManager s;
    return s;
}

PickManager::PickManager(QObject* parent)
    : QObject(parent)
{
    m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();

    vtkMapper::SetResolveCoincidentTopologyToPolygonOffset();
    vtkMapper::SetResolveCoincidentTopologyPolygonOffsetParameters(+1.0, +1000.0); // 推后一点（底网格跟着走）


    m_cbLeftPress = vtkSmartPointer<vtkCallbackCommand>::New();
    m_cbLeftPress->SetClientData(this);
    m_cbLeftPress->SetCallback(&PickManager::OnLeftButtonPress);

    m_cellPicker = vtkSmartPointer<vtkCellPicker>::New();
    m_cellPicker->SetTolerance(0.0005);

    if (!m_visual) return;

    // ================== Node points actor ==================
    m_selPts  = vtkSmartPointer<vtkPoints>::New();
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

    m_visual->removeProp("pick.node", false);
    m_visual->addProp("pick.node", m_selActor,
                      QStringLiteral("{node} 选中点"),
                      QStringLiteral("拾取选中点集合"),
                      true, true);

    // ================== Edge pipeline ==================
    m_edgeSelIds = vtkSmartPointer<vtkIdTypeArray>::New();
    m_edgeSelIds->SetNumberOfComponents(1);

    m_edgeSelNode = vtkSmartPointer<vtkSelectionNode>::New();
    m_edgeSelNode->SetFieldType(vtkSelectionNode::CELL);
    m_edgeSelNode->SetContentType(vtkSelectionNode::INDICES);
    m_edgeSelNode->SetSelectionList(m_edgeSelIds);

    m_edgeSel = vtkSmartPointer<vtkSelection>::New();
    m_edgeSel->AddNode(m_edgeSelNode);

    m_edgeExtract = vtkSmartPointer<vtkExtractSelection>::New();

    m_edgeMapper = vtkSmartPointer<vtkDataSetMapper>::New();

    m_edgeActor = vtkSmartPointer<vtkActor>::New();
    m_edgeActor->SetMapper(m_edgeMapper);
    m_edgeActor->GetProperty()->SetColor(1.0, 1.0, 0.1);
    m_edgeActor->GetProperty()->SetLineWidth(3.0);
    m_edgeActor->GetProperty()->SetRepresentationToWireframe();
    m_edgeActor->SetVisibility(false);

    m_visual->removeProp("pick.edge", false);
    m_visual->addProp("pick.edge", m_edgeActor,
                      QStringLiteral("{edge} 选中线"),
                      QStringLiteral("拾取选中线集合"),
                      true, true);

    // ================== Face pipeline（PolyData，仅高亮面） ==================
    m_facePts   = vtkSmartPointer<vtkPoints>::New();
    m_facePolys = vtkSmartPointer<vtkCellArray>::New();
    m_facePoly  = vtkSmartPointer<vtkPolyData>::New();
    m_facePoly->SetPoints(m_facePts);
    m_facePoly->SetPolys(m_facePolys);

    m_faceMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    // m_faceMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-5.0, -10000.0);
    m_faceMapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -20.0);


    m_faceMapper->SetInputData(m_facePoly);

    m_faceActor = vtkSmartPointer<vtkActor>::New();
    m_faceActor->GetProperty()->BackfaceCullingOn();

    m_faceActor->SetMapper(m_faceMapper);
    m_faceActor->GetProperty()->SetColor(0.2, 0.6, 1.0);
    m_faceActor->GetProperty()->SetOpacity(1);
    m_faceActor->GetProperty()->SetRepresentationToSurface();
    m_faceActor->SetVisibility(false);

    // m_faceActor->GetProperty()->SetDepthTest(false);
    // m_edgeActor->GetProperty()->SetDepthTest(false);
    // m_selActor ->GetProperty()->SetDepthTest(false);
    m_selActor->PickableOff();
    m_edgeActor->PickableOff();
    m_faceActor->PickableOff();

    m_visual->removeProp("pick.face", false);
    m_visual->addProp("pick.face", m_faceActor,
                      QStringLiteral("{face} 选中面"),
                      QStringLiteral("拾取选中面集合"),
                      true, true);

    m_visual->render();
}

void PickManager::updateStructure()
{
    auto *meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
    m_grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
    if (!m_grid) return;

    m_nodeIdArr = vtkIntArray::SafeDownCast(m_grid->GetPointData()->GetArray("node_id"));

    // Edge 的 extract 需要 grid + selection
    if (m_edgeExtract)
    {
        m_edgeExtract->SetInputData(0, m_grid);
        m_edgeExtract->SetInputData(1, m_edgeSel);
    }
}

void PickManager::activate(const QString& domain, const QString& uuid, PickMode mode)
{
    m_currentDomain = domain;
    m_currentUUid   = uuid;
    m_pickMode      = mode;

    updateStructure();
    if (!m_grid || !m_visual) return;

    updateActorVisibility();

    if (mode == PickMode::Node)
    {
        rebuildPointsForActive();
    }
    else if (mode == PickMode::Edge)
    {
        ensureEdgePipelineReady();
        rebuildEdgesForActive();
    }
    else if (mode == PickMode::Face)
    {
        rebuildFacesForActive();
    }

    m_visual->render();
    setupPicking();
}

void PickManager::deactivate(const QString& domain, const QString& uuid, PickMode mode)
{
    if (domain != m_currentDomain || uuid != m_currentUUid) return;

    if (mode == PickMode::Node)
    {
        m_selPts->Reset();
        m_selPts->SetNumberOfPoints(0);
        m_selPts->Modified();
        if (m_selPoly) m_selPoly->Modified();
        m_pid2SelIndex.clear();
        m_selIndex2Pid.clear();
    }
    else if (mode == PickMode::Edge)
    {
        m_edgeSelIds->SetNumberOfTuples(0);
        m_edgeSelIds->Modified();
        m_edgeId2Idx.clear();
        m_edgeIdx2Id.clear();
        if (m_edgeExtract) m_edgeExtract->Update();
    }
    else if (mode == PickMode::Face)
    {
        // 清空显示（PolyData）
        if (m_facePts)   { m_facePts->Reset();   m_facePts->SetNumberOfPoints(0); }
        if (m_facePolys) { m_facePolys->Reset(); }
        if (m_facePoly)  { m_facePoly->Modified(); }
        if (m_faceMapper){ m_faceMapper->Update(); }
    }

    m_currentDomain.clear();
    m_currentUUid.clear();
    teardownPicking();

    if (m_selActor)  m_selActor->SetVisibility(false);
    if (m_edgeActor) m_edgeActor->SetVisibility(false);
    if (m_faceActor) m_faceActor->SetVisibility(false);

    if (m_visual) m_visual->render();
}

void PickManager::updateActorVisibility()
{
    if (!m_selActor || !m_edgeActor || !m_faceActor) return;

    m_selActor->SetVisibility(m_pickMode == PickMode::Node);
    m_edgeActor->SetVisibility(m_pickMode == PickMode::Edge);
    m_faceActor->SetVisibility(m_pickMode == PickMode::Face);
}

void PickManager::setupPicking()
{
    if (!m_visual) return;
    auto iren = m_visual->interactor();
    if (!iren) return;

    teardownPicking();
    m_leftPressTag = iren->AddObserver(vtkCommand::LeftButtonPressEvent, m_cbLeftPress);
}

void PickManager::teardownPicking()
{
    if (!m_visual) return;
    auto iren = m_visual->interactor();
    if (!iren) return;

    if (m_leftPressTag)
    {
        iren->RemoveObserver(m_leftPressTag);
        m_leftPressTag = 0;
    }
}

void PickManager::OnLeftButtonPress(vtkObject* caller, unsigned long, void* clientData, void*)
{
    auto* self = static_cast<PickManager*>(clientData);
    auto iren = vtkRenderWindowInteractor::SafeDownCast(caller);
    if (!self || !iren) return;

    int xy[2];
    iren->GetEventPosition(xy);
    self->pickAt(xy[0], xy[1]);
}

bool PickManager::pickAt(int x, int y)
{
    if (!m_visual || !m_grid || !m_cellPicker) return false;

    auto ren = m_visual->renderer();
    if (!ren) return false;

    if (!m_cellPicker->Pick(x, y, 0.0, ren)) return false;

    vtkIdType cellId = m_cellPicker->GetCellId();
    if (cellId < 0) return false;

    vtkCell* cell = m_grid->GetCell(cellId);
    if (!cell) return false;

    double pickPos[3];
    m_cellPicker->GetPickPosition(pickPos);

    // Face：2D/3D 都要选到“面”，并打印 cell + face 4点
    if (m_pickMode == PickMode::Face)
    {
        int faceLocalId = 15; // 2D 默认
        QVector<vtkIdType> facePids;

        if (!computePickedFace(cellId, pickPos, faceLocalId, facePids))
            return false;

        const vtkIdType faceKey = packFaceKey(cellId, faceLocalId);
        toggleSelectFace(faceKey);

        // ---- qDebug 输出：cell + faceLocalId + face points ----
        qDebug() << "[PickFace]"
                 << "cellId=" << cellId
                 << "faceLocalId=" << faceLocalId
                 << "nPts=" << facePids.size();

        for (int i = 0; i < facePids.size(); ++i)
        {
            vtkIdType pid = facePids[i];
            double p[3]; m_grid->GetPoint(pid, p);
            qDebug() << "  p" << i
                     << "vtkPid=" << pid
                     << "xyz=(" << p[0] << "," << p[1] << "," << p[2] << ")";
        }

        return true;
    }

    // Edge：仍用 cellId（如果你只有3D体单元，Edge真实语义需要你自己做“体->边”的推断）
    if (m_pickMode == PickMode::Edge)
    {
        toggleSelectEdge(cellId);
        return true;
    }

    // Node：取 cell 顶点里距离 pickPos 最近的点
    vtkIdList* ids = cell->GetPointIds();
    if (!ids || ids->GetNumberOfIds() <= 0) return false;

    vtkIdType bestPid = -1;
    double bestD2 = VTK_DOUBLE_MAX;

    for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
    {
        vtkIdType pid = ids->GetId(i);
        double p[3];
        m_grid->GetPoint(pid, p);
        double d2 = vtkMath::Distance2BetweenPoints(p, pickPos);
        if (d2 < bestD2) { bestD2 = d2; bestPid = pid; }
    }
    if (bestPid < 0) return false;

    toggleSelectNode(bestPid);
    return true;
}

// ===================== Face picking core =====================
// 2D: faceLocalId=15, facePids = cell point ids
// 3D: 在 cell 的所有 faces 中选出最符合 pickPos 的 face
bool PickManager::computePickedFace(vtkIdType cellId, const double pickPosIn[3],
                                    int& outFaceLocalId, QVector<vtkIdType>& outFacePids) const
{
    outFacePids.clear();
    if (!m_grid) return false;

    vtkCell* cell = m_grid->GetCell(cellId);
    if (!cell) return false;

    const int dim = cell->GetCellDimension();

    if (dim == 2)
    {
        vtkIdList* ids = cell->GetPointIds();
        if (!ids || ids->GetNumberOfIds() < 3) return false;

        outFaceLocalId = 15;
        outFacePids.reserve(ids->GetNumberOfIds());
        for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
            outFacePids.push_back(ids->GetId(i));
        return true;
    }

    if (dim != 3) return false;

    const int nFaces = cell->GetNumberOfFaces();
    if (nFaces <= 0) return false;

    // ✅ VTK API 需要 non-const double*
    double x[3] = { pickPosIn[0], pickPosIn[1], pickPosIn[2] };

    int bestFace = -1;
    double bestScore = VTK_DOUBLE_MAX;
    QVector<vtkIdType> bestPids;

    for (int fi = 0; fi < nFaces; ++fi)
    {
        vtkCell* face = cell->GetFace(fi);
        if (!face) continue;

        vtkIdList* fids = face->GetPointIds();
        if (!fids || fids->GetNumberOfIds() < 3) continue;

        const int npts = int(fids->GetNumberOfIds());

        QVector<double> pts; pts.resize(npts * 3);

        double bounds[6] = {
            VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX,
            VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX,
            VTK_DOUBLE_MAX, -VTK_DOUBLE_MAX
        };

        for (int i = 0; i < npts; ++i)
        {
            double p[3];
            m_grid->GetPoint(fids->GetId(i), p);

            pts[i*3+0] = p[0];
            pts[i*3+1] = p[1];
            pts[i*3+2] = p[2];

            bounds[0] = std::min(bounds[0], p[0]);
            bounds[1] = std::max(bounds[1], p[0]);
            bounds[2] = std::min(bounds[2], p[1]);
            bounds[3] = std::max(bounds[3], p[1]);
            bounds[4] = std::min(bounds[4], p[2]);
            bounds[5] = std::max(bounds[5], p[2]);
        }

        double n[3] = {0,0,0};
        vtkPolygon::ComputeNormal(npts, pts.data(), n);
        if (vtkMath::Norm(n) < 1e-12) continue;

        // p0 必须是 double[3]（non-const）
        double p0[3] = { pts[0], pts[1], pts[2] };

        // ✅ vtkPlane::DistanceToPlane 需要 double* 参数
        const double dist = std::abs(vtkPlane::DistanceToPlane(x, n, p0));

        // ✅ vtkPolygon::PointInPolygon 也需要 double* x
        const int inside = vtkPolygon::PointInPolygon(x, npts, pts.data(), bounds, n);

        // inside 优先：不在面内的，加个惩罚项
        const double score = inside ? dist : (dist + 1e3);

        if (score < bestScore)
        {
            bestScore = score;
            bestFace = fi;

            bestPids.clear();
            bestPids.reserve(npts);
            for (int i = 0; i < npts; ++i)
                bestPids.push_back(fids->GetId(i));
        }
    }

    if (bestFace < 0 || bestPids.isEmpty()) return false;

    outFaceLocalId = bestFace;
    outFacePids    = bestPids;
    return true;
}

// ===================== Node (Points) =====================

void PickManager::rebuildPointsForActive()
{
    if (!m_selPts) return;

    m_pid2SelIndex.clear();
    m_selIndex2Pid.clear();

    m_selPts->Reset();
    m_selPts->SetNumberOfPoints(0);

    const QSet<vtkIdType>& set = m_pointsByDomainUuid[m_currentDomain][m_currentUUid];
    for (vtkIdType pid : set)
        appendSelectedPoint(pid);

    m_selPts->Modified();
    if (m_selPoly) m_selPoly->Modified();
}

void PickManager::appendSelectedPoint(vtkIdType pid)
{
    if (!m_grid || !m_selPts) return;
    if (pid < 0 || pid >= m_grid->GetNumberOfPoints()) return;
    if (m_pid2SelIndex.contains(pid)) return;

    double p[3];
    m_grid->GetPoint(pid, p);

    const int idx = m_selIndex2Pid.size();
    m_selIndex2Pid.push_back(pid);
    m_pid2SelIndex.insert(pid, idx);

    m_selPts->InsertNextPoint(p);
}

void PickManager::removeSelectedPoint(vtkIdType pid)
{
    if (!m_selPts) return;

    auto it = m_pid2SelIndex.find(pid);
    if (it == m_pid2SelIndex.end()) return;

    const int idx = it.value();
    const int lastIdx = m_selIndex2Pid.size() - 1;
    const vtkIdType lastPid = m_selIndex2Pid[lastIdx];

    if (idx != lastIdx)
    {
        double lastP[3];
        m_selPts->GetPoint(lastIdx, lastP);
        m_selPts->SetPoint(idx, lastP);

        m_selIndex2Pid[idx] = lastPid;
        m_pid2SelIndex[lastPid] = idx;
    }

    m_selIndex2Pid.pop_back();
    m_pid2SelIndex.remove(pid);

    m_selPts->SetNumberOfPoints(m_selIndex2Pid.size());
}

void PickManager::toggleSelectNode(vtkIdType vtkPid)
{
    if (vtkPid < 0) return;
    if (!m_grid || !m_selPts) return;
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty()) return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    QHash<vtkIdType, QString>& owners = m_ownerPointByDomain[dom];
    QHash<QString, QSet<vtkIdType>>& uuid2set = m_pointsByDomainUuid[dom];
    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (mySet.contains(vtkPid))
    {
        mySet.remove(vtkPid);
        if (owners.value(vtkPid) == uid) owners.remove(vtkPid);
        removeSelectedPoint(vtkPid);
    }
    else
    {
        const QString oldUid = owners.value(vtkPid);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            auto itOld = uuid2set.find(oldUid);
            if (itOld != uuid2set.end())
                itOld.value().remove(vtkPid);
        }
        owners[vtkPid] = uid;
        mySet.insert(vtkPid);
        appendSelectedPoint(vtkPid);
    }

    m_selPts->Modified();
    if (m_selPoly) m_selPoly->Modified();
    if (m_visual) m_visual->render();
}

// ===================== Edge (ExtractSelection) =====================

void PickManager::ensureEdgePipelineReady()
{
    if (!m_grid || !m_edgeExtract || !m_edgeMapper) return;

    m_edgeExtract->SetInputData(0, m_grid);
    m_edgeExtract->SetInputData(1, m_edgeSel);

    m_edgeMapper->SetInputConnection(m_edgeExtract->GetOutputPort());
}

void PickManager::rebuildEdgesForActive()
{
    if (!m_edgeSelIds) return;

    m_edgeId2Idx.clear();
    m_edgeIdx2Id.clear();
    m_edgeSelIds->SetNumberOfTuples(0);

    const QSet<vtkIdType>& set = m_edgesByDomainUuid[m_currentDomain][m_currentUUid];
    for (vtkIdType cid : set)
        appendSelectedEdge(cid);

    m_edgeSelIds->Modified();
    if (m_edgeExtract) m_edgeExtract->Update();
}

void PickManager::appendSelectedEdge(vtkIdType cellId)
{
    if (cellId < 0) return;
    if (m_edgeId2Idx.contains(cellId)) return;

    const int idx = m_edgeIdx2Id.size();
    m_edgeIdx2Id.push_back(cellId);
    m_edgeId2Idx.insert(cellId, idx);

    m_edgeSelIds->InsertNextValue(cellId);
}

void PickManager::removeSelectedEdge(vtkIdType cellId)
{
    auto it = m_edgeId2Idx.find(cellId);
    if (it == m_edgeId2Idx.end()) return;

    const int idx = it.value();
    const int lastIdx = m_edgeIdx2Id.size() - 1;
    const vtkIdType lastId = m_edgeIdx2Id[lastIdx];

    if (idx != lastIdx)
    {
        m_edgeSelIds->SetValue(idx, lastId);
        m_edgeIdx2Id[idx] = lastId;
        m_edgeId2Idx[lastId] = idx;
    }

    m_edgeIdx2Id.pop_back();
    m_edgeId2Idx.remove(cellId);

    m_edgeSelIds->SetNumberOfTuples(m_edgeIdx2Id.size());
}

void PickManager::toggleSelectEdge(vtkIdType cellId)
{
    if (cellId < 0) return;
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty()) return;

    ensureEdgePipelineReady();

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    QHash<vtkIdType, QString>& owners = m_ownerEdgeByDomain[dom];
    QHash<QString, QSet<vtkIdType>>& uuid2set = m_edgesByDomainUuid[dom];
    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (mySet.contains(cellId))
    {
        mySet.remove(cellId);
        if (owners.value(cellId) == uid) owners.remove(cellId);
        removeSelectedEdge(cellId);
    }
    else
    {
        const QString oldUid = owners.value(cellId);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            auto itOld = uuid2set.find(oldUid);
            if (itOld != uuid2set.end())
                itOld.value().remove(cellId);
        }
        owners[cellId] = uid;
        mySet.insert(cellId);
        appendSelectedEdge(cellId);
    }

    m_edgeSelIds->Modified();
    if (m_edgeExtract) m_edgeExtract->Update();
    if (m_visual) m_visual->render();
}

// ===================== Face (PolyData) =====================

void PickManager::rebuildFacesForActive()
{
    rebuildFacePolyForActive();
    if (m_visual) m_visual->render();
}

void PickManager::rebuildFacePolyForActive()
{
    if (!m_grid || !m_facePts || !m_facePolys || !m_facePoly) return;

    m_facePts->Reset();
    m_facePts->SetNumberOfPoints(0);
    m_facePolys->Reset();

    // 去重：global vtkPid -> local point index
    QHash<vtkIdType, vtkIdType> pid2local;

    const QSet<vtkIdType>& set = m_facesByDomainUuid[m_currentDomain][m_currentUUid];

    for (vtkIdType faceKey : set)
    {
        const vtkIdType cellId = unpackFaceCellId(faceKey);
        const int faceLocalId  = unpackFaceLocalId(faceKey);

        vtkCell* cell = m_grid->GetCell(cellId);
        if (!cell) continue;

        QVector<vtkIdType> facePids;

        if (cell->GetCellDimension() == 2)
        {
            // 2D：cell 本身就是面
            vtkIdList* ids = cell->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3) continue;
            facePids.reserve(ids->GetNumberOfIds());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                facePids.push_back(ids->GetId(i));
        }
        else
        {
            // 3D：取指定 faceLocalId
            if (faceLocalId < 0 || faceLocalId >= cell->GetNumberOfFaces()) continue;
            vtkCell* face = cell->GetFace(faceLocalId);
            if (!face) continue;
            vtkIdList* ids = face->GetPointIds();
            if (!ids || ids->GetNumberOfIds() < 3) continue;
            facePids.reserve(ids->GetNumberOfIds());
            for (vtkIdType i = 0; i < ids->GetNumberOfIds(); ++i)
                facePids.push_back(ids->GetId(i));
        }

        // 组一个 polygon
        vtkSmartPointer<vtkIdList> polyIds = vtkSmartPointer<vtkIdList>::New();
        polyIds->SetNumberOfIds(facePids.size());

        for (int i = 0; i < facePids.size(); ++i)
        {
            const vtkIdType gpid = facePids[i];

            vtkIdType lpid;
            auto it = pid2local.find(gpid);
            if (it == pid2local.end())
            {
                double p[3]; m_grid->GetPoint(gpid, p);
                lpid = m_facePts->InsertNextPoint(p);
                pid2local.insert(gpid, lpid);
            }
            else
            {
                lpid = it.value();
            }

            polyIds->SetId(i, lpid);
        }

        m_facePolys->InsertNextCell(polyIds);
    }

    m_facePts->Modified();
    m_facePolys->Modified();
    m_facePoly->Modified();
    if (m_faceMapper) m_faceMapper->Update();
}

void PickManager::toggleSelectFace(vtkIdType faceKey)
{
    if (m_currentDomain.isEmpty() || m_currentUUid.isEmpty()) return;

    const QString dom = m_currentDomain;
    const QString uid = m_currentUUid;

    QHash<vtkIdType, QString>& owners = m_ownerFaceByDomain[dom];
    QHash<QString, QSet<vtkIdType>>& uuid2set = m_facesByDomainUuid[dom];
    QSet<vtkIdType>& mySet = uuid2set[uid];

    if (mySet.contains(faceKey))
    {
        mySet.remove(faceKey);
        if (owners.value(faceKey) == uid) owners.remove(faceKey);
    }
    else
    {
        const QString oldUid = owners.value(faceKey);
        if (!oldUid.isEmpty() && oldUid != uid)
        {
            auto itOld = uuid2set.find(oldUid);
            if (itOld != uuid2set.end())
                itOld.value().remove(faceKey);
        }
        owners[faceKey] = uid;
        mySet.insert(faceKey);
    }

    // 这里为了保证“只高亮面”，直接重建 poly（通常选面数量不大，足够快）
    rebuildFacesForActive();
}

// ===================== getIdList =====================

QVector<vtkIdType> PickManager::getIdList(const QString& domain, const QString& uuid, PickMode mode) const
{
    QVector<vtkIdType> out;

    if (mode == PickMode::Node)
    {
        const auto& set = m_pointsByDomainUuid.value(domain).value(uuid);
        out.reserve(set.size());
        for (auto v : set) out.push_back(v);
        return out;
    }
    if (mode == PickMode::Edge)
    {
        const auto& set = m_edgesByDomainUuid.value(domain).value(uuid);
        out.reserve(set.size());
        for (auto v : set) out.push_back(v);
        return out;
    }
    if (mode == PickMode::Face)
    {
        const auto& set = m_facesByDomainUuid.value(domain).value(uuid);
        out.reserve(set.size());
        for (auto v : set) out.push_back(v);
        return out;
    }
    return out;
}
