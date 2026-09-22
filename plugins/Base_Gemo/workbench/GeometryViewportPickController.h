#pragma once

// Geometry viewport interaction gate.
//
// CAD selection and camera navigation share the left mouse in COMSOL style.
// VisualizationWidget owns the gesture state machine: a stationary left click is
// selection, while a left drag that exceeds the platform drag distance becomes
// camera rotation.  This controller only confirms a feature pick on release when
// the interactor style is still idle; it never performs its own pixel-threshold test.
//
// This controller owns one global geometry-pick session at a time:
//   * one stationary left click performs CAD picking;
//   * only left click vs left drag is specialized here; other navigation stays VTK-native;
//   * object candidates are prepared once when picking starts, not rescanned on
//     every click;
//   * Face picking uses vtkCellPicker on the existing CAD surface presentation;
//   * Edge picking uses a cached vtkCellLocator to collect only nearby candidates,
//     then ranks that small candidate set in screen pixels and reads OCC_EdgeId;
//   * preselection uses the same path, so hover feedback matches the eventual click.
//
// The public API intentionally stays compatible with v47 so feature panels do
// not need another round of changes.

#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <GeometryDisplayManager.h>
#include <GeometryPickManager.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <QHash>
#include <QPointer>
#include <QPair>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStringList>
#include <QVector>

#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCell.h>
#include <vtkCellData.h>
#include <vtkCellLocator.h>
#include <vtkCellPicker.h>
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkDataArray.h>
#include <vtkIdList.h>
#include <vtkInteractorStyle.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkWeakPointer.h>


#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>

namespace GeometryViewportPickController
{
using ObjectFilter = std::function<bool(const QString&, GeomShapeObject*)>;
using ObjectPicked = std::function<void(const QString&)>;
using TopologyPicked = std::function<void(int)>;
using TopologyPreviewed = std::function<void(int)>;
using TopologyPreviewCleared = std::function<void()>;
using TopologyObjectPicked = std::function<void(const QString&, int)>;
using TopologyObjectPreviewed = std::function<void(const QString&, int)>;
using SourceUuidProvider = std::function<QString()>;

namespace Detail
{
enum class SessionKind
{
    None,
    Object,
    ExternalActors,
    Topology
};

struct Session
{
    SessionKind kind = SessionKind::None;

    // ownerKey is retained only for identity comparison in QObject::destroyed.
    // ownerGuard is the dereference-safe lifetime guard used by event dispatch.
    QObject* ownerKey = nullptr;
    QPointer<QObject> ownerGuard;
    QPointer<QPushButton> button;
    QString idleText;
    QString activeText;

    QString selfUuid;
    QStringList excludedUuids;
    ObjectFilter objectFilter;
    ObjectPicked objectPicked;

    struct ObjectPreviewStyle
    {
        vtkWeakPointer<vtkActor> actor;
        bool valid = false;
        double color[3] = {0.0, 0.0, 0.0};
        double opacity = 1.0;
        double lineWidth = 1.0;
        int representation = VTK_SURFACE;
        int renderLinesAsTubes = 0;
        int renderPointsAsSpheres = 0;
    };
    QVector<ObjectPreviewStyle> objectPreviewStyles;
    QString objectPreviewUuid;

    GeometryPickMode topologyMode = GeometryPickMode::Edge;
    SourceUuidProvider sourceUuidProvider;
    TopologyPicked topologyPicked;
    TopologyPreviewed topologyPreviewed;
    TopologyPreviewCleared topologyPreviewCleared;

    // Scene-wide CAD selection.  sourceUuidProvider is interpreted as an optional
    // lock: empty => search the visible CAD scene; non-empty => keep selecting
    // sub-elements from that one source (e.g. after the first fillet edge).
    bool sceneWideTopology = false;
    ObjectFilter topologyObjectFilter;
    TopologyObjectPicked topologyObjectPicked;
    TopologyObjectPreviewed topologyObjectPreviewed;

    vtkSmartPointer<vtkCellPicker> objectPicker;
    QHash<vtkActor*, QString> objectUuidByActor;
    // Mesh-only Region picking reuses the same camera-safe object-pick session,
    // but the actors are temporary preview props rather than GeomManager objects.
    QHash<QString, vtkWeakPointer<vtkActor>> externalActorByKey;

    // Front-most visible surfaces are prepared once at session start.  Scene-wide
    // picking samples this picker only around the mouse and lazily builds edge
    // acceleration structures for the few objects actually touched by the cursor.
    vtkSmartPointer<vtkCellPicker> sceneSurfacePicker;
    QHash<vtkActor*, QString> sceneSurfaceUuidByActor;

    struct EdgeLocatorEntry
    {
        vtkWeakPointer<vtkPolyData> poly;
        vtkSmartPointer<vtkCellLocator> locator;
    };
    QHash<QString, EdgeLocatorEntry> edgeLocatorCache;
    QStringList edgeLocatorLru;

    // The feature filter can be more expensive than a hash lookup.  Cache it for
    // the life of the pick session; geometry is not rebuilt while a CAD pick tool
    // owns the left-button interaction.
    QHash<QString, bool> topologyAllowedCache;

    QString previewUuid;
    int previewIndex = 0;
    int lastHoverX = std::numeric_limits<int>::min();
    int lastHoverY = std::numeric_limits<int>::min();
    std::chrono::steady_clock::time_point lastHoverTime{};

    bool leftDown = false;
    // Set only when the installed InteractorStyle has actually entered a camera
    // interaction during this left gesture.  This is the single source of truth
    // for click-vs-drag; the picker no longer applies its own pixel threshold.
    bool leftCameraDrag = false;
    int pressX = 0;
    int pressY = 0;
};

struct ControllerState
{
    vtkWeakPointer<vtkRenderWindowInteractor> interactor;
    vtkSmartPointer<vtkCallbackCommand> callback;
    unsigned long pressTag = 0;
    unsigned long releaseTag = 0;
    unsigned long moveTag = 0;
    bool lastPickAppendModifier = false;
    Session session;
};

inline ControllerState& controller()
{
    static ControllerState state;
    return state;
}

inline vtkPolyData* actorPolyData(vtkActor* actor)
{
    auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
    return mapper ? mapper->GetInput() : nullptr;
}

inline int cellIdentity(vtkPolyData* poly,
                        vtkIdType cellId,
                        const char* arrayName)
{
    if (!poly || cellId < 0 || !poly->GetCellData())
        return 0;

    vtkDataArray* ids = poly->GetCellData()->GetArray(arrayName);
    if (!ids || cellId >= ids->GetNumberOfTuples())
        return 0;

    return static_cast<int>(std::llround(ids->GetTuple1(cellId)));
}

inline void resetButton(const Session& session)
{
    Q_UNUSED(session);
    // 选择按钮只负责发起动作，不再承担“当前处于选择模式”的状态显示。
}


inline void restoreObjectPreviewStyle(Session& session, bool renderNow)
{
    if (session.objectPreviewStyles.isEmpty() && session.objectPreviewUuid.isEmpty())
        return;

    for (const Session::ObjectPreviewStyle& snapshot : session.objectPreviewStyles)
    {
        vtkActor* actor = snapshot.actor.GetPointer();
        if (!actor || !snapshot.valid || !actor->GetProperty())
            continue;

        vtkProperty* property = actor->GetProperty();
        property->SetColor(snapshot.color[0], snapshot.color[1], snapshot.color[2]);
        property->SetOpacity(snapshot.opacity);
        property->SetLineWidth(snapshot.lineWidth);
        property->SetRepresentation(snapshot.representation);
        property->SetRenderLinesAsTubes(snapshot.renderLinesAsTubes);
        property->SetRenderPointsAsSpheres(snapshot.renderPointsAsSpheres);
    }

    session.objectPreviewStyles.clear();
    session.objectPreviewUuid.clear();

    if (renderNow)
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
}

inline void captureAndApplyObjectPreview(Session& session,
                                         vtkActor* actor,
                                         bool curveStyle)
{
    if (!actor || !actor->GetProperty() || actor->GetVisibility() == 0)
        return;

    vtkProperty* property = actor->GetProperty();

    Session::ObjectPreviewStyle snapshot;
    snapshot.actor = actor;
    snapshot.valid = true;
    property->GetColor(snapshot.color);
    snapshot.opacity = property->GetOpacity();
    snapshot.lineWidth = property->GetLineWidth();
    snapshot.representation = property->GetRepresentation();
    snapshot.renderLinesAsTubes = property->GetRenderLinesAsTubes();
    snapshot.renderPointsAsSpheres = property->GetRenderPointsAsSpheres();
    session.objectPreviewStyles.push_back(snapshot);

    // Object preselection is intentionally presentation-only.  Do not create an
    // separate OCC overlay just to hover a source candidate in a large STEP assembly.
    property->SetColor(1.0, 0.74, 0.18);
    if (curveStyle)
    {
        property->SetRepresentationToWireframe();
        property->SetLineWidth(std::max(4.0f, property->GetLineWidth()));
        property->SetRenderLinesAsTubes(0);
    }
}

inline void applyObjectPreviewStyle(Session& session,
                                    const QString& uuid,
                                    bool renderNow)
{
    if (session.objectPreviewUuid == uuid)
        return;

    restoreObjectPreviewStyle(session, false);
    if (uuid.isEmpty())
    {
        if (renderNow)
        {
            if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
                vis->render();
        }
        return;
    }

    if (session.kind == SessionKind::ExternalActors)
    {
        vtkActor* actor = session.externalActorByKey.value(uuid).GetPointer();
        if (actor && actor->GetVisibility() != 0)
        {
            vtkPolyData* poly = actorPolyData(actor);
            const bool curveStyle = poly && poly->GetNumberOfLines() > 0 && poly->GetNumberOfPolys() == 0;
            captureAndApplyObjectPreview(session, actor, curveStyle);
            // Partition candidates stay quiet until hover.  The item under the cursor is
            // deliberately one step stronger than the persistent selected style.
            // Tube rendering remains disabled so circular edges do not become pipes.
            if (auto* property = actor->GetProperty())
            {
                property->SetOpacity(curveStyle ? 1.0 : 0.38);
                if (curveStyle)
                {
                    property->SetLineWidth(std::max(4.0f, property->GetLineWidth()));
                    property->SetRenderLinesAsTubes(0);
                }
            }
        }
        session.objectPreviewUuid = uuid;
        if (renderNow)
        {
            if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
                vis->render();
        }
        return;
    }

    if (!GeometryDisplayManager::isVisible(uuid))
        return;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj)
        return;

    // Prefer the visible surface actor.  A batched CAD edge actor may intentionally
    // hide the per-object edge proxy, so touching only the surface also avoids
    // fighting the stable CAD Edge Pass.
    vtkActor* surfaceActor = obj->getSurfActor();
    vtkPolyData* surfacePoly = actorPolyData(surfaceActor);
    if (surfaceActor && surfacePoly && surfacePoly->GetNumberOfPolys() > 0 &&
        surfaceActor->GetVisibility() != 0)
    {
        captureAndApplyObjectPreview(session, surfaceActor, false);
    }
    else
    {
        vtkActor* edgeActor = obj->getEdgeActor();
        vtkPolyData* edgePoly = actorPolyData(edgeActor);
        if (edgeActor && edgePoly && edgePoly->GetNumberOfCells() > 0)
            captureAndApplyObjectPreview(session, edgeActor, true);
    }

    session.objectPreviewUuid = uuid;
    if (renderNow)
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
}

inline void clearSession(bool resetCurrentButton)
{
    ControllerState& state = controller();
    Session old = std::move(state.session);
    state.session = Session{};

    // QObject::destroyed clears QPointer before the destroyed hook is invoked.
    // Never call a panel-capturing preview lambda after its owner has started
    // destruction; normal stop/beforeUnload still clears preview while the owner
    // is alive.
    if (old.ownerGuard && old.topologyPreviewCleared)
        old.topologyPreviewCleared();

    // Restore presentation-only object hover even when a one-shot object pick
    // finishes successfully.  This preserves the feature's normal source color.
    restoreObjectPreviewStyle(old, false);

    if (resetCurrentButton)
        resetButton(old);
}

inline void stop(QObject* owner, bool resetCurrentButton)
{
    if (!owner)
        return;

    ControllerState& state = controller();
    if (state.session.kind == SessionKind::None || state.session.ownerKey != owner)
        return;

    clearSession(resetCurrentButton);
}

inline bool prepareObjectPicker(Session& session)
{
    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = vis ? vis->renderer() : nullptr;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!renderer || !mgr)
        return false;

    session.objectPicker = vtkSmartPointer<vtkCellPicker>::New();
    session.objectPicker->SetTolerance(0.004);
    session.objectPicker->PickFromListOn();
    session.objectUuidByActor.clear();

    // Build once at mode activation.  The old implementation repeated this
    // complete GeomManager scan on every viewport click.
    const auto map = mgr->getShapMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        const QString uuid = it.key();
        GeomShapeObject* obj = it.value();
        if (uuid.isEmpty() || uuid == session.selfUuid ||
            session.excludedUuids.contains(uuid) || !obj || !obj->getShape() ||
            obj->getShape()->IsNull() || !GeometryDisplayManager::isVisible(uuid))
        {
            continue;
        }

        // Filter before adding to vtkCellPicker.  Otherwise a visually front-most
        // invalid object consumes the pick and the valid profile/body behind it can
        // never be selected.
        if (session.objectFilter && !session.objectFilter(uuid, obj))
            continue;

        vtkActor* actor = obj->getSurfActor();
        vtkPolyData* poly = actorPolyData(actor);
        if (!actor || !poly || poly->GetNumberOfPolys() <= 0 ||
            actor->GetVisibility() == 0 || actor->GetPickable() == 0)
        {
            actor = obj->getEdgeActor();
            poly = actorPolyData(actor);
        }

        if (!actor || !poly || poly->GetNumberOfCells() <= 0 ||
            actor->GetVisibility() == 0 || actor->GetPickable() == 0)
        {
            continue;
        }

        session.objectPicker->AddPickList(actor);
        session.objectUuidByActor.insert(actor, uuid);
    }

    return !session.objectUuidByActor.isEmpty();
}

inline vtkSmartPointer<vtkCellPicker> buildSceneSurfacePicker(
    QHash<vtkActor*, QString>* actorUuidMap = nullptr)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
        return nullptr;

    auto picker = vtkSmartPointer<vtkCellPicker>::New();
    picker->SetTolerance(0.0005);
    picker->PickFromListOn();
    if (actorUuidMap)
        actorUuidMap->clear();

    int added = 0;
    const auto map = mgr->getShapMap();
    for (auto it = map.cbegin(); it != map.cend(); ++it)
    {
        const QString& uuid = it.key();
        GeomShapeObject* obj = it.value();
        if (uuid.isEmpty() || !obj || !GeometryDisplayManager::isVisible(uuid))
            continue;

        vtkActor* actor = obj->getSurfActor();
        vtkPolyData* poly = actorPolyData(actor);
        if (!actor || !poly || poly->GetNumberOfPolys() <= 0 ||
            actor->GetVisibility() == 0 || actor->GetPickable() == 0)
        {
            continue;
        }

        picker->AddPickList(actor);
        if (actorUuidMap)
            actorUuidMap->insert(actor, uuid);
        ++added;
    }

    return added > 0 ? picker : vtkSmartPointer<vtkCellPicker>();
}

inline bool worldToDisplay(vtkRenderer* renderer, const double world[3], double display[3])
{
    if (!renderer || !world || !display)
        return false;

    renderer->SetWorldPoint(world[0], world[1], world[2], 1.0);
    renderer->WorldToDisplay();
    renderer->GetDisplayPoint(display);
    return std::isfinite(display[0]) && std::isfinite(display[1]) &&
           std::isfinite(display[2]);
}

inline bool displayToWorld(vtkRenderer* renderer,
                           double x,
                           double y,
                           double z,
                           double world[3])
{
    if (!renderer || !world)
        return false;

    renderer->SetDisplayPoint(x, y, z);
    renderer->DisplayToWorld();
    double homogeneous[4] = {0.0, 0.0, 0.0, 1.0};
    renderer->GetWorldPoint(homogeneous);
    if (std::abs(homogeneous[3]) <= 1.0e-20)
        return false;

    world[0] = homogeneous[0] / homogeneous[3];
    world[1] = homogeneous[1] / homogeneous[3];
    world[2] = homogeneous[2] / homogeneous[3];
    return std::isfinite(world[0]) && std::isfinite(world[1]) && std::isfinite(world[2]);
}

inline double distance3(const double a[3], const double b[3])
{
    const double dx = a[0] - b[0];
    const double dy = a[1] - b[1];
    const double dz = a[2] - b[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

inline double normalizedPickTolerance(vtkRenderer* renderer, double radiusPixels)
{
    vtkRenderWindow* window = renderer ? renderer->GetRenderWindow() : nullptr;
    int* size = window ? window->GetSize() : nullptr;
    if (!size || size[0] <= 0 || size[1] <= 0)
        return 0.006;

    const double diagonal = std::sqrt(static_cast<double>(size[0]) * size[0] +
                                      static_cast<double>(size[1]) * size[1]);
    return diagonal > 0.0 ? radiusPixels / diagonal : 0.006;
}

inline bool transformPoint(vtkMatrix4x4* matrix, const double in[3], double out[3])
{
    if (!matrix || !in || !out)
        return false;
    const double input[4] = {in[0], in[1], in[2], 1.0};
    double result[4] = {0.0, 0.0, 0.0, 1.0};
    matrix->MultiplyPoint(input, result);
    if (std::abs(result[3]) <= 1.0e-20)
        return false;
    out[0] = result[0] / result[3];
    out[1] = result[1] / result[3];
    out[2] = result[2] / result[3];
    return true;
}

inline double pointSegmentDistance2D(double px,
                                     double py,
                                     const double a[3],
                                     const double b[3],
                                     double& t)
{
    const double vx = b[0] - a[0];
    const double vy = b[1] - a[1];
    const double len2 = vx * vx + vy * vy;
    if (len2 <= 1.0e-18)
    {
        t = 0.0;
        const double dx = px - a[0];
        const double dy = py - a[1];
        return dx * dx + dy * dy;
    }

    t = ((px - a[0]) * vx + (py - a[1]) * vy) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const double qx = a[0] + t * vx;
    const double qy = a[1] + t * vy;
    const double dx = px - qx;
    const double dy = py - qy;
    return dx * dx + dy * dy;
}

inline vtkCellLocator* ensureEdgeLocator(Session& session,
                                            const QString& sourceUuid,
                                            vtkPolyData* poly)
{
    if (sourceUuid.isEmpty() || !poly || poly->GetNumberOfLines() <= 0)
        return nullptr;

    auto existing = session.edgeLocatorCache.find(sourceUuid);
    if (existing != session.edgeLocatorCache.end() &&
        existing->poly.GetPointer() == poly &&
        existing->locator)
    {
        session.edgeLocatorLru.removeAll(sourceUuid);
        session.edgeLocatorLru.push_back(sourceUuid);
        return existing->locator.GetPointer();
    }

    // Presentation may have been rebuilt under the same UUID.
    if (existing != session.edgeLocatorCache.end())
        session.edgeLocatorCache.erase(existing);
    session.edgeLocatorLru.removeAll(sourceUuid);

    // Keep memory bounded while the cursor moves across a large assembly.
    // Locators are built lazily; we never prebuild one for every STEP part.
    constexpr int kMaxCachedEdgeLocators = 8;
    while (session.edgeLocatorLru.size() >= kMaxCachedEdgeLocators)
    {
        const QString evicted = session.edgeLocatorLru.takeFirst();
        session.edgeLocatorCache.remove(evicted);
    }

    Session::EdgeLocatorEntry entry;
    entry.poly = poly;
    entry.locator = vtkSmartPointer<vtkCellLocator>::New();
    entry.locator->SetDataSet(poly);
    entry.locator->BuildLocator();

    auto inserted = session.edgeLocatorCache.insert(sourceUuid, entry);
    session.edgeLocatorLru.push_back(sourceUuid);
    return inserted->locator.GetPointer();
}


struct EdgeCandidate
{
    vtkIdType cellId = -1;
    int edgeIndex = 0;
    double screenDistance2 = std::numeric_limits<double>::max();
    double displayDepth = 1.0;
    double world[3] = {0.0, 0.0, 0.0};
    double display[3] = {0.0, 0.0, 1.0};
};

inline bool collectEdgeCandidates(Session& session,
                                  const QString& sourceUuid,
                                  vtkActor* actor,
                                  vtkPolyData* poly,
                                  vtkRenderer* renderer,
                                  int x,
                                  int y,
                                  double radiusPixels,
                                  QVector<EdgeCandidate>& candidates)
{
    candidates.clear();
    if (!actor || !poly || !renderer)
        return false;

    vtkCellLocator* edgeLocator = ensureEdgeLocator(session, sourceUuid, poly);
    if (!edgeLocator)
        return false;

    double worldNear[3] = {0.0, 0.0, 0.0};
    double worldFar[3] = {0.0, 0.0, 0.0};
    if (!displayToWorld(renderer, x, y, 0.0, worldNear) ||
        !displayToWorld(renderer, x, y, 1.0, worldFar))
    {
        return false;
    }

    // Depth lookup must stay O(1).  vtkRenderer::GetZ reads the already-rendered
    // depth buffer; the previous code ran a scene-wide vtkCellPicker here for every
    // hover query, which becomes very expensive on large STEP assemblies.
    double depth = renderer->GetZ(x, y);
    if (!std::isfinite(depth) || depth <= 0.0 || depth >= 1.0)
    {
        depth = 0.5;
        if (vtkCamera* camera = renderer->GetActiveCamera())
        {
            double focal[3] = {0.0, 0.0, 0.0};
            double focalDisplay[3] = {0.0, 0.0, 0.5};
            camera->GetFocalPoint(focal);
            if (worldToDisplay(renderer, focal, focalDisplay))
                depth = focalDisplay[2];
        }
    }

    double worldCenter[3] = {0.0, 0.0, 0.0};
    double worldOffset[3] = {0.0, 0.0, 0.0};
    if (!displayToWorld(renderer, x, y, depth, worldCenter) ||
        !displayToWorld(renderer, x + radiusPixels, y, depth, worldOffset))
    {
        return false;
    }

    vtkNew<vtkMatrix4x4> inverse;
    vtkMatrix4x4::Invert(actor->GetMatrix(), inverse.GetPointer());
    vtkMatrix4x4* actorMatrix = actor->GetMatrix();

    double mapperNear[3], mapperFar[3], mapperCenter[3], mapperOffset[3];
    if (!transformPoint(inverse, worldNear, mapperNear) ||
        !transformPoint(inverse, worldFar, mapperFar) ||
        !transformPoint(inverse, worldCenter, mapperCenter) ||
        !transformPoint(inverse, worldOffset, mapperOffset))
    {
        return false;
    }

    // Locator tolerance is deliberately wider than the final screen-space radius.
    // Perspective changes world-units-per-pixel along the ray; a tight world-space
    // locator tolerance can therefore miss a perfectly valid short/silhouette edge.
    // False positives are harmless because every returned segment is still filtered
    // by the exact pixel radius below.
    const double mapperTolerance = std::max(1.0e-9, distance3(mapperCenter, mapperOffset) * 3.0);
    vtkNew<vtkIdList> cells;
    edgeLocator->FindCellsAlongLine(mapperNear, mapperFar, mapperTolerance, cells);
    if (cells->GetNumberOfIds() <= 0)
        return false;

    QHash<int, EdgeCandidate> bestByEdge;
    const double radius2 = radiusPixels * radiusPixels;
    for (vtkIdType n = 0; n < cells->GetNumberOfIds(); ++n)
    {
        const vtkIdType cellId = cells->GetId(n);
        const int edgeIndex = cellIdentity(poly, cellId, "OCC_EdgeId");
        vtkCell* cell = poly->GetCell(cellId);
        if (edgeIndex <= 0 || !cell || cell->GetNumberOfPoints() < 2)
            continue;

        EdgeCandidate best;
        best.cellId = cellId;
        best.edgeIndex = edgeIndex;

        for (vtkIdType i = 0; i + 1 < cell->GetNumberOfPoints(); ++i)
        {
            double mapperA[3] = {0.0, 0.0, 0.0};
            double mapperB[3] = {0.0, 0.0, 0.0};
            poly->GetPoint(cell->GetPointId(i), mapperA);
            poly->GetPoint(cell->GetPointId(i + 1), mapperB);

            double worldA[3], worldB[3], displayA[3], displayB[3];
            if (!transformPoint(actorMatrix, mapperA, worldA) ||
                !transformPoint(actorMatrix, mapperB, worldB) ||
                !worldToDisplay(renderer, worldA, displayA) ||
                !worldToDisplay(renderer, worldB, displayB))
            {
                continue;
            }

            double t = 0.0;
            const double d2 = pointSegmentDistance2D(x, y, displayA, displayB, t);
            if (d2 > radius2 || d2 >= best.screenDistance2)
                continue;

            best.screenDistance2 = d2;
            for (int c = 0; c < 3; ++c)
                best.world[c] = worldA[c] + t * (worldB[c] - worldA[c]);
            if (!worldToDisplay(renderer, best.world, best.display))
                continue;
            best.displayDepth = best.display[2];
        }

        if (best.screenDistance2 > radius2)
            continue;

        auto it = bestByEdge.find(edgeIndex);
        if (it == bestByEdge.end() || best.screenDistance2 < it->screenDistance2 ||
            (std::abs(best.screenDistance2 - it->screenDistance2) < 0.01 &&
             best.displayDepth < it->displayDepth))
        {
            bestByEdge.insert(edgeIndex, best);
        }
    }

    candidates.reserve(bestByEdge.size());
    for (auto it = bestByEdge.cbegin(); it != bestByEdge.cend(); ++it)
        candidates.push_back(it.value());

    std::sort(candidates.begin(), candidates.end(), [](const EdgeCandidate& a, const EdgeCandidate& b)
    {
        if (std::abs(a.screenDistance2 - b.screenDistance2) > 0.01)
            return a.screenDistance2 < b.screenDistance2;
        return a.displayDepth < b.displayDepth;
    });
    return !candidates.isEmpty();
}

inline bool candidateVisible(Session& session,
                             const QString& sourceUuid,
                             GeometryPickMode mode,
                             int topologyIndex,
                             vtkActor* candidateActor,
                             vtkRenderer* renderer,
                             const double candidateWorld[3],
                             const double candidateDisplay[3],
                             double radiusPixels)
{
    Q_UNUSED(session);
    Q_UNUSED(sourceUuid);
    Q_UNUSED(mode);
    Q_UNUSED(topologyIndex);
    Q_UNUSED(candidateActor);

    if (!renderer || !candidateWorld || !candidateDisplay)
        return true;

    const int px = static_cast<int>(std::llround(candidateDisplay[0]));
    const int py = static_cast<int>(std::llround(candidateDisplay[1]));
    if (!renderer->IsInViewport(px, py))
        return false;

    const double frontZ = renderer->GetZ(px, py);
    if (!std::isfinite(frontZ) || frontZ >= 1.0)
        return true; // silhouette / background

    // Same OCC display tessellation means a truly visible topology edge and its
    // owning surface should be almost co-depth.  This cheap test rejects internal
    // and back-side edges without another scene traversal.
    constexpr double kDisplayDepthEps = 1.2e-4;
    if (candidateDisplay[2] <= frontZ + kDisplayDepthEps)
        return true;

    // Account for a very small line-vs-triangle rasterization disagreement near
    // silhouettes.  Use only ~2 screen pixels in world units, not the full 7px
    // pick radius, so interior geometry does not leak through.
    double frontWorld[3] = {0.0, 0.0, 0.0};
    double onePixelWorld[3] = {0.0, 0.0, 0.0};
    if (displayToWorld(renderer, px, py, frontZ, frontWorld) &&
        displayToWorld(renderer, px + 1.0, py, frontZ, onePixelWorld))
    {
        const double worldPerPixel = distance3(frontWorld, onePixelWorld);
        if (std::isfinite(worldPerPixel) &&
            distance3(candidateWorld, frontWorld) <=
                std::max(1.0e-8, worldPerPixel * 2.0))
        {
            return true;
        }
    }

    Q_UNUSED(radiusPixels);
    return false;
}

inline bool pickObjectFromPreparedList(int x, int y, QString& pickedUuid)
{
    pickedUuid.clear();
    ControllerState& state = controller();
    Session& session = state.session;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = vis ? vis->renderer() : nullptr;
    if (!renderer || !session.objectPicker || session.objectUuidByActor.isEmpty())
        return false;

    if (session.objectPicker->Pick(x, y, 0.0, renderer) == 0)
        return false;

    vtkActor* actor = session.objectPicker->GetActor();
    const QString uuid = session.objectUuidByActor.value(actor);
    if (uuid.isEmpty())
        return false;
    if (session.kind == SessionKind::ExternalActors)
    {
        if (!actor || actor->GetVisibility() == 0 || actor->GetPickable() == 0)
            return false;
    }
    else if (!GeometryDisplayManager::isVisible(uuid))
    {
        return false;
    }

    pickedUuid = uuid;
    return true;
}

inline bool pickTopologyAt(const QString& sourceUuid,
                           GeometryPickMode mode,
                           int x,
                           int y,
                           int& topologyIndex,
                           vtkCellPicker* preparedSceneSurfacePicker = nullptr,
                           QHash<vtkActor*, QString>* preparedSurfaceUuidMap = nullptr,
                           Session* preparedSession = nullptr,
                           double* screenDistance2Out = nullptr)
{
    topologyIndex = 0;
    if (screenDistance2Out)
        *screenDistance2Out = std::numeric_limits<double>::max();

    if (sourceUuid.isEmpty() ||
        (mode != GeometryPickMode::Edge && mode != GeometryPickMode::Face) ||
        !GeometryDisplayManager::isVisible(sourceUuid))
    {
        return false;
    }

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = vis ? vis->renderer() : nullptr;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(sourceUuid) : nullptr;
    if (!renderer || !obj)
        return false;

    vtkActor* actor = mode == GeometryPickMode::Edge ? obj->getEdgeActor()
                                                     : obj->getSurfActor();
    vtkPolyData* poly = actorPolyData(actor);
    const char* idArrayName = mode == GeometryPickMode::Edge ? "OCC_EdgeId"
                                                             : "OCC_FaceId";
    if (!actor || !poly || !poly->GetCellData() ||
        !poly->GetCellData()->GetArray(idArrayName))
    {
        return false;
    }

    if (mode == GeometryPickMode::Edge && poly->GetNumberOfLines() <= 0)
        return false;
    if (mode == GeometryPickMode::Face && poly->GetNumberOfPolys() <= 0)
        return false;

    Session localSession;
    Session& session = preparedSession ? *preparedSession : localSession;
    if (!session.sceneSurfacePicker)
    {
        if (preparedSceneSurfacePicker)
            session.sceneSurfacePicker = preparedSceneSurfacePicker;
        else
            session.sceneSurfacePicker = buildSceneSurfacePicker(&session.sceneSurfaceUuidByActor);
    }
    if (preparedSurfaceUuidMap)
        session.sceneSurfaceUuidByActor = *preparedSurfaceUuidMap;

    const int savedVisibility = actor->GetVisibility();
    const int savedPickable = actor->GetPickable();
    actor->SetVisibility(1);
    actor->PickableOn();

    constexpr double kEdgePickRadiusPx = 7.0;
    constexpr double kFacePickRadiusPx = 3.0;

    if (mode == GeometryPickMode::Edge)
    {
        QVector<EdgeCandidate> candidates;
        const bool foundCandidates = collectEdgeCandidates(session, sourceUuid, actor, poly,
                                                            renderer, x, y,
                                                            kEdgePickRadiusPx, candidates);

        actor->SetVisibility(savedVisibility);
        actor->SetPickable(savedPickable);

        if (foundCandidates)
        {
            // This is the important FreeCAD-like part: if the geometrically nearest
            // line is hidden, do not fail the click.  Walk the nearby picked list
            // and return the first actually visible edge.
            for (const EdgeCandidate& candidate : candidates)
            {
                if (candidateVisible(session, sourceUuid, mode, candidate.edgeIndex,
                                     actor, renderer, candidate.world, candidate.display,
                                     kEdgePickRadiusPx))
                {
                    topologyIndex = candidate.edgeIndex;
                    if (screenDistance2Out)
                        *screenDistance2Out = candidate.screenDistance2;
                    return topologyIndex > 0;
                }
            }
            return false;
        }

        // Locator fallback for unusual legacy edge data.
        vtkNew<vtkCellPicker> fallback;
        fallback->SetTolerance(normalizedPickTolerance(renderer, kEdgePickRadiusPx));
        fallback->PickFromListOn();
        fallback->AddPickList(actor);
        actor->SetVisibility(1);
        actor->PickableOn();
        const int picked = fallback->Pick(x, y, 0.0, renderer);
        actor->SetVisibility(savedVisibility);
        actor->SetPickable(savedPickable);
        if (!picked || fallback->GetActor() != actor)
            return false;

        const int edgeIndex = cellIdentity(poly, fallback->GetCellId(), idArrayName);
        double candidateWorld[3] = {0.0, 0.0, 0.0};
        double candidateDisplay[3] = {0.0, 0.0, 1.0};
        fallback->GetPickPosition(candidateWorld);
        if (edgeIndex <= 0 || !worldToDisplay(renderer, candidateWorld, candidateDisplay) ||
            !candidateVisible(session, sourceUuid, mode, edgeIndex, actor, renderer,
                              candidateWorld, candidateDisplay, kEdgePickRadiusPx))
        {
            return false;
        }
        topologyIndex = edgeIndex;
        if (screenDistance2Out)
        {
            const double dx = candidateDisplay[0] - static_cast<double>(x);
            const double dy = candidateDisplay[1] - static_cast<double>(y);
            *screenDistance2Out = dx * dx + dy * dy;
        }
        return true;
    }

    vtkNew<vtkCellPicker> picker;
    picker->SetTolerance(normalizedPickTolerance(renderer, kFacePickRadiusPx));
    picker->PickFromListOn();
    picker->AddPickList(actor);
    const int picked = picker->Pick(x, y, 0.0, renderer);
    actor->SetVisibility(savedVisibility);
    actor->SetPickable(savedPickable);
    if (!picked || picker->GetActor() != actor)
        return false;

    const int faceIndex = cellIdentity(poly, picker->GetCellId(), idArrayName);
    double candidateWorld[3] = {0.0, 0.0, 0.0};
    double candidateDisplay[3] = {0.0, 0.0, 1.0};
    picker->GetPickPosition(candidateWorld);
    if (faceIndex <= 0 || !worldToDisplay(renderer, candidateWorld, candidateDisplay) ||
        !candidateVisible(session, sourceUuid, mode, faceIndex, actor, renderer,
                          candidateWorld, candidateDisplay, kFacePickRadiusPx))
    {
        return false;
    }

    topologyIndex = faceIndex;
    if (screenDistance2Out)
        *screenDistance2Out = 0.0;
    return true;
}

// Return the front-most visible CAD surface at one display position.  This is
// intentionally surface-first: a hidden/internal edge is never allowed to become
// the object-selection authority merely because it lies near the cursor.
inline bool frontSurfaceAt(Session& session,
                           int x,
                           int y,
                           QString& uuid,
                           int* faceIndex = nullptr)
{
    uuid.clear();
    if (faceIndex)
        *faceIndex = 0;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderer* renderer = vis ? vis->renderer() : nullptr;
    if (!renderer || !session.sceneSurfacePicker)
        return false;

    if (session.sceneSurfacePicker->Pick(x, y, 0.0, renderer) == 0)
        return false;

    vtkActor* actor = session.sceneSurfacePicker->GetActor();
    const QString candidateUuid = session.sceneSurfaceUuidByActor.value(actor);
    if (candidateUuid.isEmpty() || !GeometryDisplayManager::isVisible(candidateUuid))
        return false;

    uuid = candidateUuid;
    if (faceIndex)
    {
        vtkPolyData* poly = actorPolyData(actor);
        *faceIndex = cellIdentity(poly,
                                  session.sceneSurfacePicker->GetCellId(),
                                  "OCC_FaceId");
    }
    return true;
}

inline bool topologyObjectAllowed(Session& session,
                                  const QString& uuid,
                                  GeometryPickMode mode)
{
    if (uuid.isEmpty() || uuid == session.selfUuid ||
        session.excludedUuids.contains(uuid) ||
        !GeometryDisplayManager::isVisible(uuid))
    {
        return false;
    }

    const QString cacheKey = uuid + QLatin1Char('|') +
                             QString::number(static_cast<int>(mode));
    const auto cached = session.topologyAllowedCache.constFind(cacheKey);
    if (cached != session.topologyAllowedCache.cend())
        return cached.value();

    bool allowed = false;
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (obj && obj->getShape() && !obj->getShape()->IsNull() &&
        (!session.topologyObjectFilter || session.topologyObjectFilter(uuid, obj)))
    {
        vtkActor* actor = mode == GeometryPickMode::Edge ? obj->getEdgeActor()
                                                         : obj->getSurfActor();
        vtkPolyData* poly = actorPolyData(actor);
        if (actor && poly && poly->GetCellData())
        {
            const char* arrayName = mode == GeometryPickMode::Edge ? "OCC_EdgeId"
                                                                   : "OCC_FaceId";
            allowed = poly->GetCellData()->GetArray(arrayName) &&
                      (mode == GeometryPickMode::Edge
                           ? poly->GetNumberOfLines() > 0
                           : poly->GetNumberOfPolys() > 0);
        }
    }

    session.topologyAllowedCache.insert(cacheKey, allowed);
    return allowed;
}

inline bool pickTopologySceneAt(Session& session,
                                GeometryPickMode mode,
                                int x,
                                int y,
                                QString& pickedUuid,
                                int& topologyIndex,
                                bool exhaustive = false)
{
    pickedUuid.clear();
    topologyIndex = 0;

    // Once a feature has accepted its first edge it may lock subsequent picks to
    // the same source.  Before that, an empty provider result means true scene-wide
    // selection, independent of whatever item happens to be shown in a combo box.
    const QString lockedUuid = session.sourceUuidProvider
        ? session.sourceUuidProvider()
        : QString();

    if (!lockedUuid.isEmpty())
    {
        if (!topologyObjectAllowed(session, lockedUuid, mode))
            return false;

        if (!pickTopologyAt(lockedUuid, mode, x, y, topologyIndex,
                            session.sceneSurfacePicker.GetPointer(),
                            &session.sceneSurfaceUuidByActor,
                            &session))
        {
            return false;
        }

        pickedUuid = lockedUuid;
        return true;
    }

    // Hover usually stays on the same object for many mouse events.  Try the
    // previous preview object first: this is only a local vtkCellLocator query and
    // avoids a scene-wide picker traversal in the common case.
    if (!exhaustive && !session.previewUuid.isEmpty() &&
        topologyObjectAllowed(session, session.previewUuid, mode))
    {
        int localIndex = 0;
        if (pickTopologyAt(session.previewUuid, mode, x, y, localIndex,
                           session.sceneSurfacePicker.GetPointer(),
                           &session.sceneSurfaceUuidByActor,
                           &session))
        {
            pickedUuid = session.previewUuid;
            topologyIndex = localIndex;
            return true;
        }
    }

    if (mode == GeometryPickMode::Face)
    {
        QString uuid;
        int faceIndex = 0;
        if (!frontSurfaceAt(session, x, y, uuid, &faceIndex) ||
            faceIndex <= 0 ||
            !topologyObjectAllowed(session, uuid, mode))
        {
            return false;
        }

        pickedUuid = uuid;
        topologyIndex = faceIndex;
        return true;
    }

    constexpr double kEdgePickRadiusPx = 7.0;
    const int sampleRadius = static_cast<int>(std::ceil(kEdgePickRadiusPx * 0.72));

    // Start with the object directly under the cursor.  Only if it does not offer
    // a selectable edge do we sample the small pick-radius neighbourhood.  This
    // keeps the common hover path to one scene pick + one local locator query.
    QVector<QPair<int, int>> offsets;
    if (exhaustive)
    {
        // Clicks are infrequent, so preserve the generous FreeCAD-like radius by
        // checking the neighbourhood only on click.
        offsets = {
            {0, 0},
            { sampleRadius, 0}, {-sampleRadius, 0},
            {0,  sampleRadius}, {0, -sampleRadius},
            { sampleRadius,  sampleRadius},
            { sampleRadius, -sampleRadius},
            {-sampleRadius,  sampleRadius},
            {-sampleRadius, -sampleRadius}
        };
    }
    else
    {
        // Hover must remain cheap.  One front-surface query is enough; a click can
        // still select near silhouettes using the exhaustive path above.
        offsets = {{0, 0}};
    }

    QStringList candidateUuids;
    candidateUuids.reserve(offsets.size());
    for (const auto& off : offsets)
    {
        QString uuid;
        if (!frontSurfaceAt(session, x + off.first, y + off.second, uuid, nullptr))
            continue;
        if (!topologyObjectAllowed(session, uuid, mode) || candidateUuids.contains(uuid))
            continue;

        candidateUuids.push_back(uuid);

        // Fast path: the center object's edge is normally the intended one.
        if (candidateUuids.size() == 1 && off.first == 0 && off.second == 0)
        {
            int edge = 0;
            if (pickTopologyAt(uuid, mode, x, y, edge,
                               session.sceneSurfacePicker.GetPointer(),
                               &session.sceneSurfaceUuidByActor,
                               &session))
            {
                pickedUuid = uuid;
                topologyIndex = edge;
                return true;
            }
        }
    }

    // Neighbourhood fallback.  Usually this list contains only one or two UUIDs.
    // Compare screen-space distance so boundaries between two solids behave
    // predictably instead of depending on map iteration order.
    double bestDistance2 = std::numeric_limits<double>::max();
    QString bestUuid;
    int bestIndex = 0;

    for (const QString& uuid : candidateUuids)
    {
        int edge = 0;
        double distance2 = std::numeric_limits<double>::max();
        if (!pickTopologyAt(uuid, mode, x, y, edge,
                            session.sceneSurfacePicker.GetPointer(),
                            &session.sceneSurfaceUuidByActor,
                            &session,
                            &distance2))
        {
            continue;
        }

        if (distance2 < bestDistance2)
        {
            bestDistance2 = distance2;
            bestUuid = uuid;
            bestIndex = edge;
        }
    }

    if (bestUuid.isEmpty() || bestIndex <= 0)
        return false;

    pickedUuid = bestUuid;
    topologyIndex = bestIndex;
    return true;
}

inline void dispatch(vtkRenderWindowInteractor* iren, unsigned long eventId)
{
    ControllerState& state = controller();
    Session& session = state.session;
    if (!iren || session.kind == SessionKind::None)
        return;

    if (!session.ownerGuard)
    {
        clearSession(false);
        return;
    }

    int pos[2] = {0, 0};
    iren->GetEventPosition(pos);

    if (eventId == vtkCommand::MouseMoveEvent)
    {
        // FreeCAD-style preselection.  Object and topology modes share one global
        // modal controller, so feature panels no longer need their own mouse
        // observers or hover implementation.
        if (session.leftDown)
        {
            // The custom viewport style decides when a pending left press becomes
            // a real camera drag.  Observe that decision after the style has handled
            // MouseMove; do not duplicate it with another pixel-distance threshold.
            if (vtkInteractorStyle* style =
                    vtkInteractorStyle::SafeDownCast(iren->GetInteractorStyle()))
            {
                if (style->GetState() != VTKIS_NONE)
                    session.leftCameraDrag = true;
            }
            return;
        }

        // Any active camera gesture owns MouseMove. Do not run expensive CAD hover
        // picking against actors that may temporarily be replaced by the large-model
        // composite presentation.
        if (vtkInteractorStyle* style =
                vtkInteractorStyle::SafeDownCast(iren->GetInteractorStyle()))
        {
            if (style->GetState() != VTKIS_NONE)
                return;
        }

        const int dx = pos[0] - session.lastHoverX;
        const int dy = pos[1] - session.lastHoverY;
        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = session.lastHoverTime.time_since_epoch().count() == 0
            ? std::chrono::milliseconds(1000)
            : std::chrono::duration_cast<std::chrono::milliseconds>(now - session.lastHoverTime);
        if (dx * dx + dy * dy < 9 && elapsed < std::chrono::milliseconds(90))
            return;
        // Preselection does not need animation-frame frequency.  ~18 Hz is still
        // visually immediate while keeping heavy STEP models responsive.
        if (elapsed < std::chrono::milliseconds(55))
            return;

        session.lastHoverX = pos[0];
        session.lastHoverY = pos[1];
        session.lastHoverTime = now;

        if (session.kind == SessionKind::Object || session.kind == SessionKind::ExternalActors)
        {
            QString uuid;
            const bool hit = pickObjectFromPreparedList(pos[0], pos[1], uuid);
            const QString nextUuid = hit ? uuid : QString();
            if (nextUuid != session.objectPreviewUuid)
                applyObjectPreviewStyle(session, nextUuid, true);
            return;
        }

        QString hitUuid;
        int index = 0;
        bool hit = false;
        if (session.sceneWideTopology)
        {
            hit = pickTopologySceneAt(session, session.topologyMode,
                                      pos[0], pos[1], hitUuid, index, false);
        }
        else
        {
            hitUuid = session.sourceUuidProvider
                ? session.sourceUuidProvider()
                : QString();
            hit = pickTopologyAt(hitUuid, session.topologyMode,
                                 pos[0], pos[1], index,
                                 session.sceneSurfacePicker.GetPointer(),
                                 &session.sceneSurfaceUuidByActor,
                                 &session);
        }

        const QString nextPreviewUuid = hit ? hitUuid : QString();
        const int nextPreview = hit ? index : 0;
        if (nextPreview == session.previewIndex &&
            nextPreviewUuid == session.previewUuid)
        {
            return;
        }

        session.previewUuid = nextPreviewUuid;
        session.previewIndex = nextPreview;
        if (nextPreview > 0)
        {
            if (session.sceneWideTopology && session.topologyObjectPreviewed)
                session.topologyObjectPreviewed(nextPreviewUuid, nextPreview);
            else if (session.topologyPreviewed)
                session.topologyPreviewed(nextPreview);
        }
        else if (session.topologyPreviewCleared)
        {
            session.topologyPreviewCleared();
        }
        return;
    }

    if (eventId == vtkCommand::LeftButtonPressEvent)
    {
        session.leftDown = true;
        session.leftCameraDrag = false;
        session.pressX = pos[0];
        session.pressY = pos[1];
        state.lastPickAppendModifier = iren->GetControlKey() != 0;

        // Do not abort the press. The left-click/left-drag interactor keeps the left gesture
        // pending and will start rotation only after real drag movement. Generic
        // observers may record the press, but a confirmed feature click is consumed
        // on release below before they can act on it.
        return;
    }

    if (eventId != vtkCommand::LeftButtonReleaseEvent)
        return;

    if (!session.leftDown)
        return;
    session.leftDown = false;
    state.lastPickAppendModifier = iren->GetControlKey() != 0;

    // This observer runs before vtkInteractorStyle so it cannot rely only on the
    // MouseMove callback seeing the state transition.  Query the style directly on
    // release as well: if rotation has started, this is a drag and must not pick.
    bool wasCameraDrag = session.leftCameraDrag;
    if (vtkInteractorStyle* style =
            vtkInteractorStyle::SafeDownCast(iren->GetInteractorStyle()))
    {
        wasCameraDrag = wasCameraDrag || (style->GetState() != VTKIS_NONE);
    }
    session.leftCameraDrag = false;
    if (wasCameraDrag)
        return;

    // Do NOT abort the release.  The custom viewport style must receive it to clear
    // its pending-click state; generic VTK mouse handling therefore remains healthy.

    if (session.kind == SessionKind::Object || session.kind == SessionKind::ExternalActors)
    {
        QString uuid;
        if (!pickObjectFromPreparedList(pos[0], pos[1], uuid))
            return;

        const ObjectPicked callback = session.objectPicked;
        clearSession(true);
        if (callback)
            callback(uuid);
        return;
    }

    QString hitUuid;
    int index = 0;
    bool hit = false;
    if (session.sceneWideTopology)
    {
        hit = pickTopologySceneAt(session, session.topologyMode,
                                  pos[0], pos[1], hitUuid, index, true);
    }
    else
    {
        hitUuid = session.sourceUuidProvider
            ? session.sourceUuidProvider()
            : QString();
        hit = pickTopologyAt(hitUuid, session.topologyMode,
                             pos[0], pos[1], index,
                             session.sceneSurfacePicker.GetPointer(),
                             &session.sceneSurfaceUuidByActor,
                             &session);
    }

    if (!hit)
        return;

    if (session.previewIndex != 0 && session.topologyPreviewCleared)
        session.topologyPreviewCleared();
    session.previewUuid.clear();
    session.previewIndex = 0;

    if (session.sceneWideTopology)
    {
        const TopologyObjectPicked callback = session.topologyObjectPicked;
        if (callback)
            callback(hitUuid, index);
    }
    else
    {
        const TopologyPicked callback = session.topologyPicked;
        if (callback)
            callback(index);
    }
}

inline bool ensureObserversInstalled(vtkRenderWindowInteractor* iren)
{
    if (!iren)
        return false;

    ControllerState& state = controller();
    vtkRenderWindowInteractor* current = state.interactor.GetPointer();
    if (current == iren && state.callback && state.pressTag && state.releaseTag && state.moveTag)
        return true;

    if (current)
    {
        if (state.pressTag)
            current->RemoveObserver(state.pressTag);
        if (state.releaseTag)
            current->RemoveObserver(state.releaseTag);
        if (state.moveTag)
            current->RemoveObserver(state.moveTag);
    }

    state.interactor = iren;
    state.pressTag = 0;
    state.releaseTag = 0;
    state.moveTag = 0;
    state.callback = vtkSmartPointer<vtkCallbackCommand>::New();
    state.callback->SetCallback(
        [](vtkObject* caller, unsigned long eventId, void*, void*)
        {
            dispatch(vtkRenderWindowInteractor::SafeDownCast(caller), eventId);
        });

    // Feature picking must receive the full left-button sequence reliably.  Run above
    // generic pick observers / vtkInteractorStyle, but NEVER abort the event here.
    // The custom viewport style still receives Press/Move/Release and owns camera
    // rotation.  On release dispatch() checks the style state: VTKIS_ROTATE means drag;
    // VTKIS_NONE means a real click and performs exactly one geometry pick.
    constexpr float kGeometryPickPriority = 2.0f;
    state.pressTag = iren->AddObserver(vtkCommand::LeftButtonPressEvent,
                                       state.callback,
                                       kGeometryPickPriority);
    state.releaseTag = iren->AddObserver(vtkCommand::LeftButtonReleaseEvent,
                                         state.callback,
                                         kGeometryPickPriority);
    state.moveTag = iren->AddObserver(vtkCommand::MouseMoveEvent,
                                      state.callback,
                                      kGeometryPickPriority);
    return state.pressTag != 0 && state.releaseTag != 0 && state.moveTag != 0;
}

inline bool beginSession(QObject* owner, QPushButton* button, Session session)
{
    // Direct CAD picking (extrude/revolve) is intentionally buttonless.
    // A button is only an optional UI owner for commands such as fillet/chamfer.
    if (!owner)
        return false;

    auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>();
    vtkRenderWindowInteractor* iren = vis ? vis->interactor() : nullptr;
    if (!iren || !ensureObserversInstalled(iren))
        return false;

    // CAD command semantics: one viewport pick tool globally, not one observer
    // pair per page.  Switching tools automatically cancels the previous tool.
    clearSession(true);

    session.ownerKey = owner;
    session.ownerGuard = owner;
    session.button = button;
    session.idleText = button ? button->text() : QString();

    ControllerState& state = controller();
    state.session = std::move(session);

    if (!owner->property("_geometryViewportPickDestroyHookV48").toBool())
    {
        owner->setProperty("_geometryViewportPickDestroyHookV48", true);
        QObject::connect(owner, &QObject::destroyed, [](QObject* object)
        {
            stop(object, false);
        });
    }

    // 不修改按钮文字/按下状态。进入模型选择后由视图中的悬停高亮和
    // 页面提示说明当前动作；再次点击其他选择命令会自动切换。
    return true;
}
} // namespace Detail

inline void configureButton(QPushButton* button)
{
    if (!button)
        return;
    // 选择是一次动作，不使用“按下/再次按下退出”的模式按钮语义。
    button->setCheckable(false);
    button->setMinimumHeight(28);
}

inline bool appendSelectionRequested()
{
    return Detail::controller().lastPickAppendModifier;
}

inline void stop(QObject* owner, bool resetButton = true)
{
    Detail::stop(owner, resetButton);
}

inline bool startObject(QObject* owner,
                        QPushButton* button,
                        const QString& selfUuid,
                        const QStringList& excludedUuids,
                        ObjectFilter filter,
                        ObjectPicked onPicked,
                        const QString& activeText = QString())
{
    if (!owner || !onPicked)
        return false;

    Detail::Session session;
    session.kind = Detail::SessionKind::Object;
    session.activeText = activeText.isEmpty()
        ? QObject::tr("请在模型中选择对象")
        : activeText;
    session.selfUuid = selfUuid;
    session.excludedUuids = excludedUuids;
    session.objectFilter = std::move(filter);
    session.objectPicked = std::move(onPicked);

    // Prepare against the current scene before publishing the mode.  If there is
    // no valid visible candidate, return false immediately so the caller can reset
    // the toggle instead of leaving a dead selection button active.
    if (!Detail::prepareObjectPicker(session))
        return false;

    return Detail::beginSession(owner, button, std::move(session));
}

// Pick from a caller-owned list of already displayed temporary actors.
// This is used by Mesh-only OCC Region selection: the partition preview is not a
// project geometry object, but it should still obey the same click-vs-camera-drag gate.
inline bool startExternalActors(QObject* owner,
                                QPushButton* button,
                                const QVector<QPair<QString, vtkActor*>>& actors,
                                ObjectPicked onPicked,
                                const QString& activeText = QString())
{
    if (!owner || !onPicked || actors.isEmpty())
        return false;

    Detail::Session session;
    session.kind = Detail::SessionKind::ExternalActors;
    session.activeText = activeText.isEmpty()
        ? QObject::tr("请在模型中选择一个分块 Region")
        : activeText;
    session.objectPicked = std::move(onPicked);
    session.objectPicker = vtkSmartPointer<vtkCellPicker>::New();
    session.objectPicker->SetTolerance(0.004);
    session.objectPicker->PickFromListOn();

    for (const auto& entry : actors)
    {
        const QString key = entry.first;
        vtkActor* actor = entry.second;
        vtkPolyData* poly = Detail::actorPolyData(actor);
        // ExternalActors is shared by temporary partition Region / Face / Edge
        // picking.  Edge preview actors contain VTK line cells (no polygons), so
        // filtering on GetNumberOfPolys() incorrectly rejected every partition edge
        // before vtkCellPicker could see it.  Accept any non-empty vtkPolyData.
        if (key.isEmpty() || !actor || !poly || poly->GetNumberOfCells() <= 0 ||
            actor->GetVisibility() == 0 || actor->GetPickable() == 0)
        {
            continue;
        }
        session.objectPicker->AddPickList(actor);
        session.objectUuidByActor.insert(actor, key);
        session.externalActorByKey.insert(key, actor);
    }

    if (session.objectUuidByActor.isEmpty())
        return false;
    return Detail::beginSession(owner, button, std::move(session));
}

inline bool startTopology(QObject* owner,
                          QPushButton* button,
                          GeometryPickMode mode,
                          SourceUuidProvider sourceUuidProvider,
                          TopologyPicked onPicked,
                          const QString& activeText = QString(),
                          TopologyPreviewed onPreview = {},
                          TopologyPreviewCleared onPreviewCleared = {})
{
    if (!owner || !sourceUuidProvider || !onPicked ||
        (mode != GeometryPickMode::Edge && mode != GeometryPickMode::Face))
    {
        return false;
    }

    Detail::Session session;
    session.kind = Detail::SessionKind::Topology;
    session.activeText = activeText.isEmpty()
        ? (mode == GeometryPickMode::Edge ? QObject::tr("正在选择边")
                                          : QObject::tr("正在选择面"))
        : activeText;
    session.topologyMode = mode;
    session.sourceUuidProvider = std::move(sourceUuidProvider);
    session.topologyPicked = std::move(onPicked);
    session.topologyPreviewed = std::move(onPreview);
    session.topologyPreviewCleared = std::move(onPreviewCleared);
    session.sceneSurfacePicker = Detail::buildSceneSurfacePicker(&session.sceneSurfaceUuidByActor);
    return Detail::beginSession(owner, button, std::move(session));
}


// Scene-wide topology selection used by CAD feature commands such as fillet and
// chamfer.  Before the first accepted edge, the combo box does NOT constrain the
// viewport: hover/click returns {objectUuid, subShapeIndex} from the visible scene.
// The optional lock provider can return a UUID after a feature has accepted its
// first edge, preserving the one-source limitation of the OCC feature kernel.
inline bool startTopologyScene(QObject* owner,
                               QPushButton* button,
                               GeometryPickMode mode,
                               SourceUuidProvider lockedSourceUuidProvider,
                               TopologyObjectPicked onPicked,
                               const QString& selfUuid = QString(),
                               const QStringList& excludedUuids = {},
                               ObjectFilter objectFilter = {},
                               const QString& activeText = QString(),
                               TopologyObjectPreviewed onPreview = {},
                               TopologyPreviewCleared onPreviewCleared = {})
{
    if (!owner || !onPicked ||
        (mode != GeometryPickMode::Edge && mode != GeometryPickMode::Face))
    {
        return false;
    }

    Detail::Session session;
    session.kind = Detail::SessionKind::Topology;
    session.sceneWideTopology = true;
    session.activeText = activeText.isEmpty()
        ? (mode == GeometryPickMode::Edge ? QObject::tr("正在选择边")
                                          : QObject::tr("正在选择面"))
        : activeText;
    session.selfUuid = selfUuid;
    session.excludedUuids = excludedUuids;
    session.topologyMode = mode;
    session.sourceUuidProvider = std::move(lockedSourceUuidProvider);
    session.topologyObjectFilter = std::move(objectFilter);
    session.topologyObjectPicked = std::move(onPicked);
    session.topologyObjectPreviewed = std::move(onPreview);
    session.topologyPreviewCleared = std::move(onPreviewCleared);

    // One scene scan at mode activation.  Edge locators remain lazy and bounded.
    session.sceneSurfacePicker =
        Detail::buildSceneSurfacePicker(&session.sceneSurfaceUuidByActor);
    if (!session.sceneSurfacePicker)
        return false;

    return Detail::beginSession(owner, button, std::move(session));
}

inline bool pickFaceAtDisplayPosition(const QString& sourceUuid,
                                      int x,
                                      int y,
                                      int& faceIndex)
{
    return Detail::pickTopologyAt(sourceUuid,
                                  GeometryPickMode::Face,
                                  x,
                                  y,
                                  faceIndex);
}

inline bool pickEdgeAtDisplayPosition(const QString& sourceUuid,
                                      int x,
                                      int y,
                                      int& edgeIndex)
{
    return Detail::pickTopologyAt(sourceUuid,
                                  GeometryPickMode::Edge,
                                  x,
                                  y,
                                  edgeIndex);
}

} // namespace GeometryViewportPickController
