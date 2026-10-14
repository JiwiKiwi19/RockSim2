# Validation

This document lists how RockSim's behaviour is checked, what each check establishes, and what it does not. No single check proves a simulation correct. For example, a conservation law can hold while the trajectory is wrong. The suite therefore combines independent kinds of evidence:

- exact references: closed-form solutions and brute-force searches
- conservation laws
- convergence rates
- cross-platform reproducibility

## How to run

```bash
cmake --preset release && cmake --build --preset release && ctest --preset release   # 58 tests
cmake --preset debug && cmake --build --preset debug && ctest --preset debug         # same, under ASan + UBSan
./build/release/cli/rocksim-cli convergence [--damping 0.3]
./build/release/cli/rocksim-cli drift --orbits 200
./build/release/cli/rocksim-cli hash
cd web && npm run build && npm run test:e2e                                          # 5 browser tests
```

Results below were measured on 2026-10-14. Native runs used Ubuntu 24.04 on x86-64 (GCC 13.3, Release) and Ubuntu 22.04 on ARM64 (GCC 11.4). WebAssembly runs used Emscripten 3.1.6 in Node 22 and Chromium.

## Test inventory (`tests/`)

### Contacts (`test_contacts.cpp`)

| Test | Establishes | Does not establish |
|---|---|---|
| `BroadPhase.MatchesBruteForceOnRandomScenes` | The grid returns exactly the brute-force pair set: 60 random scenes, up to 300 bodies, radii varying 40× | Behaviour on scenes unlike these (very large N is covered by benchmarks only) |
| `BroadPhase.HandlesClusteredAndFarFlungBodies` | Same equality with a tight cluster, a body 10⁶ m away and coincident centres | — |
| `BroadPhase.RegressionE2_*`, `RegressionE7_*` | The two original broad-phase defects are fixed | — |
| `OneDimensionalCollision.MatchesClosedForm` (18 cases) | The impulse matches the closed-form 1-D collision for mass ratios 0.25–8 and e ∈ {0, 0.5, 1}, conserves momentum, removes the overlap and keeps the centre of mass fixed | Oblique or multi-body contact |
| `Conservation.MomentumIsConservedByCollisionsWithoutWalls` | Over 2,000 steps with more than 500 impulses (10% of bodies 8× heavier), total momentum changes by < 10⁻¹¹ | That the motion is physically realistic |
| `Conservation.KineticEnergyIsConservedForElasticCollisionsAndWalls` | With e = 1, kinetic energy is constant to 10⁻¹¹ relative over 3,000 steps | Correct trajectories |
| `Conservation.InelasticCollisionsOnlyRemoveEnergy` | With e < 1, kinetic energy never increases | The amount removed |
| `Walls.*`, `Collisions.*` | Wall restitution, tangential velocity preserved, fixed anchors immovable, separating pairs untouched | Friction (not modelled) |

### Integrators (`test_integrators.cpp`)

| Test | Establishes | Does not establish |
|---|---|---|
| `ConvergenceOrder.MatchesTheory` (4 integrators) | The observed order is within ±0.15 of 1, 1, 2, 4, and the error decreases monotonically | Order across contact events |
| `Integrators.VerletStaysSecondOrderWithDamping` | Order 2 with velocity-dependent forces (this test found the original first-order behaviour) | — |
| `Integrators.DampedOscillatorMatchesClosedFormAllRegimes` | Agreement with the exact solution for under-, critically and over-damped cases (RK4 < 10⁻¹¹, Verlet < 10⁻⁵) | — |
| `Integrators.ConstantAccelerationIsExactForVerletAndRK4` | Exact parabolic free fall at large dt | — |
| `Integrators.RegressionE6_FreeFallIsMassIndependent` | Free fall does not depend on mass | — |
| `Integrators.SymplecticMethodsKeepEnergyBoundedEulerDrifts` | Over 10⁵ steps, symplectic methods' energy error does not grow; explicit Euler is detected as unstable | Behaviour on non-oscillatory problems |
| `Integrators.VerletReusesForceEvaluationWhenConservative` | 1 evaluation per step (Verlet) and 4 (RK4), as claimed | — |

### Gravity (`test_nbody.cpp`)

| Test | Establishes | Does not establish |
|---|---|---|
| `Kepler.PeriodMatchesKeplersThirdLaw` | The measured period matches 2π√(a³/GM) to 10⁻⁵ relative (Verlet and RK4) | Close-encounter accuracy |
| `Kepler.ConservationOverTenOrbits` | Energy error < 10⁻⁴; angular momentum to 10⁻¹²; momentum to 10⁻¹³ | — |
| `Kepler.ExplicitEulerSpiralsOut` | The baseline behaves as theory predicts (energy grows) | — |
| `FigureEight.ReturnsToInitialStateAfterOnePeriod` | The published periodic orbit closes to 2 × 10⁻⁵ (limited by the 8-digit initial conditions) | Long-term stability (the orbit is sensitive to perturbation) |
| `NBody.ThirdLawConservesMomentumInADisk` | Momentum to 10⁻¹³ with 200 bodies | — |
| `NBody.SofteningMatchesPotentialGradient` | Force = −∇U for the softened kernel (finite differences) | — |

### Simulation (`test_simulation.cpp`)

- **Validation:** invalid bodies, springs, parameters and dt return specific messages. Invalid simulations are inert. Out-of-range scenario parameters are rejected. Live edits are validated and rolled back on failure.
- **Every scenario** builds with default parameters and runs 500 steps without instability.
- **Reproducibility:** the same seed gives bit-identical runs, and `GoldenStateHashesAfter3000Steps` pins the hashes of all six scenarios.
- **Accumulator:** steps are whole multiples of dt, the remainder carries over, and the backlog is capped.
- **Stability detection:** a stiff soft body under explicit Euler at dt = 2 ms is stopped with a message; the same scene under Verlet is stable.
- **RNG:** PCG32 matches the reference `pcg32-demo` output, and the uniform distribution is in range and unbiased (within 5σ).

### Browser (`web/e2e/smoke.spec.ts`, Playwright + Chromium)

1. The WebAssembly engine loads, runs and advances time with no page errors.
2. Out-of-range input shows a specific message and disables restart.
3. An unstable configuration is stopped with an explanation.
4. The convergence study run in the browser reproduces all four theoretical orders.
5. The browser engine reproduces the native golden hash after 3,000 steps.

## Measured results

### Convergence (`rocksim-cli convergence`)

Oscillator m = k = 1, x₀ = 1, integrated to t = 5.12 with dt = 0.08/2^j, j = 0..6. The error is √(Δx² + Δv²) against the closed-form solution, and the order is the least-squares slope of log(error) against log(dt).

| Integrator | Order (c = 0) | Order (c = 0.3) | Error at dt = 0.005 (c = 0) | Force evaluations at dt = 0.005 (c = 0 / c = 0.3) |
|---|---|---|---|---|
| Explicit Euler | 1.021 | 1.023 | 1.288 × 10⁻² | 1,024 / 1,024 |
| Symplectic Euler | 1.007 | 1.002 | 2.300 × 10⁻³ | 1,024 / 1,024 |
| Velocity Verlet | 2.000 | 2.000 | 6.986 × 10⁻⁶ | 1,025 / 2,049 |
| RK4 | 4.000 | 4.004 | 2.667 × 10⁻¹¹ | 4,096 / 4,096 |

### Energy over long runs (`rocksim-cli drift`)

Kepler orbit, e = 0.6, mass ratio 0.2, dt = 0.01. Values are max |ΔE/E₀|.

| Integrator | 50 orbits | 200 orbits | Behaviour |
|---|---|---|---|
| Explicit Euler | 1.03 | 1.03 | orbit nearly escapes |
| Symplectic Euler | 3.2 × 10⁻² | 3.2 × 10⁻² | bounded, oscillating |
| Velocity Verlet | 8.9 × 10⁻⁴ | 8.9 × 10⁻⁴ | bounded, oscillating |
| RK4 | 1.8 × 10⁻⁶ | 6.9 × 10⁻⁶ | small but growing roughly linearly |

### Reproducibility (`rocksim-cli hash`, 3,000 steps, default parameters)

| Scenario | x86-64 GCC 13 | ARM64 GCC 11 | WebAssembly |
|---|---|---|---|
| particle-box | `0c4e6de3f05f6f05` | same | same |
| spring-oscillator | `71c4b76808506524` | same | same (also in Chromium) |
| soft-body | `9fff10aa461cc357` | same | same |
| kepler | `9e500ed27738bb6a` | same | same |
| figure-eight | `1d96b7c3237da6bb` | same | same |
| orbital-disk | `80623132be0d8a69` | same | same |

Not yet tested: macOS (Apple Clang), Windows (MSVC). The `-ffp-contract=off` flag and the libm restrictions are meant to make these match too; the golden-hash test will show whether they do.
