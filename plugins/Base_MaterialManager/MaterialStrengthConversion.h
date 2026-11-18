#pragma once
// Source-compatible implementation of SOLVER (2026-10-19):
// CAE07.f90 SEARCH_FAI / PARA_HBTOMCN / GEHBTABLE and A49_HBTOMC.
// These are the *solver's* discrete approximation rules, not a new constitutive law.
#include <array>
#include <cmath>
#include <string>

namespace MaterialStrengthConversion
{
struct HoekBrownEquivalent
{
    double uniaxialCompressivePa = 0.0;
    double tensilePa = 0.0;
    double cohesionPa = 0.0;
    std::array<double, 10> frictionAnglesDeg{};
};

inline bool hoekBrown(double mb, double s, double a, double sigmaCiMpa,
                      HoekBrownEquivalent* result, std::string* error = nullptr)
{
    auto fail = [&](const char* message) {
        if (error) *error = message;
        return false;
    };
    if (!result || !std::isfinite(mb) || !std::isfinite(s) || !std::isfinite(a) ||
        !std::isfinite(sigmaCiMpa) || mb <= 0.0 || s <= 0.0 || a <= 0.0 ||
        a > 1.0 || sigmaCiMpa <= 0.0)
        return fail("mb, s and sigmaCi must be positive, with 0 < a <= 1");

    constexpr double pi = 3.14159265358979323846;
    constexpr double stepDeg = 0.01;
    constexpr double pressureSamples[10] = {0, 10, 20, 30, 40, 50, 100, 150, 200, 300};
    std::array<double, 41> centers{};
    std::array<double, 41> angles{};
    const double cohesionMpa = sigmaCiMpa * std::pow(s, a) / 2.0;
    if (!std::isfinite(cohesionMpa)) return fail("invalid derived cohesion");

    // Fortran PARA_HBTOMCN uses confining pressures 0,2,...80 MPa.
    for (int j = 0; j < 41; ++j)
    {
        const double sigma3 = 2.0 * j;
        const double powerBase = mb * sigma3 / sigmaCiMpa + s;
        const double radius = 0.5 * sigmaCiMpa * std::pow(powerBase, a);
        const double sigma1 = sigma3 + 2.0 * radius;
        centers[j] = (sigma1 + sigma3) * 0.5;
        if (!std::isfinite(centers[j]) || !std::isfinite(radius))
            return fail("invalid Hoek-Brown envelope");
        double bestErr = 1.0e9;
        double bestPhi = 0.0;
        for (int k = 1; k <= 9000; ++k)
        {
            const double phi = k * stepDeg;
            const double radians = phi * pi / 180.0;
            const double resistance = std::sin(radians) * centers[j] +
                                      cohesionMpa * std::cos(radians);
            const double residual = std::fabs(radius - resistance);
            if (residual < bestErr)
            {
                bestErr = residual;
                bestPhi = phi;
            }
        }
        angles[j] = bestPhi;
    }
    if (std::fabs(angles[0] - 90.0) < 0.1)
        angles[0] = angles[1];

    // GEACCE clamps outside the tabulated stress range, interpolates within it.
    for (int i = 0; i < 10; ++i)
    {
        const double p = pressureSamples[i];
        double phi = angles.back();
        if (p <= centers.front()) phi = angles.front();
        else if (p < centers.back())
        {
            for (int j = 1; j < 41; ++j)
            {
                if (p <= centers[j])
                {
                    if (std::fabs(p - centers[j]) <= 1.0e-5) phi = angles[j];
                    else if (std::fabs(centers[j] - centers[j-1]) <= 1.0e-6)
                        return fail("degenerate Hoek-Brown stress samples");
                    else phi = angles[j-1] + (angles[j] - angles[j-1]) *
                                   (p - centers[j-1]) / (centers[j] - centers[j-1]);
                    break;
                }
            }
        }
        result->frictionAnglesDeg[i] = phi;
    }
    result->uniaxialCompressivePa = sigmaCiMpa * 1.0e6;
    result->tensilePa = std::fabs(s / mb * sigmaCiMpa) * 1.0e6;
    result->cohesionPa = cohesionMpa * 1.0e6;
    return true;
}
} // namespace MaterialStrengthConversion
