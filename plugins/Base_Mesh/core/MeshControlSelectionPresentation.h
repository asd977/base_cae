#pragma once

#include <ServiceRegistry.h>
#include <services/MeshModuleControllerService.h>
#include <services/VisualWidgetService.h>
#include <services/PreProcessingService.h>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QSignalBlocker>

#include <QString>
#include <vtkActor.h>
#include <vtkProperty.h>
#include <vtkUnstructuredGrid.h>

// Geometry-selection presentation used by Edge / Face / Solid / Wall controls.
//
// Design rules:
//   1. Mesh controls always select CAD topology, never mesh cells/edges.
//   2. If a mesh already exists, entering a control editor hides the mesh by default.
//   3. The old mesh is kept in memory and can optionally be shown as a translucent,
//      non-pickable reference.
//   4. Leaving the control editor restores the exact mesh actor state that was active
//      before editing; editing controls never deletes the old result.
class MeshControlSelectionPresentation
{
public:
    enum class MeshDisplayMode
    {
        Hidden,
        Reference,
        Result
    };

    QWidget* createViewSelector(QWidget* parent)
    {
        parent->setProperty("mesh.ownsScene", true);
        auto* row = new QWidget(parent);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(new QLabel(QStringLiteral("视图"), row));
        auto* selector = new QComboBox(row);
        selector->setObjectName(QStringLiteral("meshSettingsViewSelector"));
        selector->addItem(QStringLiteral("几何"));
        selector->addItem(QStringLiteral("网格"));
        layout->addWidget(selector, 1);
        m_selector = selector;
        QObject::connect(selector, QOverload<int>::of(&QComboBox::currentIndexChanged), row,
            [this](int index) {
                m_mode = index == 1 && hasCurrentMesh() ? MeshDisplayMode::Result : MeshDisplayMode::Hidden;
                if (m_active) {
                    applyCurrentMode();
                    syncHighlight();
                    m_visual->render();
                }
            });
        return row;
    }

    void setSelectedEntities(int dimension, const QList<int>& ids)
    {
        m_selectionDefined = true;
        m_selectionDimension = dimension;
        m_selectedIds = ids;
        m_selectedIds.removeIf([](int id) { return id <= 0; });
        syncHighlight();
    }

    bool showingGeometry() const { return m_mode != MeshDisplayMode::Result; }

    void enter(bool showReferenceMesh = false)
    {
        if (m_active)
        {
            if (m_selector) {
                const QSignalBlocker blocker(m_selector);
                m_selector->setCurrentIndex(0);
                m_selector->setEnabled(hasCurrentMesh());
            }
            m_mode = showReferenceMesh ? MeshDisplayMode::Reference : MeshDisplayMode::Hidden;
            refresh();
            return;
        }

        m_visual = ServiceRegistry::instance().getService<VisualWidgetService>();
        if (!m_visual)
            return;

        if (m_selector) {
            const QSignalBlocker blocker(m_selector);
            m_selector->setCurrentIndex(0);
            m_selector->setEnabled(hasCurrentMesh());
        }

        m_hadMesh = hasCurrentMesh();
        snapshot(QStringLiteral("mesh.imported.surface"), m_surface);
        snapshot(QStringLiteral("mesh.imported.line"), m_line);
        snapshot(QStringLiteral("mesh.boundarylayer.internalEdges"), m_boundaryLayer);
        snapshot(QStringLiteral("mesh.localTrial.preview"), m_trialPreview);

        // CAD is always the authoritative selection target in mesh-control edit mode.
        m_visual->setGroupVisible(QStringLiteral("Geometry"), true, false);

        m_mode = showReferenceMesh ? MeshDisplayMode::Reference : MeshDisplayMode::Hidden;
        m_active = true;
        applyCurrentMode();
        syncHighlight();
        m_visual->render();
    }

    void refresh()
    {
        if (!m_active || !m_visual)
            return;
        m_visual->setGroupVisible(QStringLiteral("Geometry"), true, false);
        m_hadMesh = hasCurrentMesh();
        if (m_hadMesh)
        {
            // A remesh/display refresh can replace VTK actors while a control page
            // remains open. Snapshot the new live actors before applying edit-mode
            // visibility so leave() can restore the new result correctly.
            snapshotIfReplaced(QStringLiteral("mesh.imported.surface"), m_surface);
            snapshotIfReplaced(QStringLiteral("mesh.imported.line"), m_line);
            snapshotIfReplaced(QStringLiteral("mesh.boundarylayer.internalEdges"), m_boundaryLayer);
            snapshotIfReplaced(QStringLiteral("mesh.localTrial.preview"), m_trialPreview);
        }
        applyCurrentMode();
        syncHighlight();
        m_visual->render();
    }

    void setReferenceMeshVisible(bool on)
    {
        m_mode = on ? MeshDisplayMode::Reference : MeshDisplayMode::Hidden;
        if (!m_active || !m_visual)
            return;
        applyCurrentMode();
        m_visual->render();
    }

    bool referenceMeshVisible() const { return m_mode == MeshDisplayMode::Reference; }
    bool hasMeshResult() const { return hasCurrentMesh(); }
    bool active() const { return m_active; }

    void leave()
    {
        if (!m_active)
            return;

        if (m_visual)
        {
            if (m_selectionDefined)
                if (auto* geometry = ServiceRegistry::instance().getService<PreProcessingService>())
                    geometry->clearEntityHighlight();
            restore(QStringLiteral("mesh.imported.surface"), m_surface);
            restore(QStringLiteral("mesh.imported.line"), m_line);
            restore(QStringLiteral("mesh.boundarylayer.internalEdges"), m_boundaryLayer);
            restore(QStringLiteral("mesh.localTrial.preview"), m_trialPreview);

            // Normal result view shows the mesh and hides CAD. If the mesh was removed
            // while editing, keep CAD visible instead.
            m_visual->setGroupVisible(QStringLiteral("Geometry"), !hasCurrentMesh(), false);
            m_visual->render();
        }

        m_active = false;
        m_visual = nullptr;
        m_hadMesh = false;
        m_mode = MeshDisplayMode::Hidden;
        m_surface = ActorState{};
        m_line = ActorState{};
        m_boundaryLayer = ActorState{};
        m_trialPreview = ActorState{};
    }

private:
    struct ActorState
    {
        vtkActor* actor = nullptr;
        int visible = 0;
        int pickable = 1;
        double opacity = 1.0;
        int edgeVisibility = 0;
        bool valid = false;
    };

    static bool hasCurrentMesh()
    {
        auto* meshSvc = ServiceRegistry::instance().getService<MeshModuleControllerService>();
        vtkUnstructuredGrid* grid = meshSvc ? meshSvc->getVtkUnstructuredGrid() : nullptr;
        if (grid && grid->GetNumberOfCells() > 0)
            return true;
        auto* visual = ServiceRegistry::instance().getService<VisualWidgetService>();
        return visual && visual->getProp(QStringLiteral("mesh.localTrial.preview")) != nullptr;
    }

    void snapshot(const QString& propId, ActorState& out)
    {
        out = ActorState{};
        if (!m_visual)
            return;
        auto* actor = vtkActor::SafeDownCast(m_visual->getProp(propId));
        if (!actor)
            return;
        out.actor = actor;
        out.visible = actor->GetVisibility();
        out.pickable = actor->GetPickable();
        if (auto* p = actor->GetProperty())
        {
            out.opacity = p->GetOpacity();
            out.edgeVisibility = p->GetEdgeVisibility();
        }
        out.valid = true;
    }


    void snapshotIfReplaced(const QString& propId, ActorState& out)
    {
        if (!m_visual)
            return;
        auto* actor = vtkActor::SafeDownCast(m_visual->getProp(propId));
        if (!actor || (out.valid && out.actor == actor))
            return;
        snapshot(propId, out);
    }

    void applyCurrentMode()
    {
        if (!m_visual)
            return;

        m_visual->setGroupVisible(QStringLiteral("Geometry"), showingGeometry() || !m_hadMesh, false);
        if (!m_hadMesh) return;

        applyLiveMode(QStringLiteral("mesh.imported.surface"), 0.16);
        applyLiveMode(QStringLiteral("mesh.imported.line"), 0.12);
        applyLiveMode(QStringLiteral("mesh.boundarylayer.internalEdges"), 0.12);
        applyLiveMode(QStringLiteral("mesh.localTrial.preview"), 0.12);
    }

    void applyLiveMode(const QString& propId, double referenceOpacity)
    {
        if (!m_visual)
            return;
        if (auto* actor = vtkActor::SafeDownCast(m_visual->getProp(propId)))
        {
            actor->SetPickable(false);
            if (m_mode == MeshDisplayMode::Hidden)
            {
                actor->SetVisibility(false);
                return;
            }

            actor->SetVisibility(true);
            if (auto* p = actor->GetProperty())
            {
                if (m_mode == MeshDisplayMode::Reference) {
                    p->SetOpacity(referenceOpacity);
                    p->EdgeVisibilityOff();
                } else {
                    const ActorState* state = propId == QStringLiteral("mesh.imported.surface") ? &m_surface :
                        propId == QStringLiteral("mesh.imported.line") ? &m_line :
                        propId == QStringLiteral("mesh.boundarylayer.internalEdges") ? &m_boundaryLayer : &m_trialPreview;
                    p->SetOpacity(state->valid ? state->opacity : 1.0);
                    p->SetEdgeVisibility(state->valid ? state->edgeVisibility : 0);
                }
            }
        }
    }

    void syncHighlight()
    {
        if (!m_active || !m_selectionDefined) return;
        if (auto* geometry = ServiceRegistry::instance().getService<PreProcessingService>()) {
            if (showingGeometry() && !m_selectedIds.isEmpty())
                geometry->highlightEntities(m_selectionDimension, m_selectedIds, false);
            else
                geometry->clearEntityHighlight();
        }
    }

    void restore(const QString& propId, const ActorState& s)
    {
        if (!m_visual || !s.valid || !s.actor)
            return;

        // displayCurrentGrid() may replace actors while a control page is open.
        // Never restore state onto an actor that is no longer the live prop.
        auto* current = vtkActor::SafeDownCast(m_visual->getProp(propId));
        if (current != s.actor)
            return;

        current->SetVisibility(s.visible);
        current->SetPickable(s.pickable);
        if (auto* p = current->GetProperty())
        {
            p->SetOpacity(s.opacity);
            p->SetEdgeVisibility(s.edgeVisibility);
        }
    }

private:
    VisualWidgetService* m_visual = nullptr;
    bool m_active = false;
    bool m_hadMesh = false;
    MeshDisplayMode m_mode = MeshDisplayMode::Hidden;
    ActorState m_surface;
    ActorState m_line;
    ActorState m_boundaryLayer;
    ActorState m_trialPreview;
    QPointer<QComboBox> m_selector;
    bool m_selectionDefined = false;
    int m_selectionDimension = 3;
    QList<int> m_selectedIds;
};
