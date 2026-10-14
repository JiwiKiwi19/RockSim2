#include "rocksim/scenarios.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "rocksim/analytic.hpp"
#include "rocksim/rng.hpp"

namespace rocksim {

namespace {

ParamSpec P(std::string key, std::string label, std::string unit, double min, double max, double def,
            double step, bool integer, bool live, std::string help) {
    return ParamSpec{std::move(key), std::move(label), std::move(unit), min, max, def, step, integer, live, std::move(help)};
}

std::vector<ScenarioSpec> makeCatalog() {
    std::vector<ScenarioSpec> c;

    c.push_back({"particle-box", "Particle box",
                 "Discs in a 2 m x 2 m box with elastic or inelastic collisions. The original RockSim "
                 "model, rebuilt with a correct broad phase and mass-weighted impulses.",
                 "SI (m, kg, s)",
                 {P("count", "Particles", "", 1, 3000, 300, 1, true, false, "Number of discs"),
                  P("radius", "Radius", "m", 0.003, 0.05, 0.02, 0.001, false, false, "Radius of a 1 kg disc"),
                  P("heavyFraction", "Heavy fraction", "", 0, 0.5, 0.1, 0.01, false, false,
                    "Share of discs that are heavy (constant density: r grows as m^(1/3))"),
                  P("heavyMass", "Heavy mass", "kg", 1, 50, 8, 0.5, false, false, "Mass of a heavy disc"),
                  P("speed", "Initial speed", "m/s", 0, 3, 1, 0.05, false, false, "Maximum initial speed"),
                  P("gravity", "Gravity", "m/s²", 0, 20, 0, 0.1, false, true, "Downward field strength g"),
                  P("restitution", "Restitution e", "", 0, 1, 1, 0.01, false, true, "Disc-disc coefficient of restitution"),
                  P("wallRestitution", "Wall restitution", "", 0, 1, 1, 0.01, false, true, "Disc-wall coefficient of restitution"),
                  P("seed", "Seed", "", 1, 999999, 42, 1, true, false, "Random seed for initial conditions")},
                 IntegratorKind::VelocityVerlet, 1e-3,
                 "Momentum is conserved exactly by every disc-disc impulse; with e = 1 and no gravity, "
                 "kinetic energy is conserved. Wall bounces change momentum by design."});

    c.push_back({"spring-oscillator", "Damped oscillator",
                 "A mass on a spring attached to a fixed anchor. The exact solution is drawn alongside "
                 "the numerical one, so the integrator error is visible directly.",
                 "SI (m, kg, s)",
                 {P("mass", "Mass", "kg", 0.1, 10, 1, 0.1, false, false, "Mass of the moving body"),
                  P("stiffness", "Stiffness k", "N/m", 0.1, 1000, 10, 0.1, false, false, "Spring constant"),
                  P("damping", "Damping c", "N·s/m", 0, 20, 0, 0.05, false, false, "Dashpot coefficient"),
                  P("x0", "Initial displacement", "m", -0.9, 0.9, 0.5, 0.01, false, false, "Starting offset from equilibrium")},
                 IntegratorKind::VelocityVerlet, 1e-2,
                 "Compared against the closed-form solution of m x'' + c x' + k x = 0 at every step."});

    c.push_back({"soft-body", "Soft body",
                 "A lattice of point masses joined by structural and shear springs, dropped onto the "
                 "floor of a box. Stiff springs make explicit methods go unstable at large dt.",
                 "SI (m, kg, s)",
                 {P("cols", "Columns", "", 2, 16, 8, 1, true, false, "Nodes across"),
                  P("rows", "Rows", "", 2, 16, 6, 1, true, false, "Nodes down"),
                  P("spacing", "Spacing", "m", 0.04, 0.15, 0.09, 0.005, false, false, "Rest distance between neighbouring nodes"),
                  P("nodeMass", "Node mass", "kg", 0.01, 1, 0.05, 0.01, false, false, "Mass of each node"),
                  P("stiffness", "Stiffness k", "N/m", 10, 5000, 600, 10, false, false, "Spring constant of every spring"),
                  P("damping", "Damping c", "N·s/m", 0, 5, 0.15, 0.01, false, false, "Dashpot coefficient of every spring"),
                  P("gravity", "Gravity", "m/s²", 0, 20, 9.81, 0.1, false, true, "Downward field strength g"),
                  P("wallRestitution", "Wall restitution", "", 0, 1, 0.3, 0.01, false, true, "Node-wall coefficient of restitution"),
                  P("dropHeight", "Drop height", "m", 0, 1.2, 0.6, 0.05, false, false, "Gap between the body and the floor"),
                  P("spin", "Initial spin", "rad/s", -6, 6, 1.5, 0.1, false, false, "Initial angular velocity")},
                 IntegratorKind::VelocityVerlet, 1e-3,
                 "Qualitative. Energy can only decrease (dampers, e < 1); a rise means instability."});

    c.push_back({"kepler", "Two-body orbit",
                 "Two bodies under Newtonian gravity in their centre-of-mass frame. The period is "
                 "known exactly from Kepler's third law.",
                 "non-dimensional (G = 1, semi-major axis a = 1)",
                 {P("eccentricity", "Eccentricity", "", 0, 0.95, 0.6, 0.01, false, false, "Orbit eccentricity"),
                  P("massRatio", "Mass ratio m₂/m₁", "", 0.001, 1, 0.2, 0.001, false, false, "Secondary over primary mass")},
                 IntegratorKind::VelocityVerlet, 1e-3,
                 "Period T = 2π√(a³/GM); energy, momentum and angular momentum are conserved."});

    c.push_back({"figure-eight", "Figure-eight three-body",
                 "Three equal masses chasing each other around a figure eight (Chenciner & Montgomery, "
                 "2000). A tiny perturbation shows the orbit's sensitivity.",
                 "non-dimensional (G = 1, m = 1)",
                 {P("perturbation", "Perturbation", "", -0.05, 0.05, 0, 0.001, false, false,
                    "Offset added to body 1's x position")},
                 IntegratorKind::RK4, 1e-3,
                 "Returns to its initial state after T = 6.32591398; total momentum is exactly zero."});

    c.push_back({"orbital-disk", "Orbital disk",
                 "Many light bodies on near-circular orbits around a heavy central body, with softened "
                 "mutual gravity. Shows O(N²) direct summation at work.",
                 "non-dimensional (G = 1)",
                 {P("count", "Bodies", "", 2, 400, 150, 1, true, false, "Orbiting bodies"),
                  P("diskMass", "Disk mass", "", 0, 0.5, 0.05, 0.01, false, false, "Total mass of the disk (central mass = 1)"),
                  P("softening", "Softening ε", "", 0.001, 0.2, 0.02, 0.001, false, true, "Plummer softening length"),
                  P("seed", "Seed", "", 1, 999999, 7, 1, true, false, "Random seed")},
                 IntegratorKind::VelocityVerlet, 2e-3,
                 "Qualitative. Total momentum is conserved to round-off; energy error is reported."});
    return c;
}

double get(const ScenarioSpec& spec, const ParamValues& v, const std::string& key) {
    if (auto it = v.find(key); it != v.end()) return it->second;
    for (const auto& p : spec.params)
        if (p.key == key) return p.defaultValue;
    return 0.0;
}

template <typename... Args>
std::string fmt(const char* f, Args... args) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}

// --- builders -------------------------------------------------------------

bool buildParticleBox(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>& errors) {
    const int count = static_cast<int>(get(s, v, "count"));
    const double r = get(s, v, "radius");
    const double heavyFraction = get(s, v, "heavyFraction");
    const double heavyMass = get(s, v, "heavyMass");
    const double speed = get(s, v, "speed");
    Pcg32 rng(static_cast<std::uint64_t>(get(s, v, "seed")));

    World& w = out.world;
    w.params.walls = true;
    w.params.box = Box{-1.0, 1.0, -1.0, 1.0};
    w.params.collisions = true;
    w.params.gravity = {0.0, -get(s, v, "gravity")};
    w.params.restitution = get(s, v, "restitution");
    w.params.wallRestitution = get(s, v, "wallRestitution");
    out.view = w.params.box;

    const int heavyCount = static_cast<int>(std::lround(heavyFraction * count));
    const double heavyR = r * portableCbrt(heavyMass);  // constant density, as in the original app
    const double area = (count - heavyCount) * std::numbers::pi * r * r + heavyCount * std::numbers::pi * heavyR * heavyR;
    if (area > 0.45 * 4.0) {
        errors.push_back(fmt("The discs cover %.0f%% of the box; above 45%% they cannot be placed without "
                             "overlap. Reduce the count or the radius.", 100.0 * area / 4.0));
        return false;
    }

    w.bodies.reserve(static_cast<std::size_t>(count));
    // Place heavy discs first (hardest to fit), rejecting overlaps.
    for (int i = 0; i < count; ++i) {
        const bool heavy = i < heavyCount;
        const double ri = heavy ? heavyR : r;
        const double mi = heavy ? heavyMass : 1.0;
        bool placed = false;
        for (int attempt = 0; attempt < 2000 && !placed; ++attempt) {
            const double px = rng.uniform(-1.0 + ri, 1.0 - ri);
            const double py = rng.uniform(-1.0 + ri, 1.0 - ri);
            bool clear = true;
            for (std::size_t j = 0; j < w.bodies.size() && clear; ++j) {
                const double dx = w.bodies.x[j] - px, dy = w.bodies.y[j] - py;
                const double rs = w.bodies.radius[j] + ri;
                clear = dx * dx + dy * dy >= rs * rs;
            }
            if (!clear) continue;
            const Vec2 dir = rng.unitVector();
            const double sp = rng.uniform(0.0, speed);
            w.bodies.add({px, py}, dir * sp, mi, ri);
            placed = true;
        }
        if (!placed) {
            errors.emplace_back("Could not place every disc without overlap. Reduce the count or the radius.");
            return false;
        }
    }
    return true;
}

bool buildOscillator(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>&) {
    World& w = out.world;
    // Anchor at x = -1.6, rest length 1.6, so equilibrium is the origin and x(t)
    // of body 1 is the displacement used by the analytic solution. The spring
    // never shrinks below 0.7 m for |x0| <= 0.9, keeping its direction defined.
    w.bodies.add({-1.6, 0.0}, {}, 1.0, 0.04, /*fixed=*/true);
    w.bodies.add({get(s, v, "x0"), 0.0}, {}, get(s, v, "mass"), 0.07);
    w.springs.push_back({0, 1, get(s, v, "stiffness"), 1.6, get(s, v, "damping")});
    out.view = Box{-1.75, 1.15, -1.45, 1.45};
    const double m = get(s, v, "mass"), k = get(s, v, "stiffness");
    out.referencePeriod = 2.0 * std::numbers::pi * std::sqrt(m / k);
    return true;
}

bool buildSoftBody(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>& errors) {
    const int cols = static_cast<int>(get(s, v, "cols"));
    const int rows = static_cast<int>(get(s, v, "rows"));
    const double h = get(s, v, "spacing");
    const double m = get(s, v, "nodeMass");
    const double k = get(s, v, "stiffness");
    const double c = get(s, v, "damping");
    const double spin = get(s, v, "spin");
    const double nodeR = 0.25 * h;

    World& w = out.world;
    w.params.walls = true;
    w.params.box = Box{-1.0, 1.0, -1.0, 1.0};
    w.params.gravity = {0.0, -get(s, v, "gravity")};
    w.params.wallRestitution = get(s, v, "wallRestitution");
    out.view = w.params.box;

    const double width = (cols - 1) * h, height = (rows - 1) * h;
    const double y0 = -1.0 + nodeR + get(s, v, "dropHeight");
    if (width + 2 * nodeR >= 2.0 || y0 + height + nodeR > 1.0) {
        errors.emplace_back("The body does not fit in the box at this size and drop height. "
                            "Reduce rows, columns, spacing or drop height.");
        return false;
    }
    const double cx = 0.0, cy = y0 + 0.5 * height;
    for (int r = 0; r < rows; ++r)
        for (int col = 0; col < cols; ++col) {
            const double px = -0.5 * width + col * h, py = y0 + r * h;
            // Rigid rotation about the centre: v = omega x (r - c)
            w.bodies.add({px, py}, {-spin * (py - cy), spin * (px - cx)}, m, nodeR);
        }
    auto id = [cols](int r, int col) { return static_cast<std::uint32_t>(r * cols + col); };
    const double diag = h * std::numbers::sqrt2;
    for (int r = 0; r < rows; ++r)
        for (int col = 0; col < cols; ++col) {
            if (col + 1 < cols) w.springs.push_back({id(r, col), id(r, col + 1), k, h, c});
            if (r + 1 < rows) w.springs.push_back({id(r, col), id(r + 1, col), k, h, c});
            if (r + 1 < rows && col + 1 < cols) {
                w.springs.push_back({id(r, col), id(r + 1, col + 1), k, diag, c});
                w.springs.push_back({id(r, col + 1), id(r + 1, col), k, diag, c});
            }
        }
    return true;
}

bool buildKepler(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>&) {
    const double e = get(s, v, "eccentricity");
    const double q = get(s, v, "massRatio");
    const double G = 1.0, a = 1.0, m1 = 1.0, m2 = q, M = m1 + m2;
    // Start at periapsis: relative separation r_p = a(1 - e) with the vis-viva speed.
    const double rp = a * (1.0 - e);
    const double vp = std::sqrt(G * M * (1.0 + e) / rp);
    World& w = out.world;
    w.params.nbody = true;
    w.params.G = G;
    // Positions and velocities split about the centre of mass (total momentum 0).
    w.bodies.add({-m2 / M * rp, 0.0}, {0.0, -m2 / M * vp}, m1, 0.06);
    w.bodies.add({m1 / M * rp, 0.0}, {0.0, m1 / M * vp}, m2, 0.06 * std::max(0.4, portableCbrt(q)));
    const double half = 1.15 * a * (1.0 + e);
    out.view = Box{-half, half, -half, half};
    out.referencePeriod = analytic::keplerPeriod(G, M, a);
    return true;
}

bool buildFigureEight(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>&) {
    // Initial conditions for the figure-eight choreography as published by
    // Chenciner & Montgomery (Annals of Mathematics 152, 2000), computed by
    // C. Simo: x1 = -x2 = (0.97000436, -0.24308753), x3 = 0,
    // v3 = (-0.93240737, -0.86473146), v1 = v2 = -v3 / 2.
    const double dp = get(s, v, "perturbation");
    const Vec2 x1{0.97000436 + dp, -0.24308753};
    const Vec2 v3{-0.93240737, -0.86473146};
    World& w = out.world;
    w.params.nbody = true;
    w.params.G = 1.0;
    w.bodies.add(x1, v3 * -0.5, 1.0, 0.05);
    w.bodies.add({-0.97000436, 0.24308753}, v3 * -0.5, 1.0, 0.05);
    w.bodies.add({0.0, 0.0}, v3, 1.0, 0.05);
    out.view = Box{-1.4, 1.4, -1.4, 1.4};
    out.referencePeriod = analytic::kFigureEightPeriod;
    return true;
}

bool buildOrbitalDisk(const ScenarioSpec& s, const ParamValues& v, BuiltScenario& out, std::vector<std::string>&) {
    const int n = static_cast<int>(get(s, v, "count"));
    const double diskMass = get(s, v, "diskMass");
    Pcg32 rng(static_cast<std::uint64_t>(get(s, v, "seed")));
    World& w = out.world;
    w.params.nbody = true;
    w.params.G = 1.0;
    w.params.softening = get(s, v, "softening");
    const double m = diskMass / n;
    w.bodies.reserve(static_cast<std::size_t>(n) + 1);
    w.bodies.add({0.0, 0.0}, {0.0, 0.0}, 1.0, 0.06);
    Vec2 p{};  // momentum of the disk, removed from the centre afterwards
    for (int i = 0; i < n; ++i) {
        const double r = rng.uniform(0.3, 1.0);
        const Vec2 u = rng.unitVector();
        // Circular speed around the enclosed mass (central body + disk inside r,
        // treating the disk as uniform in r): a starting point, not an equilibrium.
        const double enclosed = 1.0 + diskMass * (r - 0.3) / 0.7;
        const double vc = std::sqrt(enclosed / r);
        const Vec2 vel{-vc * u.y, vc * u.x};
        w.bodies.add(u * r, vel, m, 0.012);
        p += vel * m;
    }
    w.bodies.vx[0] = -p.x;  // central mass is 1, so v = -p / 1
    w.bodies.vy[0] = -p.y;
    out.view = Box{-1.25, 1.25, -1.25, 1.25};
    return true;
}

}  // namespace

const std::vector<ScenarioSpec>& scenarioCatalog() {
    static const std::vector<ScenarioSpec> catalog = makeCatalog();
    return catalog;
}

const ScenarioSpec* findScenario(std::string_view id) {
    for (const auto& s : scenarioCatalog())
        if (s.id == id) return &s;
    return nullptr;
}

bool buildScenario(std::string_view id, const ParamValues& values, BuiltScenario& out,
                   std::vector<std::string>& errors) {
    errors.clear();
    out = BuiltScenario{};
    const ScenarioSpec* spec = findScenario(id);
    if (!spec) {
        errors.push_back("Unknown scenario '" + std::string(id) + "'");
        return false;
    }
    for (const auto& entry : values) {
        const std::string& key = entry.first;  // not a structured binding: lambda capture of
        const double val = entry.second;       // those needs Clang >= 16 (Emscripten 3.1 ships 15)
        auto it = std::find_if(spec->params.begin(), spec->params.end(), [&](const ParamSpec& p) { return p.key == key; });
        if (it == spec->params.end()) {
            errors.push_back("Unknown parameter '" + key + "' for scenario '" + spec->id + "'");
            continue;
        }
        if (!std::isfinite(val) || val < it->min || val > it->max)
            errors.push_back(fmt("%s must be between %g and %g%s%s (got %g)", it->label.c_str(), it->min, it->max,
                                 it->unit.empty() ? "" : " ", it->unit.c_str(), val));
        else if (it->integer && val != std::floor(val))
            errors.push_back(it->label + " must be a whole number");
    }
    if (!errors.empty()) return false;

    bool ok = false;
    if (spec->id == "particle-box") ok = buildParticleBox(*spec, values, out, errors);
    else if (spec->id == "spring-oscillator") ok = buildOscillator(*spec, values, out, errors);
    else if (spec->id == "soft-body") ok = buildSoftBody(*spec, values, out, errors);
    else if (spec->id == "kepler") ok = buildKepler(*spec, values, out, errors);
    else if (spec->id == "figure-eight") ok = buildFigureEight(*spec, values, out, errors);
    else if (spec->id == "orbital-disk") ok = buildOrbitalDisk(*spec, values, out, errors);
    if (ok) {
        auto worldErrors = validateWorld(out.world);
        errors.insert(errors.end(), worldErrors.begin(), worldErrors.end());
        ok = worldErrors.empty();
    }
    return ok;
}

bool applyLiveParam(std::string_view scenarioId, std::string_view key, double value, World& w) {
    const ScenarioSpec* spec = findScenario(scenarioId);
    if (!spec) return false;
    auto it = std::find_if(spec->params.begin(), spec->params.end(), [&](const ParamSpec& p) { return p.key == key; });
    if (it == spec->params.end() || !it->live) return false;
    if (!std::isfinite(value) || value < it->min || value > it->max) return false;
    if (key == "gravity") w.params.gravity = {0.0, -value};
    else if (key == "restitution") w.params.restitution = value;
    else if (key == "wallRestitution") w.params.wallRestitution = value;
    else if (key == "softening") w.params.softening = value;
    else return false;
    return true;
}

}  // namespace rocksim
