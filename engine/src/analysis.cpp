#include "rocksim/analysis.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "rocksim/analytic.hpp"
#include "rocksim/scenarios.hpp"
#include "rocksim/simulation.hpp"

namespace rocksim::analysis {

ConvergenceResult convergenceStudy(IntegratorKind kind, double damping, int levels) {
    ConvergenceResult res;
    res.integrator = kind;
    const double tEnd = 5.12;  // divisible by every dt = 0.08 / 2^j
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int j = 0; j < levels; ++j) {
        const double dt = 0.08 / std::pow(2.0, j);
        // Anchor far away (x = -3, rest length 3) so the spring never passes
        // through zero length and the system stays the linear 1-D oscillator.
        World w;
        w.bodies.add({-3.0, 0.0}, {}, 1.0, 0.0, true);
        w.bodies.add({1.0, 0.0}, {}, 1.0, 0.0);
        w.springs.push_back({0, 1, 1.0, 3.0, damping});
        std::vector<std::string> errs;
        Simulation sim = Simulation::create(std::move(w), kind, dt, errs);
        const long long steps = std::llround(tEnd / dt);
        for (long long s = 0; s < steps; ++s) sim.step();
        const auto ex = analytic::dampedOscillator(1.0, 1.0, damping, 1.0, 0.0, static_cast<double>(steps) * dt);
        ConvergencePoint p;
        p.dt = dt;
        p.steps = steps;
        p.error = std::hypot(sim.world().bodies.x[1] - ex.x, sim.world().bodies.vx[1] - ex.v);
        p.forceEvaluations = sim.forceEvaluations();
        res.points.push_back(p);
        if (p.error > 1e-12 && std::isfinite(p.error)) {
            const double lx = std::log(dt), ly = std::log(p.error);
            sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly; ++n;
        }
    }
    if (n >= 2) res.observedOrder = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    return res;
}

std::vector<DriftSample> energyDriftStudy(IntegratorKind kind, double dt, double eccentricity, double orbits,
                                          int samples) {
    std::vector<DriftSample> out;
    BuiltScenario s;
    std::vector<std::string> errs;
    if (!buildScenario("kepler", {{"eccentricity", eccentricity}, {"massRatio", 0.2}}, s, errs)) return out;
    const double T = s.referencePeriod;
    Simulation sim = Simulation::create(std::move(s.world), kind, dt, errs);
    if (!sim.valid() || samples < 1) return out;
    const double e0 = sim.diagnostics().now.total();
    const long long totalSteps = std::llround(orbits * T / dt);
    const long long every = std::max<long long>(1, totalSteps / samples);
    out.push_back({0.0, 0.0});
    for (long long k = 1; k <= totalSteps; ++k) {
        sim.step();
        if (sim.status() != SimStatus::Ok) break;
        if (k % every == 0) out.push_back({sim.time(), (sim.diagnostics().now.total() - e0) / std::abs(e0)});
    }
    return out;
}

}  // namespace rocksim::analysis
