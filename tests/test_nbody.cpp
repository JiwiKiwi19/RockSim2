// Gravitational N-body: analytic two-body period, conservation laws and the
// figure-eight periodic orbit.
//
// What these establish: the force law, its potential and the integrators
// reproduce known orbits and conserve energy, momentum and angular momentum.
// What they do not establish: accuracy through close encounters (softening
// changes the physics there) or performance beyond direct O(N^2) summation.
#include <gtest/gtest.h>

#include <cmath>
#include <numbers>

#include "helpers.hpp"

using namespace rocksim;

namespace {

Simulation scenarioSim(const char* id, ParamValues p, IntegratorKind kind, double dt, double* period = nullptr) {
    BuiltScenario s;
    std::vector<std::string> errs;
    EXPECT_TRUE(buildScenario(id, p, s, errs)) << (errs.empty() ? "" : errs.front());
    if (period) *period = s.referencePeriod;
    return test::makeSim(s.world, kind, dt);
}

/// Time at which the relative vector r2 - r1 completes its first revolution,
/// found by linear interpolation of the angle crossing back through 0.
double measuredPeriod(Simulation& sim, double maxTime) {
    auto rel = [&] {
        const Bodies& b = sim.world().bodies;
        return Vec2{b.x[1] - b.x[0], b.y[1] - b.y[0]};
    };
    double prevY = rel().y;
    bool wentNegative = false;
    while (sim.time() < maxTime) {
        const double t0 = sim.time();
        sim.step();
        const Vec2 r = rel();
        if (r.y < 0) wentNegative = true;
        if (wentNegative && prevY < 0 && r.y >= 0 && r.x > 0) {
            return t0 + sim.dt() * (-prevY) / (r.y - prevY);
        }
        prevY = r.y;
    }
    return -1.0;
}

}  // namespace

TEST(Kepler, PeriodMatchesKeplersThirdLaw) {
    for (auto kind : {IntegratorKind::VelocityVerlet, IntegratorKind::RK4}) {
        double T = 0;
        Simulation sim = scenarioSim("kepler", {{"eccentricity", 0.6}, {"massRatio", 0.2}}, kind, 5e-4, &T);
        EXPECT_NEAR(T, 2 * std::numbers::pi / std::sqrt(1.2), 1e-14);
        const double measured = measuredPeriod(sim, 2 * T);
        EXPECT_NEAR(measured / T, 1.0, 1e-5) << integratorInfo(kind).name;
    }
}

TEST(Kepler, ConservationOverTenOrbits) {
    double T = 0;
    Simulation sim = scenarioSim("kepler", {{"eccentricity", 0.6}, {"massRatio", 0.2}}, IntegratorKind::VelocityVerlet, 1e-3, &T);
    const Invariants i0 = sim.diagnostics().now;
    double maxEnergyErr = 0;
    while (sim.time() < 10 * T) {
        sim.step();
        if (sim.steps() % 50 == 0) maxEnergyErr = std::max(maxEnergyErr, std::abs(sim.diagnostics().relativeEnergyError));
    }
    const Invariants i1 = sim.diagnostics().now;
    EXPECT_LT(maxEnergyErr, 1e-4);                                    // bounded, oscillating error
    EXPECT_NEAR(i1.angularMomentum, i0.angularMomentum, 1e-12);       // exact for central forces under Verlet
    EXPECT_NEAR(i1.momentum.x, 0.0, 1e-13);
    EXPECT_NEAR(i1.momentum.y, 0.0, 1e-13);
}

TEST(Kepler, ExplicitEulerSpiralsOut) {
    // Energy error of explicit Euler grows secularly: after a few orbits the
    // orbit has visibly gained energy. This is why it is only a baseline.
    double T = 0;
    Simulation sim = scenarioSim("kepler", {{"eccentricity", 0.6}, {"massRatio", 0.2}}, IntegratorKind::ExplicitEuler, 1e-3, &T);
    while (sim.time() < 3 * T && sim.status() == SimStatus::Ok) sim.step();
    EXPECT_GT(sim.diagnostics().relativeEnergyError, 0.01);
}

TEST(FigureEight, ReturnsToInitialStateAfterOnePeriod) {
    // Simo's published initial conditions carry 8 significant digits, which
    // bounds how closely the orbit can close.
    const double T = analytic::kFigureEightPeriod;
    const int steps = 6400;
    Simulation sim = scenarioSim("figure-eight", {}, IntegratorKind::RK4, T / steps);
    const Bodies start = sim.world().bodies;
    sim.stepN(steps);
    const Bodies& end = sim.world().bodies;
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(end.x[i], start.x[i], 2e-5);
        EXPECT_NEAR(end.y[i], start.y[i], 2e-5);
    }
    EXPECT_NEAR(sim.diagnostics().now.momentum.x, 0.0, 1e-13);
    EXPECT_NEAR(std::abs(sim.diagnostics().relativeEnergyError), 0.0, 1e-9);
}

TEST(NBody, ThirdLawConservesMomentumInADisk) {
    Simulation sim = scenarioSim("orbital-disk", {{"count", 200}}, IntegratorKind::VelocityVerlet, 2e-3);
    for (int i = 0; i < 300; ++i) sim.step();
    EXPECT_NEAR(sim.diagnostics().now.momentum.x, 0.0, 1e-13);
    EXPECT_NEAR(sim.diagnostics().now.momentum.y, 0.0, 1e-13);
}

TEST(NBody, SofteningMatchesPotentialGradient) {
    // Finite-difference check that the force is -grad U for the softened kernel.
    World w;
    w.params.nbody = true;
    w.params.G = 1.0;
    w.params.softening = 0.1;
    w.bodies.add({0.0, 0.0}, {}, 2.0, 0.0);
    w.bodies.add({0.3, 0.2}, {}, 0.5, 0.0);
    std::vector<double> ax(2), ay(2);
    computeAccelerations(w, w.bodies.x, w.bodies.y, w.bodies.vx, w.bodies.vy, ax, ay);
    const double h = 1e-6;
    World wp = w, wm = w;
    wp.bodies.x[1] += h;
    wm.bodies.x[1] -= h;
    const double dUdx = (computeInvariants(wp).potential - computeInvariants(wm).potential) / (2 * h);
    EXPECT_NEAR(ax[1], -dUdx / w.bodies.mass[1], 1e-7);
}
