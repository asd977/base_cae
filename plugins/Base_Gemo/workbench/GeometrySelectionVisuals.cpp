#include "GeometrySelectionVisuals.h"

#include <GeometryDisplayManager.h>
#include <GeomManagerInstance.h>
#include <GeomShapeObject.h>
#include <OccToVtkConverter.h>
#include <ServiceRegistry.h>
#include <services/VisualWidgetService.h>

#include <BRep_Builder.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include <QHash>
#include <QSet>
#include <QString>
#include <vtkActor.h>
#include <vtkAssembly.h>
#include <vtkAppendPolyData.h>
#include <vtkCellArray.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkFeatureEdges.h>
#include <vtkIdList.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>

#include <algorithm>

namespace
{
const QString kGeometryTreePickDomain = QStringLiteral("GeometryTreeSelection");
const QString kGeometryTreePickOwner = QStringLiteral("geometry_tree");
const QString kGeometryTreeSelectionPropId = QStringLiteral("geometry.tree.selection.overlay");
const QString kGeometryTreeSourcePreviewPropId = QStringLiteral("geometry.tree.source_preview.overlay");

struct ActorStyleSnapshot
{
    bool valid = false;
    double color[3] = {0.0, 0.0, 0.0};
    double opacity = 1.0;
    double lineWidth = 1.0;
    int representation = VTK_SURFACE;
    bool renderLinesAsTubes = false;
    bool renderPointsAsSpheres = false;
};

struct ObjectStyleSnapshot
{
    ActorStyleSnapshot surface;
    ActorStyleSnapshot edge;
};

QHash<QString, ObjectStyleSnapshot> g_treeSelectionStyleBackups;
QHash<QString, ObjectStyleSnapshot> g_treeSourcePreviewStyleBackups;

TopAbs_ShapeEnum shapeTypeForPickMode(GeometryPickMode mode)
{
    switch (mode)
    {
    case GeometryPickMode::Vertex:
        return TopAbs_VERTEX;
    case GeometryPickMode::Edge:
        return TopAbs_EDGE;
    case GeometryPickMode::Face:
        return TopAbs_FACE;
    case GeometryPickMode::Solid:
        return TopAbs_SOLID;
    }
    return TopAbs_EDGE;
}

void captureActorStyle(vtkActor* actor, ActorStyleSnapshot& snapshot)
{
    if (!actor || !actor->GetProperty())
        return;

    vtkProperty* property = actor->GetProperty();
    snapshot.valid = true;
    property->GetColor(snapshot.color);
    snapshot.opacity = property->GetOpacity();
    snapshot.lineWidth = property->GetLineWidth();
    snapshot.representation = property->GetRepresentation();
    snapshot.renderLinesAsTubes = property->GetRenderLinesAsTubes() != 0;
    snapshot.renderPointsAsSpheres = property->GetRenderPointsAsSpheres() != 0;
}

void restoreActorStyle(vtkActor* actor, const ActorStyleSnapshot& snapshot)
{
    if (!actor || !snapshot.valid || !actor->GetProperty())
        return;

    vtkProperty* property = actor->GetProperty();
    property->SetColor(snapshot.color[0], snapshot.color[1], snapshot.color[2]);
    property->SetOpacity(snapshot.opacity);
    property->SetLineWidth(snapshot.lineWidth);
    property->SetRepresentation(snapshot.representation);
    property->SetRenderLinesAsTubes(snapshot.renderLinesAsTubes ? 1 : 0);
    property->SetRenderPointsAsSpheres(snapshot.renderPointsAsSpheres ? 1 : 0);
}

void restoreObjectHighlightStyles(QHash<QString, ObjectStyleSnapshot>& backups)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr)
    {
        backups.clear();
        return;
    }

    for (auto it = backups.constBegin(); it != backups.constEnd(); ++it)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(it.key());
        if (!obj)
            continue;

        restoreActorStyle(obj->getSurfActor(), it.value().surface);
        restoreActorStyle(obj->getEdgeActor(), it.value().edge);
    }
    backups.clear();
}

bool shapeContains(const TopoDS_Shape& shape, TopAbs_ShapeEnum type)
{
    if (shape.IsNull())
        return false;

    // Highlight classification only needs an existence test.  Building a complete
    // TopTools_IndexedMapOfShape here made every tree/source highlight proportional
    // to the entire imported B-Rep topology.
    TopExp_Explorer exp(shape, type);
    return exp.More();
}

bool applyObjectTreeHighlight(const QString& uuid, bool preview, QHash<QString, ObjectStyleSnapshot>& backups)
{
    if (uuid.isEmpty())
        return false;

    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    GeomShapeObject* obj = mgr ? mgr->getGeomShapeObject(uuid) : nullptr;
    if (!obj || !obj->getShape() || obj->getShape()->IsNull())
        return false;

    vtkActor* surfaceActor = obj->getSurfActor();
    vtkActor* edgeActor = obj->getEdgeActor();
    if (!surfaceActor && !edgeActor)
        return false;

    if (!backups.contains(uuid))
    {
        ObjectStyleSnapshot snapshot;
        captureActorStyle(surfaceActor, snapshot.surface);
        captureActorStyle(edgeActor, snapshot.edge);
        backups.insert(uuid, snapshot);
    }

    const TopoDS_Shape& shape = *obj->getShape();
    const bool hasSolid = shapeContains(shape, TopAbs_SOLID);
    const bool hasFace = shapeContains(shape, TopAbs_FACE);
    const bool hasSurface = hasSolid || hasFace;

    const double selectSurfaceColor[3] = {0.00, 0.72, 1.00};
    const double previewSurfaceColor[3] = {1.00, 0.74, 0.18};
    const double selectCurveColor[3] = {0.00, 0.88, 1.00};
    const double previewCurveColor[3] = {1.00, 0.74, 0.18};

    if (!GeometryDisplayManager::isVisible(uuid))
        return false;

    bool changed = false;

    if (hasSurface && surfaceActor && surfaceActor->GetProperty())
    {
        vtkProperty* property = surfaceActor->GetProperty();
        property->SetRepresentationToSurface();
        const double* color = preview ? previewSurfaceColor : selectSurfaceColor;
        property->SetColor(color[0], color[1], color[2]);
        property->SetOpacity(1.0);
        changed = true;

        if (!hasSolid && edgeActor && edgeActor->GetProperty())
        {
            vtkProperty* edgeProperty = edgeActor->GetProperty();
            const double* edgeColor = preview ? previewCurveColor : selectCurveColor;
            edgeProperty->SetColor(edgeColor[0], edgeColor[1], edgeColor[2]);
            edgeProperty->SetLineWidth(preview ? 1.8 : 2.0);
            edgeProperty->SetRenderLinesAsTubes(0);
            changed = true;
        }

        return changed;
    }

    if (edgeActor && edgeActor->GetProperty())
    {
        vtkProperty* property = edgeActor->GetProperty();
        const double* color = preview ? previewCurveColor : selectCurveColor;
        property->SetRepresentationToWireframe();
        property->SetColor(color[0], color[1], color[2]);
        property->SetLineWidth(preview ? 1.8 : 2.0);
        property->SetOpacity(1.0);
        property->SetRenderLinesAsTubes(0);
        changed = true;
    }

    return changed;
}


void configureSelectionSurfaceMapper(vtkPolyDataMapper* mapper)
{
    if (!mapper)
        return;

    // Selection faces are extracted from the exact same OCC_FaceId triangles as
    // the normal CAD actor, so they are intentionally coplanar.  A units-only
    // depth bias is stable when viewed nearly face-on, but at grazing camera
    // angles the window-space depth slope grows and the two surfaces can start
    // competing again.  The result is not a logical selection change: the base
    // CAD colour and the selection colour alternate in the depth buffer, which
    // looks like the orange highlight changes shade or flickers while rotating.
    //
    // VTK's OpenGL coincident-topology path implements
    //   z' = z + factor * depthSlope + units / 65000
    // so use the conventional slope-aware polygon offset here.  Keeping the
    // units term small preserves normal occlusion by genuinely nearer faces, while
    // the negative slope factor gives the selected polygon just enough priority at
    // oblique views.  This is preferable to physically moving CAD vertices along
    // their normals, which would introduce visible geometric separation at edges.
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(-1.0, -2.0);
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, 0.0);
}

void configureSelectionEdgeMapper(vtkPolyDataMapper* mapper)
{
    if (!mapper)
        return;
    mapper->SetRelativeCoincidentTopologyPolygonOffsetParameters(0.0, 0.0);
    mapper->SetRelativeCoincidentTopologyLineOffsetParameters(0.0, -3.0);
}

void removeVisualProp(const QString& propId, bool render)
{
    if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
    {
        vis->removeProp(propId, false);
        if (render)
            vis->render();
    }
}
} // namespace

namespace GeometrySelectionVisuals
{
vtkSmartPointer<vtkProp> buildPickActor(const QVector<GeometryPickItem>& items, bool preview)
{
    auto* mgr = GeomManager::GeomManagerInstance::getInstance();
    if (!mgr || items.isEmpty())
        return nullptr;

    // Selection identity stays {shapeUuid, mode, OCC index}.  For the normal CAD
    // display path, do not tessellate the selected sub-shape again.
    // The visible Surface/Edge actors already carry OCC_FaceId / OCC_EdgeId and
    // use the exact tessellation that is on screen. Extracting from those actors:
    //   1) makes the highlight sit exactly on the black CAD edge/face;
    //   2) avoids the old orange/black broken-line appearance;
    //   3) avoids an extra OCC presentation build every time the user clicks.
    QHash<QString, QSet<int>> edgeIndicesByShape;
    QHash<QString, QSet<int>> vertexIndicesByShape;
    QHash<QString, QSet<int>> faceIndicesByShape;
    QHash<QString, QSet<int>> solidIndicesByShape;

    for (const GeometryPickItem& item : items)
    {
        if (!item.isValid())
            continue;
        switch (item.mode)
        {
        case GeometryPickMode::Edge:
            edgeIndicesByShape[item.shapeUuid].insert(item.index);
            break;
        case GeometryPickMode::Vertex:
            vertexIndicesByShape[item.shapeUuid].insert(item.index);
            break;
        case GeometryPickMode::Face:
            faceIndicesByShape[item.shapeUuid].insert(item.index);
            break;
        case GeometryPickMode::Solid:
            solidIndicesByShape[item.shapeUuid].insert(item.index);
            break;
        }
    }

    vtkNew<vtkAppendPolyData> wireAppender;
    vtkNew<vtkAppendPolyData> surfaceAppender;
    bool hasWireData = false;
    bool hasSurfaceData = false;

    auto actorPolyData = [](vtkActor* actor) -> vtkPolyData*
    {
        auto* mapper = actor ? vtkPolyDataMapper::SafeDownCast(actor->GetMapper()) : nullptr;
        return mapper ? mapper->GetInput() : nullptr;
    };

    auto extractByUiId = [](vtkPolyData* source,
                            const char* idArrayName,
                            const QSet<int>& wantedIds,
                            bool asSurface) -> vtkSmartPointer<vtkPolyData>
    {
        if (!source || !source->GetPoints() || wantedIds.isEmpty())
            return nullptr;

        vtkDataArray* idArray = source->GetCellData()
                                    ? source->GetCellData()->GetArray(idArrayName)
                                    : nullptr;
        if (!idArray || idArray->GetNumberOfTuples() != source->GetNumberOfCells())
            return nullptr;

        // Reuse the exact points of the visible CAD presentation instead of
        // rebuilding every selected triangle with duplicated vertices.  The old
        // path discarded point normals and disconnected neighbouring triangles,
        // which made a selected face look like a translucent triangulated sticker.
        // Sharing the source points also keeps the overlay pixel-for-pixel aligned
        // with the rendered OCC tessellation.
        vtkNew<vtkCellArray> cells;
        vtkNew<vtkIdList> srcIds;

        for (vtkIdType cellId = 0; cellId < source->GetNumberOfCells(); ++cellId)
        {
            const int occId = static_cast<int>(idArray->GetTuple1(cellId));
            if (!wantedIds.contains(occId))
                continue;

            source->GetCellPoints(cellId, srcIds);
            const vtkIdType pointCount = srcIds->GetNumberOfIds();
            if (pointCount < (asSurface ? 3 : 2))
                continue;

            cells->InsertNextCell(srcIds);
        }

        if (cells->GetNumberOfCells() <= 0)
            return nullptr;

        auto output = vtkSmartPointer<vtkPolyData>::New();
        output->SetPoints(source->GetPoints());
        if (source->GetPointData())
            output->GetPointData()->ShallowCopy(source->GetPointData());
        if (asSurface)
            output->SetPolys(cells);
        else
            output->SetLines(cells);
        return output;
    };

    // Fast path: take exactly the cells already used by the CAD presentation.
    // Keep unresolved entries for a local OCC presentation build below (old
    // projects / legacy actors without OCC_FaceId / OCC_EdgeId arrays).
    QHash<QString, QSet<int>> unresolvedEdges = edgeIndicesByShape;
    QHash<QString, QSet<int>> unresolvedFaces = faceIndicesByShape;
    QHash<QString, QSet<int>> unresolvedSolids = solidIndicesByShape;

    for (auto it = edgeIndicesByShape.cbegin(); it != edgeIndicesByShape.cend(); ++it)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(it.key());
        vtkPolyData* source = obj ? actorPolyData(obj->getEdgeActor()) : nullptr;
        auto selected = extractByUiId(source, "OCC_EdgeId", it.value(), false);
        if (selected && selected->GetNumberOfLines() > 0)
        {
            wireAppender->AddInputData(selected);
            hasWireData = true;
            unresolvedEdges.remove(it.key());
        }
    }

    for (auto it = faceIndicesByShape.cbegin(); it != faceIndicesByShape.cend(); ++it)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(it.key());
        vtkPolyData* source = obj ? actorPolyData(obj->getSurfActor()) : nullptr;
        auto selected = extractByUiId(source, "OCC_FaceId", it.value(), true);
        if (selected && selected->GetNumberOfPolys() > 0)
        {
            surfaceAppender->AddInputData(selected);
            hasSurfaceData = true;
            unresolvedFaces.remove(it.key());
        }
    }

    // Solid selection used to be rebuilt through a second OCC tessellation because
    // the display mesh has no OCC_SolidId array.  On curved solids (especially a
    // cylinder) the rebuilt triangles are not exactly the same chords as the normal
    // CAD presentation, so the two opaque surfaces intersect in depth and appear as
    // alternating orange/grey strips.
    //
    // We can still stay on the exact display tessellation without OCC_SolidId:
    // resolve every selected Solid to the owning object's OCC Face ids, then extract
    // those OCC_FaceId cells from the already visible surface actor.  This keeps body
    // selection pixel-for-pixel on the CAD surface and also continues to support
    // compounds where only one sub-solid is selected.
    for (auto it = solidIndicesByShape.cbegin(); it != solidIndicesByShape.cend(); ++it)
    {
        GeomShapeObject* obj = mgr->getGeomShapeObject(it.key());
        vtkPolyData* source = obj ? actorPolyData(obj->getSurfActor()) : nullptr;
        if (!obj || !obj->getShape() || obj->getShape()->IsNull() || !source)
            continue;

        TopTools_IndexedMapOfShape localSolids;
        TopTools_IndexedMapOfShape localFaces;
        TopExp::MapShapes(*obj->getShape(), TopAbs_SOLID, localSolids);
        TopExp::MapShapes(*obj->getShape(), TopAbs_FACE, localFaces);
        if (localSolids.Extent() <= 0 || localFaces.Extent() <= 0)
            continue;

        QSet<int> displayFaceIds;
        QSet<int> displayResolvedSolidIds;
        for (int solidIndex : it.value())
        {
            if (solidIndex <= 0 || solidIndex > localSolids.Extent())
                continue;

            TopTools_IndexedMapOfShape solidFaces;
            TopExp::MapShapes(localSolids(solidIndex), TopAbs_FACE, solidFaces);
            if (solidFaces.Extent() <= 0)
                continue;

            bool complete = true;
            QSet<int> oneSolidFaceIds;
            for (int faceIndex = 1; faceIndex <= solidFaces.Extent(); ++faceIndex)
            {
                const int localFaceId = localFaces.FindIndex(solidFaces(faceIndex));
                if (localFaceId <= 0)
                {
                    complete = false;
                    break;
                }
                oneSolidFaceIds.insert(localFaceId);
            }

            if (!complete || oneSolidFaceIds.isEmpty())
                continue;

            displayFaceIds.unite(oneSolidFaceIds);
            displayResolvedSolidIds.insert(solidIndex);
        }

        auto selected = extractByUiId(source, "OCC_FaceId", displayFaceIds, true);
        if (!selected || selected->GetNumberOfPolys() <= 0)
            continue;

        surfaceAppender->AddInputData(selected);
        hasSurfaceData = true;

        auto unresolvedIt = unresolvedSolids.find(it.key());
        if (unresolvedIt != unresolvedSolids.end())
        {
            for (int solidIndex : displayResolvedSolidIds)
                unresolvedIt.value().remove(solidIndex);
            if (unresolvedIt.value().isEmpty())
                unresolvedSolids.erase(unresolvedIt);
        }
    }

    // Only legacy actors without OCC_FaceId (or an otherwise unresolved Solid)
    // fall back to a local OCC presentation build.
    auto appendOccPresentation = [&](const QHash<QString, QSet<int>>& groups,
                                     GeometryPickMode mode,
                                     bool asSurface)
    {
        for (auto groupIt = groups.cbegin(); groupIt != groups.cend(); ++groupIt)
        {
            GeomShapeObject* obj = mgr->getGeomShapeObject(groupIt.key());
            if (!obj || !obj->getShape() || obj->getShape()->IsNull())
                continue;

            TopTools_IndexedMapOfShape indexed;
            TopExp::MapShapes(*obj->getShape(), shapeTypeForPickMode(mode), indexed);
            if (indexed.Extent() <= 0)
                continue;

            QList<int> indices = groupIt.value().values();
            std::sort(indices.begin(), indices.end());

            TopoDS_Compound compound;
            BRep_Builder builder;
            builder.MakeCompound(compound);
            bool compoundHasShape = false;
            for (int index : indices)
            {
                if (index <= 0 || index > indexed.Extent())
                    continue;
                builder.Add(compound, indexed(index));
                compoundHasShape = true;
            }
            if (!compoundHasShape)
                continue;

            const auto display = OCC::BuildDisplayPolyData(compound, 0.5, 28.5,
                                                           false, !asSurface);
            vtkPolyData* output = asSurface ? display.surface.GetPointer()
                                            : display.edges.GetPointer();
            if (!output || output->GetNumberOfCells() <= 0)
                continue;

            if (asSurface)
            {
                surfaceAppender->AddInputData(output);
                hasSurfaceData = true;
            }
            else
            {
                wireAppender->AddInputData(output);
                hasWireData = true;
            }
        }
    };

    // Vertex presentation currently has no OCC_VertexId display array. Edge/Face
    // need this local build only for legacy actors.
    appendOccPresentation(unresolvedEdges, GeometryPickMode::Edge, false);
    appendOccPresentation(vertexIndicesByShape, GeometryPickMode::Vertex, false);
    appendOccPresentation(unresolvedFaces, GeometryPickMode::Face, true);
    appendOccPresentation(unresolvedSolids, GeometryPickMode::Solid, true);

    vtkSmartPointer<vtkActor> surfaceActor;
    if (hasSurfaceData)
    {
        surfaceAppender->Update();
        vtkPolyData* selectedSurface = surfaceAppender->GetOutput();

        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(selectedSurface);
        mapper->ScalarVisibilityOff();
        mapper->SetResolveCoincidentTopologyToPolygonOffset();
        configureSelectionSurfaceMapper(mapper);
        mapper->StaticOn();

        surfaceActor = vtkSmartPointer<vtkActor>::New();
        surfaceActor->SetMapper(mapper);
        surfaceActor->PickableOff();
        vtkProperty* property = surfaceActor->GetProperty();
        property->LightingOff();
        property->SetAmbient(1.0);
        property->SetDiffuse(0.0);
        property->SetSpecular(0.0);
        property->SetRepresentationToSurface();
        property->EdgeVisibilityOff();
        property->SetColor(preview ? 1.0 : 0.0,
                           preview ? 0.68 : 0.72,
                           preview ? 0.12 : 1.0);
        // Use an opaque CAD-style selection mask.  Semi-transparent coincident
        // geometry is order-dependent and produced patchy/z-fighting artefacts.
        property->SetOpacity(1.0);

        // Outline the boundary of the selected face region.  Internal triangle
        // edges stay hidden, so the user sees one continuous face selection rather
        // than the tessellation used to display it.
        vtkNew<vtkFeatureEdges> boundary;
        boundary->SetInputData(selectedSurface);
        boundary->BoundaryEdgesOn();
        boundary->FeatureEdgesOff();
        boundary->ManifoldEdgesOff();
        boundary->NonManifoldEdgesOff();
        boundary->ColoringOff();
        boundary->Update();
        vtkPolyData* boundaryData = boundary->GetOutput();
        if (boundaryData && boundaryData->GetNumberOfLines() > 0)
        {
            wireAppender->AddInputData(boundaryData);
            hasWireData = true;
        }
    }

    vtkSmartPointer<vtkActor> wireActor;
    if (hasWireData)
    {
        wireAppender->Update();
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(wireAppender->GetOutput());
        mapper->ScalarVisibilityOff();
        mapper->SetResolveCoincidentTopologyToPolygonOffset();
        configureSelectionEdgeMapper(mapper);
        mapper->StaticOn();

        wireActor = vtkSmartPointer<vtkActor>::New();
        wireActor->SetMapper(mapper);
        wireActor->PickableOff();
        vtkProperty* property = wireActor->GetProperty();
        property->LightingOff();
        property->SetAmbient(1.0);
        property->SetDiffuse(0.0);
        property->SetSpecular(0.0);
        property->SetRepresentationToWireframe();
        // 2px ordinary OpenGL lines mix badly with the underlying black CAD edge.
        // A slightly wider tube-style line gives one continuous, intentional CAD
        // selection stroke without adding endpoint markers or changing topology.
        property->SetRenderLinesAsTubes(1);
        property->SetRenderPointsAsSpheres(0);
        property->SetColor(preview ? 1.0 : 1.0,
                           preview ? 0.64 : 0.46,
                           preview ? 0.10 : 0.02);
        property->SetOpacity(1.0);
        property->SetLineWidth(preview ? 2.5 : 3.0);
    }

    if (surfaceActor && !wireActor)
        return surfaceActor;
    if (wireActor && !surfaceActor)
        return wireActor;
    if (!surfaceActor && !wireActor)
        return nullptr;

    auto assembly = vtkSmartPointer<vtkAssembly>::New();
    assembly->PickableOff();
    assembly->AddPart(surfaceActor);
    assembly->AddPart(wireActor);
    return assembly;
}

void clearTreePickSelection(bool render)
{
    GeometryPickManager::instance().clearSelection(kGeometryTreePickDomain,
                                                   kGeometryTreePickOwner,
                                                   render);
}

bool applyTreeSelectionHighlight(const QString& uuid)
{
    return applyObjectTreeHighlight(uuid, false, g_treeSelectionStyleBackups);
}

void clearTreeSelectionHighlight(bool render)
{
    restoreObjectHighlightStyles(g_treeSelectionStyleBackups);
    removeVisualProp(kGeometryTreeSelectionPropId, render);
    clearTreePickSelection(false);
}

bool applySourcePreviewHighlight(const QString& uuid)
{
    return applyObjectTreeHighlight(uuid, true, g_treeSourcePreviewStyleBackups);
}

void clearSourcePreviewHighlight(bool render)
{
    restoreObjectHighlightStyles(g_treeSourcePreviewStyleBackups);
    removeVisualProp(kGeometryTreeSourcePreviewPropId, render);
}

void refreshHighlights(bool render)
{
    // Preserve the logical highlight owners before restoring actor styles.
    // Source-preview is applied on top of tree selection, so restore in the
    // opposite order and then rebuild in the original order.  This matters
    // when the same object is both the current tree item and a feature input.
    const QList<QString> treeSelectionUuids = g_treeSelectionStyleBackups.keys();
    const QList<QString> sourcePreviewUuids = g_treeSourcePreviewStyleBackups.keys();

    restoreObjectHighlightStyles(g_treeSourcePreviewStyleBackups);
    restoreObjectHighlightStyles(g_treeSelectionStyleBackups);

    // Do not filter hidden objects here. applyObjectTreeHighlight() records the
    // logical owner even when the actor is currently hidden, which allows a
    // later visibility refresh to restore the same highlight after Show.
    for (const QString& uuid : treeSelectionUuids)
        applyObjectTreeHighlight(uuid, false, g_treeSelectionStyleBackups);

    for (const QString& uuid : sourcePreviewUuids)
        applyObjectTreeHighlight(uuid, true, g_treeSourcePreviewStyleBackups);

    if (render)
    {
        if (auto* vis = ServiceRegistry::instance().getService<VisualWidgetService>())
            vis->render();
    }
}
} // namespace GeometrySelectionVisuals
