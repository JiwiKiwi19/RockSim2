#pragma once

#include <string_view>
#include <vector>

#include "rocksim/world.hpp"

namespace rocksim {

/// Time integrators for the smooth part of the dynamics, x'' = a(x, v).
///
/// | method            | global order | symplectic | force evals / step |
/// |-------------------|--------------|------------|--------------------|
/// | ExplicitEuler     | 1            | no         | 1                  |
/// | SymplecticEuler   | 1            | yes        | 1                  |
/// | VelocityVerlet    | 2            | yes        | 1 (2 with damping) |
/// | RK4               | 4            | no         | 4                  |
///
/// "Symplectic" applies to velocity-independent (conservative) forces: such
/// methods keep the energy error bounded over long runs instead of letting it
/// drift (Hairer, Lubich & Wanner, Acta Numerica 2003). Spring damping is
/// velocity-dependent; there Verlet adds one predictor-corrector force
/// evaluation to stay second order. It is then no longer symplectic, which
/// does not matter for a dissipative system. Contacts are applied after the step, so these orders
/// hold only between contact events.
enum class IntegratorKind { ExplicitEuler = 0, SymplecticEuler = 1, VelocityVerlet = 2, RK4 = 3 };

struct IntegratorInfo {
    std::string_view id;    ///< stable machine name: "euler", "symplectic-euler", "verlet", "rk4"
    std::string_view name;  ///< display name
    int order;
    bool symplectic;
};

[[nodiscard]] IntegratorInfo integratorInfo(IntegratorKind k);
/// Parses an id from `IntegratorInfo::id`; returns false if unknown.
bool parseIntegrator(std::string_view id, IntegratorKind& out);

class Integrator {
public:
    explicit Integrator(IntegratorKind kind = IntegratorKind::VelocityVerlet) : kind_(kind) {}

    [[nodiscard]] IntegratorKind kind() const { return kind_; }
    void setKind(IntegratorKind k) { kind_ = k; invalidate(); }

    /// Advances world.bodies by dt under computeAccelerations(). Allocation-free
    /// after the first call for a given body count.
    void step(World& world, double dt);

    /// Must be called whenever positions, velocities or force parameters change
    /// outside step() (contacts, user edits). Verlet otherwise reuses a(x_n)
    /// from the end of the previous step ("first same as last").
    void invalidate() { cacheValid_ = false; }

    /// Force evaluations performed since construction (for benchmarks / tests).
    [[nodiscard]] long long forceEvaluations() const { return forceEvals_; }

private:
    void ensureSize(std::size_t n);
    void eval(const World& w, const std::vector<double>& x, const std::vector<double>& y,
              const std::vector<double>& vx, const std::vector<double>& vy,
              std::vector<double>& ax, std::vector<double>& ay);

    IntegratorKind kind_;
    bool cacheValid_ = false;
    long long forceEvals_ = 0;
    std::vector<double> ax_, ay_;
    // RK4 stage storage: k-values for x' = v and v' = a, plus a trial state.
    std::vector<double> kx_[4], ky_[4], kvx_[4], kvy_[4];
    std::vector<double> tx_, ty_, tvx_, tvy_;
};

}  // namespace rocksim
