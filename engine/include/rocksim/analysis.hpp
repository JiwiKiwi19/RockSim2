#pragma once

#include <vector>

#include "rocksim/integrators.hpp"

namespace rocksim::analysis {

/// One point of a convergence study.
struct ConvergencePoint {
    double dt = 0.0;
    long long steps = 0;
    double error = 0.0;  ///< phase-space error sqrt(dx^2 + (dv/omega)^2) at t_end
    long long forceEvaluations = 0;
};

struct ConvergenceResult {
    IntegratorKind integrator{};
    std::vector<ConvergencePoint> points;
    /// Least-squares slope of log(error) vs log(dt), using points above the
    /// round-off floor (error > 1e-12). Theory: 1, 1, 2, 4.
    double observedOrder = 0.0;
};

/// Integrates the oscillator m = k = 1 (omega = 1), x0 = 1, v0 = 0, with
/// dashpot `damping`, to t_end = 5.12 for dt = 0.08 / 2^j, j = 0..levels-1,
/// and compares with the closed-form solution.
ConvergenceResult convergenceStudy(IntegratorKind kind, double damping, int levels);

struct DriftSample {
    double t = 0.0;
    double relativeEnergyError = 0.0;  ///< (E - E0) / |E0|
};

/// Two-body Kepler orbit (eccentricity e, mass ratio 0.2, G = a = 1) run for
/// `orbits` periods at a fixed dt; returns `samples` evenly spaced energy-error
/// samples. Shows bounded (symplectic) versus drifting (non-symplectic) error.
std::vector<DriftSample> energyDriftStudy(IntegratorKind kind, double dt, double eccentricity, double orbits,
                                          int samples);

}  // namespace rocksim::analysis
