#pragma once
// Normalized native NETGEN 3D policy for trelau/SMESH (standalone SMESH).
// Keep this independent of Qt/OCC/NETGEN headers so the inheritance rule can
// be regression-tested without the Windows meshing runtime.
// Exact equality is intentional: policy comparison determines whether an
// explicit Solid hypothesis can be safely omitted, not visual equivalence.

struct MeshNativeDomainPolicy final
{
    double maximum = 0.0;
    double minimum = 0.0;
    int fineness = 0;
    bool curvature = false;
    bool optimize = true;
    double growthRate = 0.0;
    double segmentsPerEdge = 0.0;
    double segmentsPerRadius = 0.0;

    static bool inheritsParent(const MeshNativeDomainPolicy& local,
                               const MeshNativeDomainPolicy& parent,
                               bool customFineness) noexcept
    {
        return local.maximum == parent.maximum &&
               local.minimum == parent.minimum &&
               local.fineness == parent.fineness &&
               local.curvature == parent.curvature &&
               local.optimize == parent.optimize &&
               (!customFineness ||
                (local.growthRate == parent.growthRate &&
                 local.segmentsPerEdge == parent.segmentsPerEdge &&
                 local.segmentsPerRadius == parent.segmentsPerRadius));
    }
};
