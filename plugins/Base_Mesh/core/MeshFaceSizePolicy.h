#pragma once
// Mesh face size rule storage contract (compatible with the existing service ABI):
//   positive value  -> legacy ratio of the parent Mesh maximum size;
//   negative value  -> absolute CAD-coordinate target size (new projects).
// The project serializer also writes size_mode/target_size so the on-disk
// semantics are explicit.  Never multiply an absolute size by sizeFactor.
// This small header deliberately has no Qt, OCC, or SMESH dependencies.
#include <cmath>

struct MeshFaceSizePolicy final
{
    static bool absolute(double encoded) noexcept
    {
        return std::isfinite(encoded) && encoded < 0.0;
    }
    static double encodeAbsolute(double target) noexcept
    {
        return (std::isfinite(target) && target > 0.0) ? -target : 0.0;
    }
    static bool valid(double encoded) noexcept
    {
        return std::isfinite(encoded) && encoded != 0.0;
    }
    // Parent maxSize is the unscaled UI size; scale applies only to old ratios.
    static double target(double encoded, double parentMax, double scale = 1.0) noexcept
    {
        return absolute(encoded) ? -encoded : parentMax * scale * encoded;
    }
    static double ratio(double encoded, double parentMax, double scale = 1.0) noexcept
    {
        const double max = parentMax * scale;
        return std::isfinite(max) && max > 0.0 ? target(encoded, parentMax, scale) / max : 0.0;
    }
};
