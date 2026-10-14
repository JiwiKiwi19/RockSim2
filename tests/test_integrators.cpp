// Integrator accuracy, convergence order and long-run energy behaviour.
//
// What these establish: each method converges at its theoretical order on a
// problem with a known exact solution, and the symplectic methods keep energy
// bounded where explicit Euler drifts. What they do not establish: accuracy
// across contact events (contacts are applied after the step and reduce the
// effective order), or behaviour on chaotic problems.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "helpers.hpp"

using namespace rocksim;

namespace {

/// Phase-space error at t_end for the undamped oscillator m = k = 1, x0 = 1:
/// sqrt(dx^2 + (dv/omega)^2). Using both components avoids accidental zeros
/// of the position error alone.
double oscillatorError(IntegratorKind kind, double dt, double tEnd, double damping = 0.0) {
    Simulation sim = test::makeSim(test::oscillatorWorld(1.0, 1.0, damping, 1.0), kind, dt);
    const int steps = static_cast<int>(std::lround(tEnd / dt));
    sim.stepN(steps);
    const auto exact = analytic::dampedOscillator(1.0, 1.0, damping, 1.0, 0.0, steps * dt);
    const double dx = sim.world().bodies.x[1] - exact.x;
    const double dv = sim.world().bodies.vx[1] - exact.v;
    return std::hypot(dx, dv);
}

struct OrderCase {
    IntegratorKind kind;
    double expectedOrder;
    double coarsestDt;
};

class ConvergenceOrder : public ::testing::TestWithParam<OrderCase> {};

}  // namespace

TEST_P(ConvergenceOrder, MatchesTheory) {
    const OrderCase c = GetParam();
    std::vector<double> dts, errs;
    for (int level = 0; level < 5; ++level) {
        const double dt = c.coarsestDt / std::pow(2.0, level);
        dts.push_back(dt);
        errs.push_back(oscillatorError(c.kind, dt, 5.12));
    }
    const double order = test::fittedOrder(dts, errs);
    EXPECT_NEAR(order, c.expectedOrder, 0.15) << integratorInfo(c.kind).name << " observed order " << order;
    for (std::size_t i = 1; i < errs.size(); ++i) EXPECT_LT(errs[i], errs[i - 1]);
}

INSTANTIATE_TEST_SUITE_P(AllIntegrators, ConvergenceOrder,
                         ::testing::Values(OrderCase{IntegratorKind::ExplicitEuler, 1.0, 0.01},
                                           OrderCase{IntegratorKind::SymplecticEuler, 1.0, 0.01},
                                           OrderCase{IntegratorKind::VelocityVerlet, 2.0, 0.04},
                                           OrderCase{IntegratorKind::RK4, 4.0, 0.08}),
                         [](const ::testing::TestParamInfo<OrderCase>& p) {
                             switch (p.param.kind) {
                                 case IntegratorKind::ExplicitEuler: return std::string("ExplicitEuler");
                                 case IntegratorKind::SymplecticEuler: return std::string("SymplecticEuler");
                                 case IntegratorKind::VelocityVerlet: return std::string("VelocityVerlet");
                                 case IntegratorKind::RK4: return std::string("RK4");
                             }
                             return std::string("Unknown");
                         });

TEST(Integrators, DampedOscillatorMatchesClosedFormAllRegimes) {
    // Under-, critically and over-damped (c = 2 sqrt(km) = 2 is critical).
    for (double c : {0.3, 2.0, 5.0}) {
        EXPECT_LT(oscillatorError(IntegratorKind::RK4, 1e-3, 8.0, c), 1e-11) << "c = " << c;
        EXPECT_LT(oscillatorError(IntegratorKind::VelocityVerlet, 1e-3, 8.0, c), 1e-5) << "c = " << c;
    }
}

TEST(Integrators, VerletStaysSecondOrderWithDamping) {
    // Without the predictor-corrector pass for velocity-dependent forces this
    // measured ~1.0 (found while writing these tests).
    std::vector<double> dts, errs;
    for (int level = 0; level < 5; ++level) {
        const double dt = 0.04 / std::pow(2.0, level);
        dts.push_back(dt);
        errs.push_back(oscillatorError(IntegratorKind::VelocityVerlet, dt, 5.12, 0.3));
    }
    EXPECT_NEAR(test::fittedOrder(dts, errs), 2.0, 0.15);
}

TEST(Integrators, ConstantAccelerationIsExactForVerletAndRK4) {
    // Under uniform gravity x(t) = x0 + v0 t + g t^2 / 2 is quadratic; Verlet
    // and RK4 reproduce it to round-off at any dt, Euler does not.
    for (auto kind : {IntegratorKind::VelocityVerlet, IntegratorKind::RK4}) {
        World w;
        w.params.gravity = {0.0, -9.81};
        w.bodies.add({0.0, 10.0}, {3.0, 4.0}, 2.0, 0.0);
        Simulation sim = test::makeSim(w, kind, 0.1);
        sim.stepN(20);
        const double t = 2.0;
        EXPECT_NEAR(sim.world().bodies.y[0], 10.0 + 4.0 * t - 0.5 * 9.81 * t * t, 1e-12);
        EXPECT_NEAR(sim.world().bodies.vy[0], 4.0 - 9.81 * t, 1e-12);
        EXPECT_NEAR(sim.world().bodies.x[0], 3.0 * t, 1e-12);
    }
}

// Regression for audit finding E6: free fall must not depend on mass.
TEST(Integrators, RegressionE6_FreeFallIsMassIndependent) {
    World w;
    w.params.gravity = {0.0, -9.81};
    w.bodies.add({0.0, 0.0}, {}, 1.0, 0.0);
    w.bodies.add({1.0, 0.0}, {}, 10.0, 0.0);
    Simulation sim = test::makeSim(w, IntegratorKind::ExplicitEuler, 1e-3);
    sim.stepN(500);
    EXPECT_DOUBLE_EQ(sim.world().bodies.y[0], sim.world().bodies.y[1]);
}

TEST(Integrators, SymplecticMethodsKeepEnergyBoundedEulerDrifts) {
    // 100 000 steps of the undamped oscillator at omega dt = 0.05.
    auto relEnergyError = [](IntegratorKind kind, double& firstWindowMax, double& lastWindowMax) {
        Simulation sim = test::makeSim(test::oscillatorWorld(1.0, 1.0, 0.0, 1.0), kind, 0.05);
        const int n = 100000;
        firstWindowMax = lastWindowMax = 0.0;
        for (int i = 0; i < n; ++i) {
            sim.step();
            if (sim.status() != SimStatus::Ok) return;
            const double err = std::abs(sim.diagnostics().relativeEnergyError);
            if (i < n / 10) firstWindowMax = std::max(firstWindowMax, err);
            if (i >= n - n / 10) lastWindowMax = std::max(lastWindowMax, err);
        }
    };
    for (auto kind : {IntegratorKind::SymplecticEuler, IntegratorKind::VelocityVerlet}) {
        double first = 0, last = 0;
        relEnergyError(kind, first, last);
        EXPECT_LT(last, 1.05 * first + 1e-12) << integratorInfo(kind).name << " energy error must not grow";
        EXPECT_LT(last, 0.03);
    }
    // Explicit Euler: E_{n+1} = (1 + omega^2 dt^2) E_n exactly, so energy grows
    // geometrically; the simulation's instability detector stops it.
    Simulation euler = test::makeSim(test::oscillatorWorld(1.0, 1.0, 0.0, 1.0), IntegratorKind::ExplicitEuler, 0.05);
    euler.stepN(100000);
    EXPECT_EQ(euler.status(), SimStatus::Unstable);
}

TEST(Integrators, VerletReusesForceEvaluationWhenConservative) {
    Simulation sim = test::makeSim(test::oscillatorWorld(1.0, 1.0, 0.0, 1.0), IntegratorKind::VelocityVerlet, 0.01);
    sim.stepN(100);
    EXPECT_EQ(sim.forceEvaluations(), 101);  // one per step + the initial evaluation
    Simulation rk = test::makeSim(test::oscillatorWorld(1.0, 1.0, 0.0, 1.0), IntegratorKind::RK4, 0.01);
    rk.stepN(100);
    EXPECT_EQ(rk.forceEvaluations(), 400);
}

TEST(Integrators, ParseRoundTrip) {
    for (auto k : {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler, IntegratorKind::VelocityVerlet,
                   IntegratorKind::RK4}) {
        IntegratorKind out{};
        ASSERT_TRUE(parseIntegrator(integratorInfo(k).id, out));
        EXPECT_EQ(out, k);
    }
    IntegratorKind out{};
    EXPECT_FALSE(parseIntegrator("leapfrog-ish", out));
}
