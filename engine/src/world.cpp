#include "rocksim/world.hpp"

#include <cmath>
#include <cstdio>

namespace rocksim {

void Bodies::clear() {
    x.clear(); y.clear(); vx.clear(); vy.clear(); mass.clear(); radius.clear(); fixed.clear();
}

void Bodies::reserve(std::size_t n) {
    x.reserve(n); y.reserve(n); vx.reserve(n); vy.reserve(n);
    mass.reserve(n); radius.reserve(n); fixed.reserve(n);
}

std::uint32_t Bodies::add(Vec2 p, Vec2 v, double m, double r, bool isFixed) {
    x.push_back(p.x);
    y.push_back(p.y);
    vx.push_back(isFixed ? 0.0 : v.x);
    vy.push_back(isFixed ? 0.0 : v.y);
    mass.push_back(m);
    radius.push_back(r);
    fixed.push_back(isFixed ? 1 : 0);
    return static_cast<std::uint32_t>(x.size() - 1);
}

void computeAccelerations(const World& world,
                          std::span<const double> x, std::span<const double> y,
                          std::span<const double> vx, std::span<const double> vy,
                          std::span<double> ax, std::span<double> ay) {
    const Bodies& b = world.bodies;
    const WorldParams& p = world.params;
    const std::size_t n = b.size();

    // Uniform field: a = g for every free body (weight m g divided by m).
    // The original Qt app used a -= 1e-4 * m per tick, i.e. an acceleration
    // proportional to mass; that is corrected here (audit finding E6).
    for (std::size_t i = 0; i < n; ++i) {
        ax[i] = b.fixed[i] ? 0.0 : p.gravity.x;
        ay[i] = b.fixed[i] ? 0.0 : p.gravity.y;
    }

    // Springs: accumulate equal and opposite forces, then divide by mass.
    for (const Spring& s : world.springs) {
        const double dx = x[s.j] - x[s.i];
        const double dy = y[s.j] - y[s.i];
        const double len = std::sqrt(dx * dx + dy * dy);  // not hypot: see docs/physics.md, determinism
        if (len < 1e-15) continue;  // direction undefined; no force is the least-bad choice
        const double ux = dx / len;
        const double uy = dy / len;
        const double stretchRate = (vx[s.j] - vx[s.i]) * ux + (vy[s.j] - vy[s.i]) * uy;
        const double f = -(s.stiffness * (len - s.restLength) + s.damping * stretchRate);  // on j along u
        if (!b.fixed[s.j]) { ax[s.j] += f * ux / b.mass[s.j]; ay[s.j] += f * uy / b.mass[s.j]; }
        if (!b.fixed[s.i]) { ax[s.i] -= f * ux / b.mass[s.i]; ay[s.i] -= f * uy / b.mass[s.i]; }
    }

    // Direct-sum Newtonian gravity, O(N^2) over unordered pairs with the
    // Plummer-softened kernel a_i = G m_j d / (|d|^2 + eps^2)^{3/2}. Each pair
    // is visited once and applied symmetrically, so total momentum is
    // conserved to round-off by construction (Newton's third law).
    if (p.nbody) {
        const double eps2 = p.softening * p.softening;
        for (std::size_t i = 0; i < n; ++i) {
            if (b.fixed[i]) continue;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (b.fixed[j]) continue;
                const double dx = x[j] - x[i];
                const double dy = y[j] - y[i];
                const double r2 = dx * dx + dy * dy + eps2;
                if (r2 <= 0.0) continue;
                const double invR = 1.0 / std::sqrt(r2);
                const double s = p.G * invR * invR * invR;
                ax[i] += s * b.mass[j] * dx;
                ay[i] += s * b.mass[j] * dy;
                ax[j] -= s * b.mass[i] * dx;
                ay[j] -= s * b.mass[i] * dy;
            }
        }
    }
}

Invariants computeInvariants(const World& world) {
    const Bodies& b = world.bodies;
    const WorldParams& p = world.params;
    const std::size_t n = b.size();
    Invariants inv;
    double totalMass = 0.0;

    for (std::size_t i = 0; i < n; ++i) {
        if (b.fixed[i]) continue;
        const double m = b.mass[i];
        inv.kinetic += 0.5 * m * (b.vx[i] * b.vx[i] + b.vy[i] * b.vy[i]);
        // U = -m g . r  (zero at the origin)
        inv.potential -= m * (p.gravity.x * b.x[i] + p.gravity.y * b.y[i]);
        inv.momentum.x += m * b.vx[i];
        inv.momentum.y += m * b.vy[i];
        inv.angularMomentum += m * (b.x[i] * b.vy[i] - b.y[i] * b.vx[i]);
        inv.centerOfMass.x += m * b.x[i];
        inv.centerOfMass.y += m * b.y[i];
        totalMass += m;
    }
    if (totalMass > 0.0) inv.centerOfMass = inv.centerOfMass * (1.0 / totalMass);

    for (const Spring& s : world.springs) {
        const double dx = b.x[s.j] - b.x[s.i], dy = b.y[s.j] - b.y[s.i];
        const double ext = std::sqrt(dx * dx + dy * dy) - s.restLength;
        inv.potential += 0.5 * s.stiffness * ext * ext;
    }

    if (p.nbody) {
        // Potential consistent with the softened force: U = -G m_i m_j / sqrt(r^2 + eps^2)
        const double eps2 = p.softening * p.softening;
        for (std::size_t i = 0; i < n; ++i) {
            if (b.fixed[i]) continue;
            for (std::size_t j = i + 1; j < n; ++j) {
                if (b.fixed[j]) continue;
                const double dx = b.x[j] - b.x[i];
                const double dy = b.y[j] - b.y[i];
                const double r2 = dx * dx + dy * dy + eps2;
                if (r2 > 0.0) inv.potential -= p.G * b.mass[i] * b.mass[j] / std::sqrt(r2);
            }
        }
    }
    return inv;
}

namespace {
bool finite(double v) { return std::isfinite(v); }

template <typename... Args>
std::string fmt(const char* f, Args... args) {
    char buf[256];
    std::snprintf(buf, sizeof buf, f, args...);
    return buf;
}
}  // namespace

std::vector<std::string> validateWorld(const World& world) {
    std::vector<std::string> errs;
    const Bodies& b = world.bodies;
    const WorldParams& p = world.params;
    const std::size_t n = b.size();

    const bool sizesMatch = b.y.size() == n && b.vx.size() == n && b.vy.size() == n &&
                            b.mass.size() == n && b.radius.size() == n && b.fixed.size() == n;
    if (!sizesMatch) {
        errs.emplace_back("internal: body arrays have different lengths");
        return errs;
    }

    constexpr std::size_t kMaxReported = 5;  // keep messages readable for large scenes
    std::size_t bodyErrors = 0;
    auto report = [&](std::string msg) {
        if (bodyErrors++ < kMaxReported) errs.push_back(std::move(msg));
    };

    for (std::size_t i = 0; i < n; ++i) {
        if (!finite(b.x[i]) || !finite(b.y[i]) || !finite(b.vx[i]) || !finite(b.vy[i]))
            report(fmt("body %zu: position and velocity must be finite numbers", i));
        if (!finite(b.mass[i]) || b.mass[i] <= 0.0)
            report(fmt("body %zu: mass must be a positive number (got %g kg)", i, b.mass[i]));
        if (!finite(b.radius[i]) || b.radius[i] < 0.0)
            report(fmt("body %zu: radius must be >= 0 (got %g m)", i, b.radius[i]));
        if (p.walls) {
            const Box& bx = p.box;
            if (2.0 * b.radius[i] >= bx.xmax - bx.xmin || 2.0 * b.radius[i] >= bx.ymax - bx.ymin)
                report(fmt("body %zu: diameter %g m does not fit in the box", i, 2.0 * b.radius[i]));
            else if (b.x[i] - b.radius[i] < bx.xmin - 1e-12 || b.x[i] + b.radius[i] > bx.xmax + 1e-12 ||
                     b.y[i] - b.radius[i] < bx.ymin - 1e-12 || b.y[i] + b.radius[i] > bx.ymax + 1e-12)
                report(fmt("body %zu: starts outside the box", i));
        }
    }
    if (bodyErrors > kMaxReported)
        errs.push_back(fmt("... and %zu more body errors", bodyErrors - kMaxReported));

    for (std::size_t s = 0; s < world.springs.size(); ++s) {
        const Spring& sp = world.springs[s];
        if (sp.i >= n || sp.j >= n || sp.i == sp.j) {
            errs.push_back(fmt("spring %zu: must connect two different existing bodies", s));
            continue;
        }
        if (!finite(sp.stiffness) || sp.stiffness < 0.0)
            errs.push_back(fmt("spring %zu: stiffness must be >= 0 N/m", s));
        if (!finite(sp.restLength) || sp.restLength < 0.0)
            errs.push_back(fmt("spring %zu: rest length must be >= 0 m", s));
        if (!finite(sp.damping) || sp.damping < 0.0)
            errs.push_back(fmt("spring %zu: damping must be >= 0 N s/m", s));
    }

    if (!finite(p.gravity.x) || !finite(p.gravity.y)) errs.emplace_back("gravity must be finite");
    if (!finite(p.G) || p.G < 0.0) errs.emplace_back("G must be >= 0");
    if (!finite(p.softening) || p.softening < 0.0) errs.emplace_back("softening must be >= 0");
    if (!(p.restitution >= 0.0 && p.restitution <= 1.0))
        errs.emplace_back("restitution must be in [0, 1]");
    if (!(p.wallRestitution >= 0.0 && p.wallRestitution <= 1.0))
        errs.emplace_back("wall restitution must be in [0, 1]");
    if (p.walls) {
        const Box& bx = p.box;
        if (!(finite(bx.xmin) && finite(bx.xmax) && finite(bx.ymin) && finite(bx.ymax)) ||
            !(bx.xmax > bx.xmin) || !(bx.ymax > bx.ymin))
            errs.emplace_back("box must have finite bounds with max > min");
    }
    return errs;
}

}  // namespace rocksim
