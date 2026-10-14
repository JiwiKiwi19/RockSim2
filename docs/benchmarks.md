# Benchmarks

These benchmarks cover only the parts where cost is a real design question: the collision broad phase, a full particle step, the integrators, direct-sum gravity, and native against WebAssembly speed.

## Method

- **Native:** Google Benchmark (`bench/bench_main.cpp`), Release build (`-O3`, the `bench` preset), `--benchmark_min_time=0.2s`. The raw output is in [`benchmark-raw.txt`](benchmark-raw.txt).
- **Native vs WebAssembly:** the same scenarios, stepped 500 to 5,000 times after 50 warm-up steps, timed with `std::chrono::steady_clock` (native) and `performance.now()` in Node 22 (WebAssembly, `-O3`).
- **Machine:** cloud VM, 2 vCPU Intel Xeon @ 2.10 GHz, Ubuntu 24.04, GCC 13.3, single-threaded, measured 2026-10-14. Google Benchmark warned that the system's copy of its library was built in debug mode; that affects the harness, not the measured engine code.
- Shared VMs are noisy, so treat differences under about 10% as noise. Re-run on your machine with `cmake --preset bench && cmake --build --preset bench && ./build/bench/bench/rocksim_bench`.

## Broad phase: uniform grid vs brute force

Discs of radius 0.01 at a constant 10% area fraction, so the number of neighbours per disc stays fixed while N grows.

| N | Grid | Brute force | Speed-up |
|---|---|---|---|
| 256 | 8.4 µs | 37.8 µs | 4.5× |
| 1,024 | 47.8 µs | 726 µs | 15× |
| 4,096 | 282 µs | 7.48 ms | 27× |
| 16,384 | 1.26 ms | 108 ms | 86× |
| 65,536 | 6.84 ms | not run | — |

Google Benchmark's fitted complexity: brute force O(N²) (1% RMS); grid O(N log N) (6% RMS). The log factor comes from sorting the output pairs so that contacts are resolved in a deterministic order. The search itself is linear.

## One full step of the particle box (integrate + contacts), Verlet

| Discs | Native | WebAssembly | WebAssembly / native |
|---|---|---|---|
| 300 (r = 8 mm) | 12.3 µs | — | — |
| 1,000 | 70 µs | 91 µs | 1.30× |
| 3,000 | 337 µs | 445 µs | 1.32× |

At the default dt = 1 ms, real time needs 1,000 steps/s. In the browser, 3,000 discs therefore use about 45% of one core.

## Integrator cost per step (soft body, 96 nodes, 326 damped springs)

| Integrator | Time per step | Force evaluations |
|---|---|---|
| Explicit Euler | 3.6 µs | 1 |
| Symplectic Euler | 3.6 µs | 1 |
| Velocity Verlet | 12.6 µs | 2 (damped springs, see physics.md) |
| RK4 | 18.3 µs | 4 |

Verlet costs more than twice Euler here: beyond its second force evaluation, it copies the velocity arrays for the corrector pass. RK4 costs 5× Euler for 4× the evaluations, so per evaluation its overhead is small.

## Direct-sum gravity

| Bodies | Time per Verlet step |
|---|---|
| 50 | 6.7 µs |
| 128 | 36.5 µs |
| 256 | 142 µs |
| 400 | 369 µs (native), 540 µs (WebAssembly) |

The fit is O(N²) (3% RMS). At 400 bodies and dt = 1 ms the browser stays in real time at about 55% of one core. A Barnes–Hut tree would be the next step for larger N.

## What was not measured

- Rendering cost in the browser: Canvas 2D at these body counts was not profiled.
- The original app's performance. It does not build as delivered, and the benchmark table in its README could not be reproduced. See the [audit](audit.md).
