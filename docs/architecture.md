# Architecture

## Layout

```
engine/            C++20 library, standard library only (no Qt, no Emscripten)
  include/rocksim/   public headers: world, contacts, integrators, simulation, scenarios, analysis, analytic, rng
  src/               implementation
tests/             GoogleTest suite (58 tests)
bench/             Google Benchmark
cli/               rocksim-cli: list, run, convergence, drift, hash
bindings/wasm/     embind layer, the only code that knows about JavaScript
web/               React + TypeScript + Vite UI; the engine runs in a Web Worker
docs/              physics, validation, benchmarks, audit, course deliverables
legacy/            the original Qt application, unchanged, plus the audit harness
```

## Data flow

```mermaid
sequenceDiagram
  participant UI as React UI (main thread)
  participant W as Engine worker
  participant E as WebAssembly engine
  UI->>W: load {scenario, params, integrator, dt}
  W->>E: Simulation.load(...)
  E-->>W: {ok, errors} (validated, never throws)
  W-->>UI: loaded {topology: radii, masses, springs}
  UI->>W: play
  loop every ~16 ms
    W->>E: advance(elapsed x speed, stepBudget)
    E-->>W: steps taken (whole steps of dt)
    W->>E: x(), y(), vx(), vy() views + diagnostics()
    W-->>UI: frame (copied Float64Arrays, transferred, not cloned)
  end
  UI->>UI: canvas redraw each animation frame; charts and readouts at 10 Hz
```

## Components and their contracts

**`World`** (`world.hpp`) holds plain data: bodies stored as a structure of arrays, springs, and global parameters. `computeAccelerations` evaluates the smooth forces at any phase-space point, so multi-stage integrators can call it at trial states. `validateWorld` returns messages and never throws.

**`Integrator`** (`integrators.hpp`) advances the smooth dynamics by dt. It allocates nothing after the first step. It caches the last acceleration for Verlet, and that cache must be invalidated after any outside change to state or parameters; `Simulation` does this.

**Contacts** (`contacts.hpp`): `UniformGrid::findOverlaps` returns exactly the brute-force pair set, sorted. `resolveContacts` applies positional projection and impulses. The two are separate so the broad phase can be tested and benchmarked on its own.

**`Simulation`** (`simulation.hpp`) owns a world and runs the fixed-dt accumulator, contacts, health checks and diagnostics. `Simulation::create` validates its inputs; an invalid simulation is inert. Live parameter edits go through `paramsChanged()`, which re-validates and rolls back on failure.

**Scenarios** (`scenarios.hpp`) form a catalogue of parameter specifications: range, unit, integer-only, and whether a parameter can change while running. The CLI and the web UI both use it, so validation rules live in one place.

**Analysis** (`analysis.hpp`) runs the convergence and energy-drift studies. The CLI, the tests and the browser's Verify view all run the same code.

**WebAssembly bindings** (`bindings/wasm/bindings.cpp`) expose bulk arrays as typed-array views into WebAssembly memory. The worker copies them at once, because growing memory invalidates the views, and transfers the copies to the UI thread at zero cost. Errors cross the boundary as `{ok, errors}` objects; exceptions are disabled.

**Engine worker** (`web/src/engine.worker.ts`) owns the simulation and the time loop, so the UI thread only renders and handles input. Its step budget per tick adapts to measured step cost and stays under about 10 ms. When the engine can't keep up, simulated time runs slower instead of building a backlog.

**UI** (`web/src`):

- `Viewport` draws on a canvas every animation frame from the latest frame: graph paper with a true scale bar, strain-coloured springs, trails, and the analytic ghost for the oscillator.
- React re-renders readouts and charts (uPlot) at 10 Hz.
- `SetupPanel` validates every field inline before anything reaches the engine.

## Decisions and trade-offs

| Decision | Alternative | Why |
|---|---|---|
| C++ engine compiled to WebAssembly, with a React UI | SDL2 + Dear ImGui native app | A reviewer can use the real engine from a link, and the build forces the engine to stay UI-free. Cost: two toolchains. |
| Single-threaded WebAssembly in a worker | WebAssembly threads | Threads need cross-origin isolation (COOP/COEP headers), which static hosts such as GitHub Pages cannot set. One worker already keeps the UI responsive. |
| Canvas 2D | WebGL2 | Engine stepping dominates at the supported body counts, so WebGL would add complexity without a measured need. |
| uPlot | Recharts and similar | Streaming time series at 10 Hz with thousands of points; uPlot draws on canvas and is about 50 KB. |
| `double` everywhere | `float` (as in the original) | Validation down to 10⁻¹¹ requires it; memory is not the constraint at these sizes. |
| No exceptions across the WebAssembly boundary | Exceptions | Smaller, faster binary; every error is a value the UI can display. |
| `SINGLE_FILE` WebAssembly (embedded in the JS module) | A separate `.wasm` file | No path resolution issues in workers or bundlers, for about 33% more size on the wasm part (about 240 KB total). |
