// Benchmarks for the computationally significant parts of the engine.
// Run: cmake --preset bench && cmake --build --preset bench && ./build/bench/bench/rocksim_bench
#include <benchmark/benchmark.h>

#include "rocksim/rocksim.hpp"

using namespace rocksim;

namespace {

// Discs at a fixed area fraction (~10%) in a box that grows with N, so the
// expected number of neighbours per disc is constant and the grid should scale
// linearly while brute force scales quadratically.
Bodies discs(int n) {
    Pcg32 rng(1234);
    const double r = 0.01;
    const double half = std::sqrt(n * 3.14159265 * r * r / 0.10) / 2.0;
    Bodies b;
    b.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) b.add({rng.uniform(-half, half), rng.uniform(-half, half)}, {}, 1.0, r);
    return b;
}

void BM_BroadPhaseGrid(benchmark::State& state) {
    const Bodies b = discs(static_cast<int>(state.range(0)));
    UniformGrid grid;
    std::vector<IndexPair> out;
    for (auto _ : state) {
        grid.findOverlaps(b, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetComplexityN(state.range(0));
    state.counters["pairs"] = static_cast<double>(out.size());
}
BENCHMARK(BM_BroadPhaseGrid)->RangeMultiplier(4)->Range(256, 65536)->Complexity();

void BM_BroadPhaseBruteForce(benchmark::State& state) {
    const Bodies b = discs(static_cast<int>(state.range(0)));
    std::vector<IndexPair> out;
    for (auto _ : state) {
        bruteForceOverlaps(b, out);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_BroadPhaseBruteForce)->RangeMultiplier(4)->Range(256, 16384)->Complexity();

// One full step of the default particle-box scene (integrate + contacts).
void BM_ParticleBoxStep(benchmark::State& state) {
    BuiltScenario s;
    std::vector<std::string> errs;
    buildScenario("particle-box", {{"count", static_cast<double>(state.range(0))}, {"radius", 0.008}}, s, errs);
    Simulation sim = Simulation::create(s.world, IntegratorKind::VelocityVerlet, 1e-3, errs);
    for (auto _ : state) sim.step();
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_ParticleBoxStep)->Arg(300)->Arg(1000)->Arg(3000);

// Cost per step of each integrator on the soft body (springs, 96 nodes).
void BM_IntegratorSoftBody(benchmark::State& state) {
    BuiltScenario s;
    std::vector<std::string> errs;
    buildScenario("soft-body", {{"cols", 12}, {"rows", 8}}, s, errs);
    s.world.params.walls = false;  // isolate the integrator
    Simulation sim = Simulation::create(s.world, static_cast<IntegratorKind>(state.range(0)), 1e-4, errs);
    for (auto _ : state) sim.step();
    state.SetLabel(std::string(integratorInfo(static_cast<IntegratorKind>(state.range(0))).name));
}
BENCHMARK(BM_IntegratorSoftBody)->DenseRange(0, 3);

// Direct-sum gravity: O(N^2) per force evaluation.
void BM_NBodyStep(benchmark::State& state) {
    BuiltScenario s;
    std::vector<std::string> errs;
    buildScenario("orbital-disk", {{"count", static_cast<double>(state.range(0))}}, s, errs);
    Simulation sim = Simulation::create(s.world, IntegratorKind::VelocityVerlet, 1e-3, errs);
    for (auto _ : state) sim.step();
    state.SetComplexityN(state.range(0));
}
BENCHMARK(BM_NBodyStep)->RangeMultiplier(2)->Range(50, 400)->Complexity(benchmark::oNSquared);

}  // namespace
