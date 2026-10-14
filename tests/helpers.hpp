#pragma once

#include <cmath>
#include <vector>

#include "rocksim/rocksim.hpp"

namespace rocksim::test {

/// Undamped/damped 1-D oscillator: anchor (fixed) at x = -3, body at x0, rest
/// length 3, so body 1's x coordinate is the displacement from equilibrium.
/// The anchor is far outside the motion: if the spring length could reach
/// zero, the 2-D spring's direction would flip and it would no longer be the
/// linear 1-D oscillator the analytic solution describes.
inline World oscillatorWorld(double m, double k, double c, double x0, double v0 = 0.0) {
    World w;
    w.bodies.add({-3.0, 0.0}, {}, 1.0, 0.0, true);
    w.bodies.add({x0, 0.0}, {v0, 0.0}, m, 0.0);
    w.springs.push_back({0, 1, k, 3.0, c});
    return w;
}

inline Simulation makeSim(World w, IntegratorKind k, double dt) {
    std::vector<std::string> errs;
    Simulation s = Simulation::create(std::move(w), k, dt, errs);
    return s;
}

/// Least-squares slope of log(err) against log(dt): the observed order of convergence.
inline double fittedOrder(const std::vector<double>& dts, const std::vector<double>& errs) {
    const std::size_t n = dts.size();
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double x = std::log(dts[i]), y = std::log(errs[i]);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    return (n * sxy - sx * sy) / (n * sxx - sx * sx);
}

}  // namespace rocksim::test
