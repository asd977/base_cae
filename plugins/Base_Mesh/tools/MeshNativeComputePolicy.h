#pragma once

// Routing rules for the standalone trelau/SMESH native Compute API.
// Deliberately independent of Qt, OCC and SMESH so every branch is testable.
// A normal complete parent Mesh Compute resolves 1D/2D/3D dependencies itself;
// only explicit preview/limits/ViscousLayers or legacy scoped incremental
// operations need the separate 2D preparation stage.
struct MeshNativeComputePolicy final
{
    bool selectedGroup = false;
    bool parentCompute = false;
    bool surfacePreflight = false;
    bool standardRoot = false;

    static MeshNativeComputePolicy resolve(bool partial,
                                         bool hasSurfaceScopes,
                                         bool surfaceOnly,
                                         bool limitedSurface,
                                         bool viscousLayers,
                                         int selectedCount,
                                         bool hasCompletedSubmeshes) noexcept
    {
        MeshNativeComputePolicy result;
        result.selectedGroup = partial && selectedCount > 1 && !hasCompletedSubmeshes &&
            (hasSurfaceScopes || surfaceOnly || limitedSurface || viscousLayers);
        result.parentCompute = !hasCompletedSubmeshes && (!partial || result.selectedGroup);
        result.standardRoot = result.parentCompute && !partial &&
            !surfaceOnly && !limitedSurface && !viscousLayers;
        result.surfacePreflight = surfaceOnly || limitedSurface || viscousLayers ||
            (hasSurfaceScopes && !result.standardRoot);
        return result;
    }
};
