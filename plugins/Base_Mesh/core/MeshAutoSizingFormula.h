#pragma once
#include <algorithm>
#include <cmath>

namespace MeshAutoSizingFormula {
enum class Density { Few, Normal, Fine };
struct Sizes { double maximum = 0.0; double minimum = 0.0; int fineness = 2; };

// Geometry scale and robust short-edge size are expressed in the same OCC
// coordinate unit. This is a reproducible initial guess, not a cell-count target.
inline Sizes infer(double extent, double feature, Density level)
{
    Sizes p;
    if (!std::isfinite(extent) || extent <= 0.0) return p;
    const double fraction = level == Density::Few ? 0.80
        : level == Density::Fine ? 0.20 : 0.40;
    p.maximum = extent * fraction;
    const double candidate = std::isfinite(feature) && feature > 0.0
        ? feature * 0.22 : extent * 0.002;
    p.minimum = std::clamp(candidate,
        extent * 1.0e-5, std::min(extent * 0.01, p.maximum * 0.25));
    p.fineness = level == Density::Few ? 1 : level == Density::Fine ? 3 : 2;
    return p;
}
}
