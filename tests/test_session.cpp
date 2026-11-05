#include <gtest/gtest.h>

#include <map>

#include "helpers.hpp"
#include "rocksim/rocksim.hpp"

using namespace rocksim;

namespace {

constexpr const char* kScenarios[] = {"particle-box", "spring-oscillator", "soft-body",
                                      "kepler",       "figure-eight",      "orbital-disk"};
constexpr IntegratorKind kIntegrators[] = {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler,
                                           IntegratorKind::VelocityVerlet, IntegratorKind::RK4};

Simulation makeScenarioSim(const char* id, IntegratorKind kind) {
    const ScenarioSpec* spec = findScenario(id);
    BuiltScenario b;
    std::vector<std::string> errs;
    EXPECT_TRUE(buildScenario(id, {}, b, errs));
    return test::makeSim(b.world, kind, spec->defaultDt);
}

Session makeSession(const char* id, SessionConfig cfg = {}) {
    const ScenarioSpec* spec = findScenario(id);
    Session s(cfg);
    std::vector<std::string> errs;
    EXPECT_TRUE(s.load(id, {}, spec->defaultIntegrator, spec->defaultDt, errs));
    return s;
}

std::uint64_t hashOf(const Session& s, int run = 0) { return stateHash(s.sim(run).world().bodies); }

/// Runs a fixed script of user actions on a soft body, step by step, and
/// returns the state hash observed at each step (after that step's commands).
std::map<std::uint64_t, std::uint64_t> runScript(Session& s, int steps) {
    std::map<std::uint64_t, std::uint64_t> seen;
    seen[0] = hashOf(s);
    for (int i = 1; i <= steps; ++i) {
        s.step(1);
        const auto at = s.cursorStep();
        // Commands land on keyframe boundaries (every 50 steps) and in between.
        if (at == 50) EXPECT_TRUE(s.grabStart(0, 3, {0.2, 0.5}).ok);
        if (at == 60) EXPECT_TRUE(s.grabMove(0, {-0.3, 0.1}).ok);
        if (at == 75) EXPECT_TRUE(s.setIntegrator(0, IntegratorKind::RK4).ok);
        if (at == 100) {
            EXPECT_TRUE(s.grabEnd(0).ok);
            EXPECT_TRUE(s.setParam(0, "gravity", 4.0).ok);
        }
        if (at == 120) EXPECT_TRUE(s.setDt(0, 5e-4).ok);
        if (at == 150) EXPECT_TRUE(s.setBody(0, 5, {0.0, 0.3}, {1.0, -1.0}).ok);
        seen[at] = hashOf(s);
    }
    return seen;
}

SessionConfig fineKeyframes() {
    SessionConfig c;
    c.keyframeInterval = 50;
    return c;
}

}  // namespace

TEST(SimulationSnapshot, RestoreContinuesBitIdenticallyForEveryScenarioAndIntegrator) {
    for (const char* id : kScenarios)
        for (IntegratorKind kind : kIntegrators) {
            Simulation sim = makeScenarioSim(id, kind);
            sim.stepN(60);
            const SimSnapshot snap = sim.capture();
            sim.stepN(90);
            const std::uint64_t expected = stateHash(sim.world().bodies);
            const double t = sim.time();

            sim.restore(snap);
            EXPECT_EQ(sim.steps(), 60u);
            sim.stepN(90);
            EXPECT_EQ(stateHash(sim.world().bodies), expected) << id << " " << integratorInfo(kind).id;
            EXPECT_EQ(sim.time(), t) << id;

            // Into a different object that has been somewhere else entirely.
            Simulation other = makeScenarioSim(id, IntegratorKind::ExplicitEuler);
            other.stepN(7);
            other.restore(snap);
            other.stepN(90);
            EXPECT_EQ(stateHash(other.world().bodies), expected) << id << " " << integratorInfo(kind).id;
        }
}

TEST(SimulationSnapshot, RestoreBringsBackAnUnstableRunToItsOkState) {
    Simulation sim = makeScenarioSim("soft-body", IntegratorKind::ExplicitEuler);
    ASSERT_TRUE(sim.setDt(0.004));
    sim.stepN(10);
    const SimSnapshot ok = sim.capture();
    sim.stepN(100000);
    ASSERT_EQ(sim.status(), SimStatus::Unstable);
    sim.restore(ok);
    EXPECT_EQ(sim.status(), SimStatus::Ok);
    EXPECT_TRUE(sim.statusMessage().empty());
}

TEST(Grab, ForceIsCriticallyDampedAndUsesOnlyArithmetic) {
    World w;
    w.bodies.add({1.0, 2.0}, {0.5, -0.25}, 3.0, 0.0);
    w.grab = Grab{0, {4.0, -1.0}, 10.0};
    double ax = 0, ay = 0;
    computeAccelerations(w, w.bodies.x, w.bodies.y, w.bodies.vx, w.bodies.vy, {&ax, 1}, {&ay, 1});
    EXPECT_EQ(ax, 100.0 * (4.0 - 1.0) - 2.0 * 10.0 * 0.5);
    EXPECT_EQ(ay, 100.0 * (-1.0 - 2.0) - 2.0 * 10.0 * -0.25);
}

TEST(Grab, PullsABodyToItsTargetWithoutOvershootAtAnyIntegrator) {
    for (IntegratorKind kind : kIntegrators) {
        World w;
        w.bodies.add({0.0, 0.0}, {0.0, 0.0}, 1.0, 0.0);
        Simulation sim = test::makeSim(std::move(w), kind, 1e-3);
        ASSERT_TRUE(sim.setGrab(0, {1.0, 0.0}));
        double maxX = 0.0;
        for (int i = 0; i < 2000; ++i) {
            sim.step();
            maxX = std::max(maxX, sim.world().bodies.x[0]);
        }
        EXPECT_NEAR(sim.world().bodies.x[0], 1.0, 2e-3) << integratorInfo(kind).id;
        EXPECT_LT(maxX, 1.01) << integratorInfo(kind).id;
    }
}

TEST(Grab, IsIndependentOfTheVerletCache) {
    // After the target moves, the cached acceleration is stale. The next step
    // must equal the same step taken by a simulation that never had a cache.
    Simulation sim = makeScenarioSim("soft-body", IntegratorKind::VelocityVerlet);
    ASSERT_TRUE(sim.setGrab(4, {0.1, 0.2}));
    sim.stepN(25);
    ASSERT_TRUE(sim.moveGrab({-0.4, 0.6}));
    Simulation fresh = test::makeSim(sim.world(), IntegratorKind::VelocityVerlet, sim.dt());
    sim.step();
    fresh.step();
    EXPECT_EQ(stateHash(sim.world().bodies), stateHash(fresh.world().bodies));
}

TEST(Grab, RejectsFixedBodiesBadIndicesAndNonFiniteTargets) {
    Simulation sim = makeScenarioSim("spring-oscillator", IntegratorKind::VelocityVerlet);
    EXPECT_FALSE(sim.setGrab(0, {0.0, 0.0}));  // the anchor
    EXPECT_FALSE(sim.setGrab(9, {0.0, 0.0}));
    EXPECT_FALSE(sim.setGrab(1, {std::nan(""), 0.0}));
    EXPECT_FALSE(sim.grabbed());
    EXPECT_TRUE(sim.setGrab(1, {0.0, 0.0}));
    EXPECT_FALSE(sim.moveGrab({0.0, INFINITY}));
}

TEST(Grab, TargetIsClampedIntoTheBox) {
    Simulation sim = makeScenarioSim("particle-box", IntegratorKind::VelocityVerlet);
    ASSERT_TRUE(sim.setGrab(0, {50.0, -50.0}));
    sim.stepN(2000);
    const double r = sim.world().bodies.radius[0];
    EXPECT_LE(sim.world().bodies.x[0], 1.0 - r + 1e-9);
    EXPECT_GE(sim.world().bodies.y[0], -1.0 + r - 1e-9);
    EXPECT_EQ(sim.status(), SimStatus::Ok);
}

TEST(Grab, AHardGrabIsNotMistakenForNumericalInstability) {
    for (IntegratorKind kind : kIntegrators) {
        Simulation sim = makeScenarioSim("soft-body", kind);
        ASSERT_TRUE(sim.setGrab(2, {0.9, 0.9}));
        for (int i = 0; i < 4000; ++i) {
            if (i % 500 == 0) sim.moveGrab({(i % 1000 == 0) ? 0.9 : -0.9, 0.9});
            sim.step();
        }
        EXPECT_EQ(sim.status(), SimStatus::Ok) << integratorInfo(kind).id << ": " << sim.statusMessage();
        sim.releaseGrab();
        EXPECT_EQ(sim.diagnostics().relativeEnergyError, 0.0);  // baseline reset on release
    }
}

TEST(Grab, RealInstabilityIsStillDetectedWithAndWithoutAGrab) {
    for (bool grab : {false, true}) {
        // Explicit Euler on a stiff spring at a step far too large: the state grows without bound.
        Simulation sim = makeScenarioSim("spring-oscillator", IntegratorKind::ExplicitEuler);
        ASSERT_TRUE(sim.setDt(0.5));
        if (grab) ASSERT_TRUE(sim.setGrab(1, {0.3, 0.0}));
        sim.stepN(100000);
        EXPECT_EQ(sim.status(), SimStatus::Unstable) << (grab ? "with grab" : "without grab");
    }
    // Without a grab the energy check also catches a blow-up that stays finite.
    Simulation soft = makeScenarioSim("soft-body", IntegratorKind::ExplicitEuler);
    ASSERT_TRUE(soft.setDt(0.004));
    soft.stepN(200000);
    EXPECT_EQ(soft.status(), SimStatus::Unstable);
}

TEST(Session, SeekReproducesTheRecordedHashAtEveryStep) {
    Session s = makeSession("soft-body", fineKeyframes());
    const auto seen = runScript(s, 200);
    ASSERT_EQ(s.events(0).size(), 7u);

    for (std::uint64_t at : {0u, 1u, 49u, 50u, 51u, 60u, 75u, 99u, 100u, 101u, 119u, 120u, 121u, 149u, 150u, 151u, 199u, 200u}) {
        EXPECT_EQ(s.seek(at), at);
        EXPECT_EQ(hashOf(s), seen.at(at)) << "seek to " << at;
    }
    // Every step, backwards and forwards.
    for (std::uint64_t at = 200; at >= 7; at -= 7) {
        s.seek(at);
        EXPECT_EQ(hashOf(s), seen.at(at)) << "seek to " << at;
    }
    for (std::uint64_t at = 3; at <= 200; at += 11) {
        s.seek(at);
        EXPECT_EQ(hashOf(s), seen.at(at)) << "seek to " << at;
    }
}

TEST(Session, ReplayingForwardDoesNotDuplicateSamplesKeyframesOrEvents) {
    Session s = makeSession("soft-body", fineKeyframes());
    runScript(s, 200);
    const auto samples = s.series(0).size();
    const auto keyframes = s.keyframeCount(0);
    const auto events = s.events(0).size();
    const auto head = s.headStep(0);
    s.seek(20);
    s.step(180);
    EXPECT_EQ(s.cursorStep(), 200u);
    EXPECT_EQ(s.headStep(0), head);
    EXPECT_EQ(s.series(0).size(), samples);
    EXPECT_EQ(s.keyframeCount(0), keyframes);
    EXPECT_EQ(s.events(0).size(), events);
}

TEST(Session, SameStartAndSameEventLogGiveTheSameHash) {
    Session a = makeSession("soft-body", fineKeyframes());
    Session b = makeSession("soft-body", fineKeyframes());
    const auto seenA = runScript(a, 200);
    const auto seenB = runScript(b, 200);
    EXPECT_EQ(seenA, seenB);
    ASSERT_EQ(a.events(0).size(), b.events(0).size());
    for (std::size_t i = 0; i < a.events(0).size(); ++i) {
        EXPECT_EQ(a.events(0)[i].step, b.events(0)[i].step);
        EXPECT_EQ(a.events(0)[i].kind, b.events(0)[i].kind);
    }
    // Rewinding to the start and replaying the log gives the same end state.
    a.seek(0);
    a.step(200);
    EXPECT_EQ(hashOf(a), seenB.at(200));
}

TEST(Session, DraggingABodyBetweenTwoStepsIsLoggedOnce) {
    Session s = makeSession("spring-oscillator");
    s.step(3);
    for (int i = 0; i < 10; ++i) ASSERT_TRUE(s.setBody(0, 1, {0.01 * i, 0.0}, {0.0, 0.0}).ok);
    ASSERT_EQ(s.events(0).size(), 1u);
    EXPECT_EQ(s.events(0)[0].a, 0.09);
    s.step(1);
    ASSERT_TRUE(s.setBody(0, 1, {0.2, 0.0}, {0.0, 0.0}).ok);
    EXPECT_EQ(s.events(0).size(), 2u);  // a different step is a different event
    // Replaying the collapsed log reproduces the live state.
    const std::uint64_t live = hashOf(s);
    s.seek(0);
    s.step(1);
    s.seek(4);
    EXPECT_EQ(hashOf(s), live);
}

TEST(Session, RejectedCommandsAreNotLogged) {
    Session s = makeSession("spring-oscillator");
    s.step(5);
    EXPECT_FALSE(s.setBody(0, 0, {0.0, 0.0}, {0.0, 0.0}).ok);        // anchor
    EXPECT_FALSE(s.setBody(0, 1, {std::nan(""), 0.0}, {0.0, 0.0}).ok);
    EXPECT_FALSE(s.grabStart(0, 0, {0.0, 0.0}).ok);
    EXPECT_FALSE(s.grabMove(0, {0.0, 0.0}).ok);                       // nothing held
    EXPECT_FALSE(s.setParam(0, "stiffness", 20.0).ok);                // not live
    EXPECT_FALSE(s.setParam(0, "nope", 1.0).ok);
    EXPECT_FALSE(s.setDt(0, -1.0).ok);
    EXPECT_FALSE(s.setBody(1, 1, {0.1, 0.0}, {0.0, 0.0}).ok);         // no run B
    EXPECT_TRUE(s.events(0).empty());
    EXPECT_EQ(s.runCount(), 1);
}

TEST(Session, SeveralGrabMovesBetweenTwoStepsAreLoggedOnce) {
    Session s = makeSession("soft-body");
    s.step(3);
    ASSERT_TRUE(s.grabStart(0, 1, {0.1, 0.1}).ok);
    for (int i = 0; i < 10; ++i) ASSERT_TRUE(s.grabMove(0, {0.1 * i, 0.0}).ok);
    ASSERT_EQ(s.events(0).size(), 2u);
    EXPECT_EQ(s.events(0)[1].a, 0.9);
}

TEST(Session, AnEditInThePastBranchesIntoRunBAndKeepsAsFuture) {
    Session s = makeSession("soft-body", fineKeyframes());
    const auto seen = runScript(s, 200);
    const auto eventsBefore = s.events(0);

    s.seek(40);
    const CommandResult r = s.setBody(0, 7, {0.0, 0.0}, {2.0, 2.0});
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.branched);
    ASSERT_EQ(s.runCount(), 2);
    EXPECT_EQ(s.headStep(0), 200u);  // A's future is intact
    EXPECT_EQ(s.headStep(1), 40u);
    EXPECT_EQ(s.branchStep(), 40u);
    ASSERT_EQ(s.events(0).size(), eventsBefore.size());
    EXPECT_EQ(s.events(1).size(), 1u);
    EXPECT_NE(hashOf(s, 0), hashOf(s, 1));

    // Lockstep: both runs advance together; A replays its recorded future.
    for (int i = 0; i < 160; ++i) {
        s.step(1);
        ASSERT_EQ(s.sim(0).steps(), s.sim(1).steps());
        if (const auto it = seen.find(s.cursorStep()); it != seen.end() && s.cursorStep() <= 200)
            ASSERT_EQ(hashOf(s, 0), it->second) << "A at " << s.cursorStep();
    }
    EXPECT_EQ(s.cursorStep(), 200u);
    EXPECT_NE(hashOf(s, 0), hashOf(s, 1));
    EXPECT_GT(s.divergence(), 0.0);

    // B is reproducible too: rewind and replay.
    const std::uint64_t endB = hashOf(s, 1);
    s.seek(60);
    s.step(140);
    EXPECT_EQ(hashOf(s, 1), endB);
    EXPECT_EQ(s.series(1).div.size(), s.series(1).size());
    EXPECT_EQ(s.series(1).div.front(), 0.0);
}

TEST(Session, ADragThatStartedInThePastKeepsEditingItsBranch) {
    Session s = makeSession("spring-oscillator");
    s.step(100);
    s.seek(40);
    ASSERT_TRUE(s.setBody(0, 1, {0.1, 0.0}, {0.0, 0.0}).branched);
    for (int i = 2; i < 6; ++i) {
        const CommandResult r = s.setBody(0, 1, {0.1 * i, 0.0}, {0.0, 0.0});
        ASSERT_TRUE(r.ok);
        EXPECT_FALSE(r.branched);
    }
    ASSERT_EQ(s.events(1).size(), 1u);
    EXPECT_EQ(s.events(1)[0].a, 0.5);
    EXPECT_EQ(s.headStep(0), 100u);
    // A different edit is a new branch.
    EXPECT_TRUE(s.setBody(0, 1, {0.9, 0.0}, {0.0, 0.0}).ok);
}

TEST(Session, AThirdBranchReplacesRunB) {
    Session s = makeSession("soft-body", fineKeyframes());
    runScript(s, 200);
    s.seek(40);
    ASSERT_TRUE(s.setBody(0, 7, {0.0, 0.0}, {2.0, 2.0}).branched);
    s.step(50);
    s.seek(20);
    const CommandResult r = s.setBody(0, 3, {0.1, 0.1}, {0.0, 0.0});
    ASSERT_TRUE(r.branched);
    EXPECT_EQ(s.runCount(), 2);
    ASSERT_EQ(s.events(1).size(), 1u);
    EXPECT_EQ(s.events(1)[0].body, 3u);
    EXPECT_EQ(s.headStep(1), 20u);
}

TEST(Session, ACommandOnBInThePastReplacesBsFutureOnly) {
    Session s = makeSession("soft-body", fineKeyframes());
    s.step(100);
    s.seek(30);
    ASSERT_TRUE(s.setBody(0, 7, {0.0, 0.0}, {2.0, 2.0}).branched);
    s.step(70);
    ASSERT_EQ(s.cursorStep(), 100u);
    s.seek(60);
    ASSERT_TRUE(s.setBody(1, 2, {0.1, 0.1}, {0.0, 0.0}).branched);
    EXPECT_EQ(s.headStep(0), 100u);
    EXPECT_EQ(s.headStep(1), 60u);
    EXPECT_EQ(s.events(1).size(), 2u);
}

TEST(Session, AUnstableRunCanBeRewoundAndRunAgainWithASmallerStep) {
    SessionConfig cfg;
    cfg.keyframeInterval = 25;
    Session s = makeSession("soft-body", cfg);
    ASSERT_TRUE(s.setIntegrator(0, IntegratorKind::ExplicitEuler).ok);
    ASSERT_TRUE(s.setDt(0, 0.004).ok);
    s.play();
    for (int i = 0; i < 2000 && s.running(); ++i) s.tick(0.016, 0.0);
    ASSERT_EQ(s.sim(0).status(), SimStatus::Unstable);
    EXPECT_FALSE(s.running());
    EXPECT_FALSE(s.setDt(0, 0.001).ok);  // edits wait for a rewind
    const double t = s.cursorTime();

    s.seekTime(t - 0.1);
    EXPECT_EQ(s.sim(0).status(), SimStatus::Ok);
    EXPECT_LT(s.cursorTime(), t);
    const CommandResult r = s.setDt(0, 0.0005);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.branched);
    s.step(400);
    EXPECT_EQ(s.sim(1).status(), SimStatus::Ok);
}

TEST(Session, KeyframeBudgetDropsTheOldestAndEventsGoWithThem) {
    SessionConfig cfg;
    cfg.keyframeInterval = 10;
    Session probe = makeSession("spring-oscillator", cfg);
    const std::size_t one = probe.keyframeBytes();
    ASSERT_GT(one, 0u);
    cfg.keyframeBudgetBytes = 6 * one;

    Session s = makeSession("spring-oscillator", cfg);
    std::map<std::uint64_t, std::uint64_t> seen;
    for (int i = 1; i <= 300; ++i) {
        s.step(1);
        if (s.cursorStep() % 100 == 5) ASSERT_TRUE(s.setBody(0, 1, {0.1, 0.0}, {0.0, 0.5}).ok);
        seen[s.cursorStep()] = hashOf(s);
    }
    EXPECT_LE(s.keyframeBytes(), cfg.keyframeBudgetBytes);
    EXPECT_LE(s.keyframeCount(0), 6u);
    const std::uint64_t oldest = s.oldestStep();
    EXPECT_GT(oldest, 0u);
    for (const Event& e : s.events(0)) EXPECT_GE(e.step, oldest);
    EXPECT_LT(s.events(0).size(), 3u);  // the early events were trimmed

    EXPECT_EQ(s.seek(0), oldest);  // clamped to what is still reachable
    EXPECT_EQ(hashOf(s), seen.at(oldest));
    for (std::uint64_t at = oldest; at <= 300; at += 13) {
        s.seek(at);
        EXPECT_EQ(hashOf(s), seen.at(at)) << at;
    }
}

TEST(Session, TimeLoopTakesWholeStepsAndStopsWhenPaused) {
    Session s = makeSession("spring-oscillator");  // dt = 0.01
    EXPECT_EQ(s.tick(0.016, 0.0), 0);              // not running
    s.play();
    int total = 0;
    for (int i = 0; i < 100; ++i) total += s.tick(0.016, 0.0);
    EXPECT_NEAR(total, 160, 2);  // 1.6 s of wall time at 1x
    EXPECT_EQ(s.cursorStep(), static_cast<std::uint64_t>(total));
    s.setSpeed(2.0);
    const int before = total;
    for (int i = 0; i < 50; ++i) total += s.tick(0.016, 0.0);
    EXPECT_NEAR(total - before, 160, 3);
    s.pause();
    EXPECT_EQ(s.tick(0.016, 0.0), 0);
}

TEST(Session, SamplesAreBoundedAndEvenlySpaced) {
    SessionConfig cfg;
    cfg.maxSamples = 100;
    Session s = makeSession("spring-oscillator", cfg);
    s.step(5000);
    const Series& ser = s.series(0);
    EXPECT_LE(ser.size(), 100u);
    EXPECT_GT(ser.size(), 40u);
    const double gap = ser.step[1] - ser.step[0];
    for (std::size_t i = 1; i < ser.size(); ++i) EXPECT_EQ(ser.step[i] - ser.step[i - 1], gap);
    EXPECT_EQ(ser.numX.size(), ser.size());
    EXPECT_EQ(ser.refX.size(), ser.size());
}

TEST(Session, TrailsFillAtAFixedStepStrideAndClearOnSeek) {
    Session s = makeSession("kepler");
    s.step(100);
    EXPECT_EQ(s.traced().size(), 2u);
    std::vector<double> counts;
    (void)s.trailPoints(0, counts);
    ASSERT_EQ(counts.size(), 2u);
    EXPECT_EQ(counts[0], 11.0);  // step 0 plus every 10th step (dt = 1e-3)
    s.seek(50);
    (void)s.trailPoints(0, counts);
    EXPECT_EQ(counts[0], 1.0);
}

TEST(Session, PickUsesTheEdgeOfTheBodyAndAWorldTolerance) {
    Session s = makeSession("spring-oscillator");
    const double x = s.sim(0).world().bodies.x[1], r = s.sim(0).world().bodies.radius[1];
    EXPECT_EQ(s.pick(0, x + r + 0.01, 0.0, 0.02), 1);
    EXPECT_EQ(s.pick(0, x + r + 0.05, 0.0, 0.02), -1);
    EXPECT_EQ(s.pick(0, -1.6, 0.0, 0.02), 0);  // the anchor can be inspected
}

TEST(Session, LoadFailureLeavesThePreviousSessionUntouched) {
    Session s = makeSession("spring-oscillator");
    s.step(10);
    std::vector<std::string> errs;
    EXPECT_FALSE(s.load("spring-oscillator", {{"mass", -1.0}}, IntegratorKind::RK4, 0.01, errs));
    EXPECT_FALSE(errs.empty());
    EXPECT_EQ(s.cursorStep(), 10u);
}
