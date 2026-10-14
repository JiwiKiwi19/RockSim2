# Physics and numerics

This document covers what RockSim simulates, the equations it uses, the numerical methods and their assumptions, and where it stops being accurate. File references are to `engine/`.

## State and units

Bodies are point masses with an optional contact radius. They are stored as a structure of arrays (`Bodies` in `world.hpp`): `x, y, vx, vy, mass, radius, fixed`. All values are `double`.

- Units are SI (m, kg, s, J) except in the gravitational scenarios. Those are non-dimensional with G = 1, which the UI and `rocksim-cli list` state.
- A *fixed* body has infinite inertia. No force moves it, and contacts treat it as immovable. It is used for spring anchors. Fixed bodies are excluded from kinetic energy and momentum, because they are external constraints.

## Forces (`world.cpp`, `computeAccelerations`)

All smooth forces are summed into an acceleration a(x, v) for each body.

**Uniform field.** a = g for every free body. Weight is m g, so the acceleration does not depend on mass. The original app applied `vy -= 1e-4 * mass` per tick (audit finding E6).

**Damped linear spring** between bodies i and j, with d = x_j − x_i and unit vector d̂:

```
F_j = −[ k (|d| − L) + c ((v_j − v_i) · d̂) ] d̂,     F_i = −F_j
```

Here k is the stiffness, L the rest length and c a dashpot acting along the spring. The potential is ½ k (|d| − L)². The direction is undefined at |d| = 0, where no force is applied. The oscillator scenario places its anchor so the spring never comes close to zero length. A test found this matters: when the anchor sits at the turning point, overshooting integrators flip the spring's direction, and the system is no longer the 1-D oscillator.

**Newtonian gravity** with Plummer softening ε:

```
a_i = Σ_j  G m_j d_ij / (|d_ij|² + ε²)^{3/2},     U = −Σ_{i<j} G m_i m_j / √(|d_ij|² + ε²)
```

Each unordered pair is computed once and applied with equal and opposite signs, so total momentum is conserved to round-off. A finite-difference test checks that the force is −∇U for the softened potential. Cost is O(N²) per evaluation.

## Integrators (`integrators.cpp`)

The smooth dynamics x″ = a(x, v) are integrated with one of:

| Method | Update | Global order | Symplectic | Force evaluations per step |
|---|---|---|---|---|
| Explicit Euler | x += dt v; v += dt a(x, v) | 1 | no | 1 |
| Symplectic Euler | v += dt a(x, v); x += dt v | 1 | yes | 1 |
| Velocity Verlet | kick ½, drift, kick ½ | 2 | yes | 1 (2 with damping) |
| RK4 | classical four-stage | 4 | no | 4 |

**Why four methods?** Each one illustrates a different trade-off, and the Verify view measures it:

- Explicit Euler is the original app's method and the baseline. For an undamped oscillator its energy grows by a factor of (1 + ω² dt²) every step, so it is unstable at any dt. The engine detects this and stops the run.
- Symplectic Euler costs exactly the same as explicit Euler but keeps the energy error bounded. That makes it the cheapest method you can leave running.
- Velocity Verlet is second order and symplectic, and needs one force evaluation per step because it reuses the last acceleration. It is the default for most scenarios.
- RK4 is the most accurate per step at fourth order, but costs four evaluations and is not symplectic. Over very long runs its energy error grows steadily instead of oscillating.

"Bounded" is not "small". On the Kepler orbit at dt = 0.01, RK4's energy error after 200 orbits (6.9 × 10⁻⁶) is still far below Verlet's bounded error (8.9 × 10⁻⁴), but RK4's grows with time and Verlet's does not. Which is better depends on how long you run and what you care about.

### Velocity Verlet with damping

The kick–drift–kick form needs a(x_{n+1}, v_{n+1}) for the final half-kick. With velocity-dependent forces, such as spring dashpots, v_{n+1} is the unknown. Evaluating at the half-step velocity leaves an O(dt²) local velocity error, and the method falls to first order. The damped convergence test caught this, measuring an order of about 1.0. The engine adds one predictor–corrector pass:

```
v*      = v_{n+1/2} + dt/2 · a(x_{n+1}, v_{n+1/2})
v_{n+1} = v_{n+1/2} + dt/2 · a(x_{n+1}, v*)
```

The last evaluation is reused for the next step's first kick. Its velocity argument differs from v_{n+1} by O(dt²), which adds only O(dt³) local error. The measured order with damping is 2.000, at two force evaluations per step. The method is no longer symplectic in this case, which does not matter for a dissipative system.

## Time stepping (`simulation.cpp`)

The step size dt is fixed for a run. `Simulation::advance(simSeconds, maxSteps)` adds the requested simulated time to an accumulator and takes whole steps while at least dt remains ([Fiedler, "Fix Your Timestep!"](https://gafferongames.com/post/fix_your_timestep/)). Consequences:

- Frame rate, timer jitter and playback speed change how much simulated time passes per real second, never the physics.
- If a step budget is exceeded, the backlog is dropped instead of queued, and reported as `droppedTime`. Queuing it would make every later frame slower.

Each step integrates the smooth forces, resolves contacts, then runs a health check:

- A non-finite position or velocity stops the run with a message naming the body and time.
- No model injects energy: there are no drivers, e ≤ 1, and dampers only remove energy. So a total-energy increase above 10× the initial energy scale can only be numerical. The run is stopped with that explanation. The check runs every 8 steps (every 64 with N-body, where the potential costs O(N²)).

## Contacts (`contacts.cpp`)

Contacts are handled by operator splitting: after the integrator step, overlaps are found and resolved.

**Broad phase.** A uniform grid with cells at least 2 r_max wide on each axis. Overlapping discs have centres closer than r_i + r_j ≤ 2 r_max, so a 3×3 neighbourhood search is exhaustive. Each body (not each occupied cell) drives its own search, and each pair is reported from its lower index. Bodies are bucketed with a counting sort into flat arrays, so no per-cell allocation happens per step. If a few bodies escape far away, the cell count is capped and cells grow; larger cells are still correct, only slower. Tests require the output to equal an O(N²) search on random, clustered and far-flung scenes.

**Response.** For each overlapping pair, in sorted order:

1. A positional correction removes the overlap, split in proportion to inverse mass. The centre of mass does not move, so momentum is unaffected.
2. If the bodies are approaching (v_n < 0, with n from a to b), a normal impulse is applied:

```
J = −(1 + e) v_n / (1/m_a + 1/m_b),    Δv_a = −J n / m_a,    Δv_b = +J n / m_b
```

This conserves linear momentum exactly and makes the post-collision normal velocity −e v_n. It matches the closed-form 1-D collision for every mass ratio and e tested. The original app split the impulse 50/50 regardless of mass, and mapped one slider to different restitution formulas for walls and pairs (audit findings E4 and E5).

**Walls.** A body past a wall is moved back inside by e times its penetration depth (an exact mirror for e = 1), and its normal velocity is reflected and scaled by e. Walls are frictionless.

**Limitations.** Detection is discrete: a body that moves more than about its radius in one step can pass through another. Multi-body contacts are resolved by sequential pairwise impulses, which is an approximation. There is no friction and no rotation.

## Reproducibility

The same scenario, parameters, seed, integrator, dt and step count give a **bit-identical** state on every platform tested: x86-64 GCC 13, ARM64 GCC 11, and WebAssembly from Emscripten 3.1.6 / Clang 15. A 64-bit FNV-1a hash over the state bits (`stateHash`, `rocksim-cli hash`) is pinned in a test. The browser shows it live.

The first attempt was not bit-identical. Native and WebAssembly initial conditions already differed at step 0. The causes were, with the fixes:

- **libm functions that are not correctly rounded.** `sin`, `cos`, `cbrt` and `hypot` may differ in the last bit between glibc (native Linux) and musl (Emscripten). IEEE 754 requires +, −, ×, ÷ and `sqrt` to be correctly rounded, so state-affecting code now uses only those:
  - random directions come from rejection sampling in the unit disc (`Pcg32::unitVector`), not from an angle
  - the cube root is a fixed-iteration Newton method (`portableCbrt`)
  - distances use `sqrt(dx*dx + dy*dy)`
- **Random distributions.** `std::uniform_real_distribution` is implementation-defined, so the engine uses PCG32, checked against the reference output, with its own distributions.
- **FMA contraction.** Clang, including Apple Clang on arm64, fuses `a*b + c` into a single fused multiply-add by default, which rounds differently. The engine is compiled with `-ffp-contract=off`.

`analytic.hpp` (used only for comparison, never for state) still uses libm.

## Scenario details

| Scenario | Default integrator, dt | Notes |
|---|---|---|
| Particle box | Verlet, 1 ms | 2 m × 2 m box; a 1 kg disc has radius r; heavy discs have radius r·m^(1/3) (constant density, as in the original). Placement rejects overlaps and refuses packings above 45% area fraction. |
| Damped oscillator | Verlet, 10 ms | Anchor at x = −1.6 m with rest length 1.6 m, so equilibrium is the origin and the spring stays at least 0.7 m long. |
| Soft body | Verlet, 1 ms | n × m nodes; structural springs at rest length h, shear springs at h√2; initial rigid spin ω. |
| Two-body orbit | Verlet, 1 ms | G = 1, a = 1, m₁ = 1; starts at periapsis with the vis-viva speed, in the centre-of-mass frame. |
| Figure-eight | RK4, 1 ms | Simó's initial conditions as published by [Chenciner & Montgomery (2000)](https://arxiv.org/abs/math/0011268): x₁ = −x₂ = (0.97000436, −0.24308753), x₃ = 0, v₃ = (−0.93240737, −0.86473146), v₁ = v₂ = −v₃/2. |
| Orbital disk | Verlet, 2 ms | Central mass 1 plus N light bodies on near-circular orbits; the centre's velocity is set to cancel the disk's momentum. |

## References

- E. Hairer, C. Lubich, G. Wanner, "Geometric numerical integration illustrated by the Störmer–Verlet method", *Acta Numerica* 12 (2003). [doi:10.1017/S0962492902000144](https://doi.org/10.1017/S0962492902000144)
- A. Chenciner, R. Montgomery, "A remarkable periodic solution of the three-body problem in the case of equal masses", *Annals of Mathematics* 152 (2000). [arXiv:math/0011268](https://arxiv.org/abs/math/0011268)
- G. Fiedler, "Fix Your Timestep!" [gafferongames.com](https://gafferongames.com/post/fix_your_timestep/)
- M. E. O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically Good Algorithms for Random Number Generation" (2014). [pcg-random.org](https://www.pcg-random.org/paper.html)
