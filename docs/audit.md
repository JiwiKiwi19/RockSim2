# Audit of the original ROCsim code

Before the rebuild, the original Qt application was audited (October 2026). This document summarizes the verified findings and how each one was resolved. The original code is preserved unchanged in [`legacy/qt-app/`](../legacy/qt-app/).

## What the original did (verified in source)

The original is a Qt 6 desktop app, about 2,100 lines of C++. Its one working model is a 2D box of discs:

- wall bounces with damping
- optional particle–particle collisions, using a uniform-grid broad phase and an impulse response (CPU only)
- optional downward gravity
- JSON save and load
- an optional HIP/ROCm kernel for the per-particle update, which skips collisions

A spring model is referenced by the UI and the build, but its source file is not in the repository.

## Build

The build fails at configure: `Cannot find source file: springmassglwidget.cpp`. With a stub for that class it fails again at `mainwindow.cpp:543` (`Ui::MainWindow has no member named 'labelMemory'`). With both worked around it compiles, with 14 warnings. The test file `particleWidgetTest.cpp` is not part of the build and tests copies of the formulas, not the shipped code. The README's benchmark table, and features such as N-body, Verlet/RK4, the 3D camera and CSV export, do not exist in the code.

## Measured physics findings

The original `particlesglwidget.cpp` was compiled unmodified into a headless harness ([`legacy/audit/physics_probe.cpp`](../legacy/audit/physics_probe.cpp)), which called its step functions directly.

| # | Finding (original code) | Measured | Resolution in the rebuild | Regression test |
|---|---|---|---|---|
| E1 | Particle count argument ignored (`PARTICLE_COUNT = 100`) | 5,000 requested → 100 | Count is a validated scenario parameter | `Scenarios.RegressionE1_ParticleCountIsHonoured` |
| E2, E3 | Broad phase skips cells with fewer than 2 bodies, so lone bodies in adjacent cells never collide | A lone pair in adjacent cells is never resolved (E2); across 2,000 random 100-disc states, 31.4% of overlapping, approaching pairs are missed (774 of 2,464; E3) | Every body drives its own neighbourhood search; result must equal brute force | `BroadPhase.RegressionE2_*`, `BroadPhase.MatchesBruteForceOnRandomScenes` |
| E4 | Impulse split 50/50 regardless of mass | Masses 1 and 8: momentum −0.070 → +0.070 | Mass-weighted impulse J/m | `OneDimensionalCollision.MatchesClosedForm` (18 cases) |
| E5 | One "damping" slider gives e = d for walls but e = 2d − 1 for pairs | Default: walls e = 0.5, pairs e = 0; below d = 0.5, pairs have e < 0 | Separate, validated restitution for walls and pairs, both in [0, 1] | `Walls.*`, `OneDimensionalCollision.*` |
| E6 | Gravity applied as `vy -= 1e-4 * mass` (acceleration ∝ mass); the GPU path omits mass | After 60 ticks: m = 1 at y = 0.323, m = 10 at y = −0.790 | a = g for every body | `Integrators.RegressionE6_FreeFallIsMassIndependent` |
| E7 | 3×3 search is only valid if 2r ≤ cell size, but mass up to 100 gives 2r = 0.093 > 0.045 | Overlapping heavy pair missed | Cell size ≥ 2 r_max, recomputed each step | `BroadPhase.RegressionE7_LargeBodiesTwoCellsApart` |
| E8 | Forward Euler with dt = one timer tick; position clamping at walls | Energy drift −5.2% with "perfectly elastic" walls | Fixed dt in seconds; four integrators; walls reflect by e × penetration | `Conservation.KineticEnergyIsConservedForElasticCollisionsAndWalls` |

Other verified issues:

- Loading a file from the home screen is likely overwritten by `initializeGL()`. This was inferred from reading the code, not run.
- JSON input is not validated.
- The "Enable HIP" checkbox has no runtime effect.
- The memory readout never updates.
- The `OpenGLWidget` singleton can dangle.
- Speed settings are lost on resume.
- The grid overlay is offset by half a cell from the real grid.

The rebuild replaces the components where these live rather than patching them.

## What was kept

- The ideas, each re-implemented with tests: the uniform-grid broad phase (the team's choice over O(N²)), impulse-based contacts, constant-density mass–radius scaling, and JSON-style parameter sets.
- The UI concepts: parameter panel, scene selector, live statistics, play/pause/reset/speed.
- All original source, documents and history, in `legacy/` and `docs/course/`.
