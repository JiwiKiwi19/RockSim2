// Simulation orchestration: validation, reproducibility, the fixed-step
// accumulator, instability detection, scenarios and the RNG.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <limits>

#include "helpers.hpp"

using namespace rocksim;

namespace {
bool anyContains(const std::vector<std::string>& v, const char* needle) {
    for (const auto& s : v)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}
}  // namespace

TEST(Validation, RejectsBadBodiesAndParameters) {
    World w;
    w.bodies.add({0, 0}, {}, -1.0, 0.1);                                        // negative mass
    w.bodies.add({std::numeric_limits<double>::quiet_NaN(), 0}, {}, 1.0, 0.1);  // NaN position
    w.bodies.add({0, 0}, {}, 1.0, -0.1);                                        // negative radius
    w.springs.push_back({1, 1, 10.0, 1.0, 0.0});                                // self-loop
    w.springs.push_back({0, 7, 10.0, 1.0, 0.0});                                // out of range
    w.params.restitution = 1.5;
    std::vector<std::string> errs;
    Simulation sim = Simulation::create(w, IntegratorKind::RK4, 0.0, errs);
    EXPECT_FALSE(sim.valid());
    EXPECT_TRUE(anyContains(errs, "mass must be a positive"));
    EXPECT_TRUE(anyContains(errs, "must be finite"));
    EXPECT_TRUE(anyContains(errs, "radius must be >= 0"));
    EXPECT_TRUE(anyContains(errs, "two different existing bodies"));
    EXPECT_TRUE(anyContains(errs, "restitution must be in [0, 1]"));
    EXPECT_TRUE(anyContains(errs, "time step dt must be a positive"));
    sim.step();  // an invalid simulation is inert, never UB
    EXPECT_EQ(sim.steps(), 0u);
}

TEST(Validation, BodyOutsideBox) {
    World w;
    w.params.walls = true;
    w.bodies.add({0.995, 0}, {}, 1.0, 0.01);
    EXPECT_TRUE(anyContains(validateWorld(w), "outside the box"));
}

TEST(Validation, ScenarioParameterRanges) {
    BuiltScenario s;
    std::vector<std::string> errs;
    EXPECT_FALSE(buildScenario("particle-box", {{"count", 2.5}}, s, errs));
    EXPECT_TRUE(anyContains(errs, "whole number"));
    EXPECT_FALSE(buildScenario("particle-box", {{"restitution", 1.2}}, s, errs));
    EXPECT_TRUE(anyContains(errs, "between 0 and 1"));
    EXPECT_FALSE(buildScenario("particle-box", {{"bogus", 1}}, s, errs));
    EXPECT_TRUE(anyContains(errs, "Unknown parameter"));
    EXPECT_FALSE(buildScenario("no-such-scenario", {}, s, errs));
    EXPECT_FALSE(buildScenario("particle-box", {{"count", 3000}, {"radius", 0.05}}, s, errs));
    EXPECT_TRUE(anyContains(errs, "cannot be placed"));
}

TEST(Validation, LiveParamEditsAreValidatedAndRolledBack) {
    BuiltScenario s;
    std::vector<std::string> errs;
    ASSERT_TRUE(buildScenario("particle-box", {}, s, errs));
    Simulation sim = test::makeSim(s.world, IntegratorKind::VelocityVerlet, 1e-3);
    EXPECT_TRUE(applyLiveParam("particle-box", "gravity", 9.81, sim.mutableWorld()));
    EXPECT_TRUE(sim.paramsChanged().empty());
    EXPECT_DOUBLE_EQ(sim.world().params.gravity.y, -9.81);
    EXPECT_FALSE(applyLiveParam("particle-box", "count", 10, sim.mutableWorld()));       // not live
    EXPECT_FALSE(applyLiveParam("particle-box", "restitution", 1.5, sim.mutableWorld()));  // out of range
    sim.mutableWorld().params.restitution = -2.0;  // bypassing the helper
    EXPECT_FALSE(sim.paramsChanged().empty());
    EXPECT_DOUBLE_EQ(sim.world().params.restitution, 1.0);  // restored
}

TEST(Scenarios, EveryScenarioBuildsAndRunsWithDefaults) {
    for (const ScenarioSpec& spec : scenarioCatalog()) {
        BuiltScenario s;
        std::vector<std::string> errs;
        ASSERT_TRUE(buildScenario(spec.id, {}, s, errs)) << spec.id << ": " << (errs.empty() ? "" : errs.front());
        Simulation sim = test::makeSim(s.world, spec.defaultIntegrator, spec.defaultDt);
        ASSERT_TRUE(sim.valid()) << spec.id;
        sim.stepN(500);
        EXPECT_EQ(sim.status(), SimStatus::Ok) << spec.id << ": " << sim.statusMessage();
        for (const auto& p : spec.params) {
            EXPECT_LE(p.min, p.defaultValue) << spec.id << "." << p.key;
            EXPECT_LE(p.defaultValue, p.max) << spec.id << "." << p.key;
        }
    }
}

// Audit finding E1: the requested body count must be honoured.
TEST(Scenarios, RegressionE1_ParticleCountIsHonoured) {
    BuiltScenario s;
    std::vector<std::string> errs;
    ASSERT_TRUE(buildScenario("particle-box", {{"count", 1234}, {"radius", 0.01}}, s, errs));
    EXPECT_EQ(s.world.bodies.size(), 1234u);
}

TEST(Reproducibility, SameSeedGivesBitIdenticalRuns) {
    auto run = [](double seed) {
        BuiltScenario s;
        std::vector<std::string> errs;
        EXPECT_TRUE(buildScenario("particle-box", {{"seed", seed}, {"gravity", 9.81}, {"restitution", 0.9}}, s, errs));
        Simulation sim = test::makeSim(s.world, IntegratorKind::VelocityVerlet, 1e-3);
        sim.stepN(800);
        return sim.world().bodies;
    };
    const Bodies a = run(11), b = run(11), c = run(12);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(0, std::memcmp(a.x.data(), b.x.data(), a.size() * sizeof(double)));
    EXPECT_EQ(0, std::memcmp(a.vy.data(), b.vy.data(), a.size() * sizeof(double)));
    EXPECT_NE(a.x, c.x);
}

TEST(Accumulator, TakesWholeStepsAndCarriesRemainder) {
    // dt = 0.25 is exact in binary, so the arithmetic below is exact.
    Simulation sim = test::makeSim(test::oscillatorWorld(1, 1, 0, 0.5), IntegratorKind::VelocityVerlet, 0.25);
    EXPECT_EQ(sim.advance(0.6, 100), 2);  // 0.1 left over
    EXPECT_EQ(sim.advance(0.2, 100), 1);  // 0.3 >= 0.25
    EXPECT_DOUBLE_EQ(sim.time(), 0.75);
    EXPECT_EQ(sim.advance(10.0, 5), 5);   // capped: backlog dropped, not queued
    EXPECT_GT(sim.droppedTime(), 0.0);
    EXPECT_EQ(sim.advance(0.1, 100), 0);
}

TEST(Stability, StiffSoftBodyWithExplicitEulerIsDetected) {
    BuiltScenario s;
    std::vector<std::string> errs;
    ASSERT_TRUE(buildScenario("soft-body", {{"stiffness", 3000}}, s, errs));
    Simulation sim = test::makeSim(s.world, IntegratorKind::ExplicitEuler, 2e-3);
    sim.stepN(20000);
    ASSERT_EQ(sim.status(), SimStatus::Unstable);
    EXPECT_FALSE(sim.statusMessage().empty());
    const auto stepsAtStop = sim.steps();
    sim.step();
    EXPECT_EQ(sim.steps(), stepsAtStop);  // halted, no further corruption

    // The same scene is stable with Verlet at the same dt.
    Simulation verlet = test::makeSim(s.world, IntegratorKind::VelocityVerlet, 2e-3);
    verlet.stepN(5000);
    EXPECT_EQ(verlet.status(), SimStatus::Ok);
}

TEST(Rng, MatchesPcg32ReferenceOutput) {
    // First outputs of the reference pcg32-demo for pcg32_srandom(42u, 54u).
    Pcg32 rng(42u, 54u);
    const std::uint32_t expected[] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (std::uint32_t e : expected) EXPECT_EQ(rng.next_u32(), e);
}

TEST(Rng, UniformIsInRangeAndUnbiased) {
    Pcg32 rng(2024);
    double sum = 0;
    const int n = 200000;
    for (int i = 0; i < n; ++i) {
        const double u = rng.uniform(-2.0, 3.0);
        ASSERT_GE(u, -2.0);
        ASSERT_LT(u, 3.0);
        sum += u;
    }
    EXPECT_NEAR(sum / n, 0.5, 0.02);  // mean of U(-2, 3); 5-sigma bound is ~0.016
}

// Cross-platform reproducibility. These hashes were produced natively (GCC 13,
// x86-64 Linux) and by the WebAssembly build (Emscripten 3.1.6, Clang 15) and
// are identical. The engine uses only correctly rounded IEEE 754 operations on
// state (no hypot/sin/cos/cbrt from libm) and is compiled with
// -ffp-contract=off, so any conforming platform should reproduce them. A
// failure here means some platform-dependent rounding entered the state path.
TEST(Reproducibility, GoldenStateHashesAfter3000Steps) {
    const std::pair<const char*, std::uint64_t> golden[] = {
        {"particle-box", 0x0c4e6de3f05f6f05ULL}, {"spring-oscillator", 0x71c4b76808506524ULL},
        {"soft-body", 0x9fff10aa461cc357ULL},    {"kepler", 0x9e500ed27738bb6aULL},
        {"figure-eight", 0x1d96b7c3237da6bbULL}, {"orbital-disk", 0x80623132be0d8a69ULL},
    };
    for (const auto& [id, expected] : golden) {
        const ScenarioSpec* spec = findScenario(id);
        ASSERT_NE(spec, nullptr);
        BuiltScenario s;
        std::vector<std::string> errs;
        ASSERT_TRUE(buildScenario(id, {}, s, errs));
        Simulation sim = test::makeSim(s.world, spec->defaultIntegrator, spec->defaultDt);
        sim.stepN(3000);
        EXPECT_EQ(stateHash(sim.world().bodies), expected) << id;
    }
}
