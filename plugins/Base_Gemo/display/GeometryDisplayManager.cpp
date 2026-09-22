#include "GeometryDisplayManager.h"

#include "CadDocument.h"
#include "CadBody.h"
#include "GeometryDisplayStyle.h"
#include "GeometryWorkbench.h"

#include <ServiceRegistry.h>
#include <services/ProjectManagerService.h>
#include <services/VisualWidgetService.h>

#include <vtkActor.h>
#include <vtkAppendPolyData.h>
#include <vtkDataArray.h>
#include <vtkDataSetAttributes.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkSmartPointer.h>

#include <QHash>

namespace
{
QHash<QString, bool>& visibilityStates()
{
    static QHash<QString, bool> states;
    return states;
}

bool& renderSuspendedState()
{
    static bool suspended = false;
    return suspended;
}

bool shouldRender(bool renderNow)
{
    return renderNow && !renderSuspendedState();
}

void attachObjectActors(const QString& uuid, GeomShapeObject* object,
                        VisualWidgetService* visual, bool resetCamera)
{
    if (uuid.isEmpty() || !object || !visual)
        return;

    const QString faceId = uuid + QStringLiteral("_face");
    const QString edgeId = uuid + QStringLiteral("_edge");
    visual->removeProp(faceId, false);
    visual->removeProp(edgeId, false);
    if (object->getSurfActor())
        visual->addProp(faceId, object->getSurfActor(), QStringLiteral("Geometry"),
                        faceId, resetCamera, false);
    if (object->getEdgeActor())
        visual->addProp(edgeId, object->getEdgeActor(), QStringLiteral("Geometry"),
                        edgeId, false, false);
}

bool usesEmbeddedTopologyEdges(GeomShapeObject* object)
{
    if (!object)
        return false;

    auto surfActor = object->getSurfActor();
    if (!surfActor || !surfActor->GetProperty() ||
        surfActor->GetProperty()->GetEdgeVisibility() == 0)
    {
        return false;
    }

    auto* mapper = vtkPolyDataMapper::SafeDownCast(surfActor->GetMapper());
    vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
    if (!poly || !poly->GetPointData())
        return false;

    auto* flags = poly->GetPointData()->GetAttribute(vtkDataSetAttributes::EDGEFLAG);
    return flags && flags->GetNumberOfComponents() == 1 &&
           flags->GetNumberOfTuples() == poly->GetNumberOfPoints();
}


// 大模型正常显示时，独立 Surface actor 仍保留：
// 1) 每个对象颜色/透明度互不影响；
// 2) 现有对象级拾取、树选择和属性更新不用改。
// 真正数量翻倍的是每个实体额外的一份黑色拓扑 Edge actor。所有普通 CAD Edge
// 样式相同，因此可以安全地合成一份显示 actor；原 edge actor 仍注册在场景管理器中，
// 只在普通显示层隐藏，现有 OCC/VTK 数据和 UUID 关系不丢。
constexpr int kEdgeBatchThreshold = 24;
const QString kBatchedEdgePropId = QStringLiteral("__geometry_cad_edge_batch");

bool& edgeBatchDirtyState()
{
    static bool dirty = true;
    return dirty;
}

bool& edgeBatchActiveState()
{
    static bool active = false;
    return active;
}

bool logicalVisible(const QString& uuid, GeomShapeObject* object)
{
    const auto& states = visibilityStates();
    const auto it = states.constFind(uuid);
    if (it != states.cend())
        return it.value();

    if (!object)
        return false;

    const bool surfVisible =
        object->getSurfActor() && object->getSurfActor()->GetVisibility() != 0;
    const bool edgeVisible =
        object->getEdgeActor() && object->getEdgeActor()->GetVisibility() != 0;
    return surfVisible || edgeVisible;
}

vtkPolyData* edgePolyData(GeomShapeObject* object)
{
    if (!object || !object->getSurfActor() || !object->getEdgeActor())
        return nullptr;

    // Surface + Edge 都存在才属于实体/曲面的普通拓扑边。
    // 纯 wire/草图没有 Surface actor，必须继续保留独立 edge actor。
    auto* mapper = vtkPolyDataMapper::SafeDownCast(object->getEdgeActor()->GetMapper());
    vtkPolyData* poly = mapper ? mapper->GetInput() : nullptr;
    return (poly && poly->GetNumberOfLines() > 0) ? poly : nullptr;
}

QString localEdgePropId(const QString& uuid)
{
    return uuid + QStringLiteral("_edge");
}

void setIndividualEdgeVisible(VisualWidgetService* vis,
                              const QString& uuid,
                              GeomShapeObject* object,
                              bool visible)
{
    if (!object || !object->getEdgeActor())
        return;

    const bool show = visible && !usesEmbeddedTopologyEdges(object);
    object->getEdgeActor()->SetVisibility(show ? 1 : 0);
    if (vis)
        vis->setVisible(localEdgePropId(uuid), show, false);
}

void rebuildEdgeBatch(bool renderNow)
{
    if (renderSuspendedState())
    {
        edgeBatchDirtyState() = true;
        return;
    }

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!vis || !mgr)
        return;

    const QHash<QString, GeomShapeObject*> shapeMap = mgr->getShapMap();

    int visibleBatchableCount = 0;
    for (auto it = shapeMap.cbegin(); it != shapeMap.cend(); ++it)
    {
        GeomShapeObject* object = it.value();
        if (logicalVisible(it.key(), object) && edgePolyData(object))
            ++visibleBatchableCount;
    }

    const bool useBatch = visibleBatchableCount >= kEdgeBatchThreshold;
    vis->removeProp(kBatchedEdgePropId, false);

    if (!useBatch)
    {
        // 小场景使用原来的独立 Edge actor，避免为了十几个对象额外做 append。
        for (auto it = shapeMap.cbegin(); it != shapeMap.cend(); ++it)
            setIndividualEdgeVisible(vis, it.key(), it.value(),
                                     logicalVisible(it.key(), it.value()));

        edgeBatchActiveState() = false;
        edgeBatchDirtyState() = false;
        if (renderNow)
            vis->render();
        return;
    }

    auto append = vtkSmartPointer<vtkAppendPolyData>::New();
    int inputCount = 0;

    for (auto it = shapeMap.cbegin(); it != shapeMap.cend(); ++it)
    {
        const QString& uuid = it.key();
        GeomShapeObject* object = it.value();
        const bool visible = logicalVisible(uuid, object);
        vtkPolyData* poly = visible ? edgePolyData(object) : nullptr;

        if (poly)
        {
            append->AddInputData(poly);
            ++inputCount;

            // 普通显示交给 batch；独立 actor 仍保留在 registry 中作为对象数据代理。
            object->getEdgeActor()->SetVisibility(0);
            vis->setVisible(localEdgePropId(uuid), false, false);
        }
        else
        {
            // 纯 wire / 草图等不能合批到实体拓扑边里。
            setIndividualEdgeVisible(vis, uuid, object, visible);
        }
    }

    if (inputCount > 0)
    {
        append->Update();

        auto combined = vtkSmartPointer<vtkPolyData>::New();
        combined->ShallowCopy(append->GetOutput());

        if (combined->GetNumberOfLines() > 0)
        {
            auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
            mapper->SetInputData(combined);
            mapper->ScalarVisibilityOff();
            mapper->StaticOn();
            GeometryDisplayStyle::configureCoincidentEdgeMapper(mapper);

            auto actor = vtkSmartPointer<vtkActor>::New();
            actor->SetMapper(mapper);
            actor->PickableOff(); // 对象/子形状拾取继续走现有专用拾取 actor。
            GeometryDisplayStyle::applyDefaultEdge(actor->GetProperty());

            vis->addProp(kBatchedEdgePropId,
                         actor,
                         QStringLiteral("Geometry"),
                         QStringLiteral("CAD topology edges (batched)"),
                         false,
                         false);
            edgeBatchActiveState() = true;
        }
        else
        {
            edgeBatchActiveState() = false;
        }
    }
    else
    {
        edgeBatchActiveState() = false;
    }

    edgeBatchDirtyState() = false;
    if (renderNow)
        vis->render();
}

void markEdgeBatchDirty(bool rebuildNow)
{
    edgeBatchDirtyState() = true;
    if (rebuildNow && !renderSuspendedState())
        rebuildEdgeBatch(false);
}
}

QString GeometryDisplayManager::facePropId(const QString& uuid)
{
    return uuid + QStringLiteral("_face");
}

QString GeometryDisplayManager::edgePropId(const QString& uuid)
{
    return uuid + QStringLiteral("_edge");
}

bool GeometryDisplayManager::registerObject(const QString& uuid, GeomShapeObject* object,
                                            bool resetCamera, bool renderNow)
{
    if (uuid.isEmpty() || !object)
        return false;

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(facePropId(uuid), false);
        vis->removeProp(edgePropId(uuid), false);
        if (object->getSurfActor())
            vis->addProp(facePropId(uuid), object->getSurfActor(), QStringLiteral("Geometry"),
                         facePropId(uuid), resetCamera, false);
        if (object->getEdgeActor())
            vis->addProp(edgePropId(uuid), object->getEdgeActor(), QStringLiteral("Geometry"),
                         edgePropId(uuid), false, false);
    }

    // Rebuilding/re-registering an actor must not silently make a previously
    // hidden history node visible again.  Keep visibility as logical state,
    // independent from the lifetime of the current VTK actors.
    const bool visible = visibilityStates().value(uuid, true);
    setVisible(uuid, visible, false);

    if (shouldRender(renderNow))
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
    return true;
}

bool GeometryDisplayManager::removeObject(const QString& uuid, bool removeFromDocument, bool renderNow)
{
    if (uuid.isEmpty())
        return false;

    bool changed = false;
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        changed = vis->removeProp(facePropId(uuid), false) || changed;
        changed = vis->removeProp(edgePropId(uuid), false) || changed;
    }

    if (removeFromDocument)
    {
        if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
            changed = mgr->removeGeomShapeObject(uuid) || changed;
    }

    if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
        pm->setNodeDimmed(uuid, true);

    markEdgeBatchDirty(!renderSuspendedState());

    if (shouldRender(renderNow))
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
    return changed;
}

bool GeometryDisplayManager::setVisible(const QString& uuid, bool visible, bool renderNow)
{
    if (uuid.isEmpty())
        return false;

    // Visibility belongs to the geometry history node, not to a particular
    // vtkActor instance.  Builders may destroy/recreate actors while keeping
    // the same UUID, so remember the requested state before touching actors.
    visibilityStates().insert(uuid, visible);

    bool changed = false;
    GeomShapeObject* object = nullptr;
    if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
        object = mgr->getGeomShapeObject(uuid);

    // Hidden project-history nodes intentionally restore only their OCC Shape.
    // Showing such a node is the explicit lifecycle transition that creates its
    // one VTK presentation and registers it. This is not a conversion retry: the
    // object has never had a presentation before this transition.
    if (visible && object && !object->hasActors())
    {
        if (object->materializePresentation())
        {
            if (auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>())
                attachObjectActors(uuid, object, visual, false);
            changed = true;
        }
    }

    if (object)
    {
        const bool embeddedTopologyEdges = usesEmbeddedTopologyEdges(object);
        if (object->getSurfActor())
        {
            object->getSurfActor()->SetVisibility(visible ? 1 : 0);
            changed = true;
        }
        if (object->getEdgeActor())
        {
            // Edge batch only replaces topology edges of objects that also have a
            // Surface actor.  Pure wire objects (especially sketches) must always
            // keep their own Edge actor; otherwise a sketch consumed by Extrude
            // cannot be shown again while the large-scene edge batch is active.
            const bool batchableEdge = edgePolyData(object) != nullptr;
            const bool showIndividualEdge =
                visible && !embeddedTopologyEdges &&
                (!edgeBatchActiveState() || !batchableEdge);
            object->getEdgeActor()->SetVisibility(showIndividualEdge ? 1 : 0);
            changed = true;
        }
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        const bool embeddedTopologyEdges = usesEmbeddedTopologyEdges(object);

        changed = vis->setVisible(facePropId(uuid), visible, false) || changed;
        const bool batchableEdge = edgePolyData(object) != nullptr;
        const bool showIndividualEdge =
            visible && !embeddedTopologyEdges &&
            (!edgeBatchActiveState() || !batchableEdge);
        changed = vis->setVisible(edgePropId(uuid), showIndividualEdge, false) || changed;

        // 可见性改变会改变 batch 内容。单对象显隐立即重建但不额外 Render；
        // 批量显隐时 Workbench 已设置 renderSuspended，只标 dirty，结束时统一重建。
        markEdgeBatchDirty(!renderSuspendedState());

        if (shouldRender(renderNow))
            vis->render();
    }

    // renderNow=false 是 GeometryWorkbench 的局部/批量路径；节点的 RoleDimmed、
    // checkState 已由调用方直接写回。不要再通过 ProjectManager::findNodeByUuid()
    // 从工程根节点递归查一次，否则批量显隐会退化成 O(M*N)。
    if (shouldRender(renderNow))
    {
        if (auto* pm = ServiceRegistry::instance().getService<ProjectManagerService>())
            pm->setNodeDimmed(uuid, !visible);
    }

    // renderNow=false 表示调用方正在做局部 UI 更新或批处理。此时模型树节点状态
    // 已由 GeometryWorkbench 直接写回，不应再 O(N) 地 refreshTreePresentation()。
    // 只有显式要求立即完成整套显示更新的调用才触发全树同步。
    if (renderNow && !renderSuspendedState())
    {
        if (auto* pre = ServiceRegistry::instance().getService<PreProcessingService>())
        {
            if (auto* workbench = dynamic_cast<GeometryWorkbench*>(pre))
                workbench->refreshTreePresentation();
        }
    }

    return changed;
}

bool GeometryDisplayManager::hide(const QString& uuid, bool renderNow)
{
    return setVisible(uuid, false, renderNow);
}

bool GeometryDisplayManager::show(const QString& uuid, bool renderNow)
{
    return setVisible(uuid, true, renderNow);
}

bool GeometryDisplayManager::isVisible(const QString& uuid)
{
    if (uuid.isEmpty())
        return false;

    const auto& states = visibilityStates();
    const auto stateIt = states.constFind(uuid);
    if (stateIt != states.cend())
        return stateIt.value();

    if (auto* mgr = GeomManager::GeomManagerInstance::getInstance())
    {
        if (auto* object = mgr->getGeomShapeObject(uuid))
        {
            const bool surfVisible = object->getSurfActor() &&
                                     object->getSurfActor()->GetVisibility() != 0;
            const bool edgeVisible = object->getEdgeActor() &&
                                     object->getEdgeActor()->GetVisibility() != 0;
            return surfVisible || edgeVisible;
        }
    }

    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        auto* surf = vtkActor::SafeDownCast(vis->getProp(facePropId(uuid)));
        auto* edge = vtkActor::SafeDownCast(vis->getProp(edgePropId(uuid)));
        return (surf && surf->GetVisibility() != 0) || (edge && edge->GetVisibility() != 0);
    }

    return false;
}

void GeometryDisplayManager::hideSources(const QStringList& uuids, bool renderNow)
{
    for (int i = 0; i < uuids.size(); ++i)
        hide(uuids.at(i), renderNow && i == uuids.size() - 1);
}

void GeometryDisplayManager::forgetVisibilityState(const QString& uuid)
{
    if (!uuid.isEmpty())
        visibilityStates().remove(uuid);
}

void GeometryDisplayManager::clearVisibilityStates()
{
    visibilityStates().clear();
    edgeBatchDirtyState() = true;
    edgeBatchActiveState() = false;
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
        vis->removeProp(kBatchedEdgePropId, false);
}

void GeometryDisplayManager::setRenderSuspended(bool suspended)
{
    const bool wasSuspended = renderSuspendedState();
    renderSuspendedState() = suspended;

    // STEP 同步、工程恢复、批量显示/隐藏都在 suspended 区间内可能改几十/几百个对象。
    // 不要每个对象 append 一次；只在批处理结束时构建一次合批边线。
    if (wasSuspended && !suspended && edgeBatchDirtyState())
        rebuildEdgeBatch(false);
}

bool GeometryDisplayManager::isRenderSuspended()
{
    return renderSuspendedState();
}
