#pragma once

#include <cmath>
#include <numbers>

namespace rocksim::analytic {

/// Exact solution of m x'' + c x' + k x = 0 with x(0) = x0, x'(0) = v0.
/// Handles the under-, critically and over-damped regimes.
struct OscillatorState { double x; double v; };

inline OscillatorState dampedOscillator(double m, double k, double c, double x0, double v0, double t) {
    const double w0 = std::sqrt(k / m);
    const double zeta = c / (2.0 * std::sqrt(k * m));
    if (zeta < 1.0 - 1e-12) {
        const double wd = w0 * std::sqrt(1.0 - zeta * zeta);
        const double s = zeta * w0;
        const double B = (v0 + s * x0) / wd;
        const double e = std::exp(-s * t);
        const double cs = std::cos(wd * t), sn = std::sin(wd * t);
        return {e * (x0 * cs + B * sn), e * (-s * (x0 * cs + B * sn) + (-x0 * wd * sn + B * wd * cs))};
    }
    if (zeta <= 1.0 + 1e-12) {
        const double B = v0 + w0 * x0;
        const double e = std::exp(-w0 * t);
        return {e * (x0 + B * t), e * (B - w0 * (x0 + B * t))};
    }
    const double r = w0 * std::sqrt(zeta * zeta - 1.0);
    const double r1 = -zeta * w0 + r, r2 = -zeta * w0 - r;
    const double A = (v0 - r2 * x0) / (r1 - r2);
    const double Bc = x0 - A;
    return {A * std::exp(r1 * t) + Bc * std::exp(r2 * t), A * r1 * std::exp(r1 * t) + Bc * r2 * std::exp(r2 * t)};
}

/// Period of a bound two-body orbit with semi-major axis a and total mass M (Kepler's third law).
inline double keplerPeriod(double G, double totalMass, double a) {
    return 2.0 * std::numbers::pi * std::sqrt(a * a * a / (G * totalMass));
}

/// Period of the Chenciner-Montgomery figure-eight choreography (G = m = 1),
/// with the initial conditions computed by C. Simo; see scenarios.cpp.
inline constexpr double kFigureEightPeriod = 6.32591398;

}  // namespace rocksim::analytic
