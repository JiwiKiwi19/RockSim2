// Contact detection and response.
//
// What these tests establish: the grid broad phase finds exactly the pairs an
// O(N^2) search finds; impulses match the closed-form 1-D collision for any
// masses and restitution; momentum and (for e = 1) kinetic energy are
// conserved. What they do not establish: that sequential pairwise impulses
// are physically right for simultaneous multi-body contact (they are an
// approximation), or that fast bodies cannot tunnel (detection is discrete).
#include <gtest/gtest.h>

#include <cmath>

#include "helpers.hpp"

using namespace rocksim;

namespace {

Bodies randomBodies(std::uint64_t seed, int n, double rmin, double rmax, double extent) {
    Pcg32 rng(seed);
    Bodies b;
    for (int i = 0; i < n; ++i)
        b.add({rng.uniform(-extent, extent), rng.uniform(-extent, extent)}, {}, 1.0, rng.uniform(rmin, rmax));
    return b;
}

World twoBodies(double m1, double v1, double m2, double v2, double e) {
    World w;
    w.params.collisions = true;
    w.params.restitution = e;
    // Overlapping by 1 mm and approaching along x.
    w.bodies.add({0.0, 0.0}, {v1, 0.0}, m1, 0.1);
    w.bodies.add({0.199, 0.0}, {v2, 0.0}, m2, 0.1);
    return w;
}

}  // namespace

TEST(BroadPhase, MatchesBruteForceOnRandomScenes) {
    UniformGrid grid;
    std::vector<IndexPair> fast, slow;
    for (std::uint64_t seed = 1; seed <= 60; ++seed) {
        const int n = static_cast<int>(seed * 5);
        // Mixed radii up to 40x apart, dense enough to produce many overlaps.
        Bodies b = randomBodies(seed, n, 0.002, 0.08, 1.0);
        grid.findOverlaps(b, fast);
        bruteForceOverlaps(b, slow);
        ASSERT_EQ(fast, slow) << "seed " << seed << ", n " << n;
    }
}

TEST(BroadPhase, HandlesClusteredAndFarFlungBodies) {
    UniformGrid grid;
    std::vector<IndexPair> fast, slow;
    Bodies b = randomBodies(99, 400, 0.01, 0.03, 0.2);  // tight cluster
    b.add({1e6, -1e6}, {}, 1.0, 0.02);                // one escaped body stretches the bounding box
    b.add({0.0, 0.0}, {}, 1.0, 0.02);
    b.add({0.0, 0.0}, {}, 1.0, 0.02);                  // coincident centres
    grid.findOverlaps(b, fast);
    bruteForceOverlaps(b, slow);
    EXPECT_EQ(fast, slow);
    EXPECT_FALSE(fast.empty());
}

TEST(BroadPhase, IgnoresPointMasses) {
    Bodies b;
    b.add({0, 0}, {}, 1, 0.0);
    b.add({0, 0}, {}, 1, 0.0);
    UniformGrid grid;
    std::vector<IndexPair> out;
    grid.findOverlaps(b, out);
    EXPECT_TRUE(out.empty());
}

// Regression for audit finding E2: two overlapping bodies, each alone in its
// cell. The original app skipped cells with fewer than two bodies.
TEST(BroadPhase, RegressionE2_LoneBodiesInAdjacentCells) {
    Bodies b;
    b.add({-0.006, 0.0}, {}, 1.0, 0.01);
    b.add({0.006, 0.0}, {}, 1.0, 0.01);
    for (int i = 0; i < 50; ++i) b.add({-0.9 + 0.036 * i, 0.9}, {}, 1.0, 0.01);  // spread out the grid
    UniformGrid grid;
    std::vector<IndexPair> out;
    grid.findOverlaps(b, out);
    ASSERT_FALSE(out.empty());
    EXPECT_EQ(out.front(), IndexPair(0, 1));
}

// Regression for audit finding E7: radii larger than half the cell size.
TEST(BroadPhase, RegressionE7_LargeBodiesTwoCellsApart) {
    Bodies b;
    b.add({0.0, 0.0}, {}, 100.0, 0.0464);
    b.add({0.08, 0.0}, {}, 100.0, 0.0464);
    for (int i = 0; i < 40; ++i) b.add({-0.9 + 0.045 * i, 0.9}, {}, 1.0, 0.01);
    UniformGrid grid;
    std::vector<IndexPair> fast, slow;
    grid.findOverlaps(b, fast);
    bruteForceOverlaps(b, slow);
    EXPECT_EQ(fast, slow);
    EXPECT_EQ(fast.front(), IndexPair(0, 1));
}

// Closed-form 1-D collision: with relative normal velocity reversed and
// scaled by e and momentum conserved,
//   v1' = ((m1 - e m2) v1 + (1 + e) m2 v2) / (m1 + m2)
//   v2' = ((m2 - e m1) v2 + (1 + e) m1 v1) / (m1 + m2)
// Also the regression for audit findings E4 (momentum) and E5 (one e).
class OneDimensionalCollision : public ::testing::TestWithParam<std::tuple<double, double, double>> {};

TEST_P(OneDimensionalCollision, MatchesClosedForm) {
    const auto [m1, m2, e] = GetParam();
    const double v1 = 1.0, v2 = -0.5;
    World w = twoBodies(m1, v1, m2, v2, e);
    UniformGrid grid;
    std::vector<IndexPair> scratch;
    const ContactStats st = resolveContacts(w, grid, scratch);
    EXPECT_EQ(st.impulses, 1u);
    const double M = m1 + m2;
    EXPECT_NEAR(w.bodies.vx[0], ((m1 - e * m2) * v1 + (1 + e) * m2 * v2) / M, 1e-14);
    EXPECT_NEAR(w.bodies.vx[1], ((m2 - e * m1) * v2 + (1 + e) * m1 * v1) / M, 1e-14);
    EXPECT_NEAR(m1 * w.bodies.vx[0] + m2 * w.bodies.vx[1], m1 * v1 + m2 * v2, 1e-13);
    // The overlap is removed and the centre of mass did not move.
    EXPECT_GE(w.bodies.x[1] - w.bodies.x[0], 0.2 - 1e-12);
    EXPECT_NEAR((m1 * w.bodies.x[0] + m2 * w.bodies.x[1]) / M, (m2 * 0.199) / M, 1e-14);
}

INSTANTIATE_TEST_SUITE_P(MassesAndRestitution, OneDimensionalCollision,
                         ::testing::Combine(::testing::Values(1.0, 3.0), ::testing::Values(1.0, 8.0, 0.25),
                                            ::testing::Values(0.0, 0.5, 1.0)));

TEST(Collisions, SeparatingBodiesGetNoImpulse) {
    World w = twoBodies(1.0, -1.0, 1.0, 1.0, 1.0);  // overlapping but moving apart
    UniformGrid grid;
    std::vector<IndexPair> scratch;
    const ContactStats st = resolveContacts(w, grid, scratch);
    EXPECT_EQ(st.bodyContacts, 1u);
    EXPECT_EQ(st.impulses, 0u);
    EXPECT_DOUBLE_EQ(w.bodies.vx[0], -1.0);
    EXPECT_DOUBLE_EQ(w.bodies.vx[1], 1.0);
}

TEST(Collisions, FixedAnchorDoesNotMove) {
    World w;
    w.params.collisions = true;
    w.params.restitution = 0.5;
    w.bodies.add({0.0, 0.0}, {}, 1.0, 0.1, true);
    w.bodies.add({0.19, 0.0}, {-2.0, 0.0}, 1.0, 0.1);
    UniformGrid grid;
    std::vector<IndexPair> scratch;
    resolveContacts(w, grid, scratch);
    EXPECT_DOUBLE_EQ(w.bodies.x[0], 0.0);
    EXPECT_DOUBLE_EQ(w.bodies.vx[0], 0.0);
    EXPECT_NEAR(w.bodies.vx[1], 1.0, 1e-14);  // reflected and scaled by e
}

TEST(Walls, ReflectNormalVelocityScaledByRestitution) {
    for (double e : {0.0, 0.3, 1.0}) {
        World w;
        w.params.walls = true;
        w.params.wallRestitution = e;
        w.bodies.add({0.995, 0.0}, {2.0, 0.5}, 1.0, 0.01);  // already 5 mm into the right wall
        UniformGrid grid;
        std::vector<IndexPair> scratch;
        resolveContacts(w, grid, scratch);
        EXPECT_NEAR(w.bodies.vx[0], -e * 2.0, 1e-15);
        EXPECT_DOUBLE_EQ(w.bodies.vy[0], 0.5);  // tangential velocity untouched (frictionless)
        EXPECT_LE(w.bodies.x[0] + 0.01, 1.0 + 1e-15);
    }
}

namespace {
World gas(std::uint64_t seed, bool walls, double e) {
    BuiltScenario s;
    std::vector<std::string> errs;
    ParamValues p{{"count", 600}, {"radius", 0.02}, {"speed", 1.5}, {"restitution", e}, {"wallRestitution", 1.0},
                  {"seed", static_cast<double>(seed)}};
    EXPECT_TRUE(buildScenario("particle-box", p, s, errs));
    s.world.params.walls = walls;
    return s.world;
}
}  // namespace

TEST(Conservation, MomentumIsConservedByCollisionsWithoutWalls) {
    // No walls, no gravity: only disc-disc impulses act, so total momentum must
    // stay at its initial value to round-off, whatever the masses (10% are 8x heavier).
    Simulation sim = test::makeSim(gas(3, false, 0.7), IntegratorKind::VelocityVerlet, 1e-3);
    ASSERT_TRUE(sim.valid());
    const Vec2 p0 = sim.diagnostics().now.momentum;
    std::size_t impulses = 0;
    for (int i = 0; i < 2000; ++i) {
        sim.step();
        impulses += sim.diagnostics().lastContacts.impulses;
    }
    const Vec2 p1 = sim.diagnostics().now.momentum;
    EXPECT_GT(impulses, 500u) << "test must actually exercise collisions";
    EXPECT_NEAR(p1.x, p0.x, 1e-11);
    EXPECT_NEAR(p1.y, p0.y, 1e-11);
}

TEST(Conservation, KineticEnergyIsConservedForElasticCollisionsAndWalls) {
    Simulation sim = test::makeSim(gas(5, true, 1.0), IntegratorKind::SymplecticEuler, 1e-3);
    ASSERT_TRUE(sim.valid());
    const double k0 = sim.diagnostics().now.kinetic;
    for (int i = 0; i < 3000; ++i) sim.step();
    EXPECT_NEAR(sim.diagnostics().now.kinetic / k0, 1.0, 1e-11);
}

TEST(Conservation, InelasticCollisionsOnlyRemoveEnergy) {
    Simulation sim = test::makeSim(gas(8, true, 0.5), IntegratorKind::VelocityVerlet, 1e-3);
    double prev = sim.diagnostics().now.kinetic;
    for (int i = 0; i < 1000; ++i) {
        sim.step();
        const double k = sim.diagnostics().now.kinetic;
        ASSERT_LE(k, prev * (1 + 1e-12));
        prev = k;
    }
}
