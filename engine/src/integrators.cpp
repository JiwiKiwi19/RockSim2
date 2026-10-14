#include "rocksim/integrators.hpp"

namespace rocksim {

IntegratorInfo integratorInfo(IntegratorKind k) {
    switch (k) {
        case IntegratorKind::ExplicitEuler: return {"euler", "Explicit Euler", 1, false};
        case IntegratorKind::SymplecticEuler: return {"symplectic-euler", "Symplectic Euler", 1, true};
        case IntegratorKind::VelocityVerlet: return {"verlet", "Velocity Verlet", 2, true};
        case IntegratorKind::RK4: return {"rk4", "Runge-Kutta 4", 4, false};
    }
    return {"?", "?", 0, false};
}

bool parseIntegrator(std::string_view id, IntegratorKind& out) {
    for (auto k : {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler,
                   IntegratorKind::VelocityVerlet, IntegratorKind::RK4}) {
        if (integratorInfo(k).id == id) { out = k; return true; }
    }
    return false;
}

void Integrator::ensureSize(std::size_t n) {
    if (ax_.size() == n) return;
    ax_.assign(n, 0.0); ay_.assign(n, 0.0);
    for (int s = 0; s < 4; ++s) {
        kx_[s].assign(n, 0.0); ky_[s].assign(n, 0.0); kvx_[s].assign(n, 0.0); kvy_[s].assign(n, 0.0);
    }
    tx_.assign(n, 0.0); ty_.assign(n, 0.0); tvx_.assign(n, 0.0); tvy_.assign(n, 0.0);
    cacheValid_ = false;
}

void Integrator::eval(const World& w, const std::vector<double>& x, const std::vector<double>& y,
                      const std::vector<double>& vx, const std::vector<double>& vy,
                      std::vector<double>& ax, std::vector<double>& ay) {
    computeAccelerations(w, x, y, vx, vy, ax, ay);
    ++forceEvals_;
}

void Integrator::step(World& world, double dt) {
    Bodies& b = world.bodies;
    const std::size_t n = b.size();
    ensureSize(n);
    if (n == 0) return;

    switch (kind_) {
        case IntegratorKind::ExplicitEuler: {
            // x_{n+1} = x_n + dt v_n ;  v_{n+1} = v_n + dt a(x_n, v_n)
            // (The original app's update, with dt = 1 tick.)
            eval(world, b.x, b.y, b.vx, b.vy, ax_, ay_);
            for (std::size_t i = 0; i < n; ++i) {
                b.x[i] += dt * b.vx[i]; b.y[i] += dt * b.vy[i];
                b.vx[i] += dt * ax_[i]; b.vy[i] += dt * ay_[i];
            }
            cacheValid_ = false;
            break;
        }
        case IntegratorKind::SymplecticEuler: {
            // v_{n+1} = v_n + dt a(x_n, v_n) ;  x_{n+1} = x_n + dt v_{n+1}
            eval(world, b.x, b.y, b.vx, b.vy, ax_, ay_);
            for (std::size_t i = 0; i < n; ++i) {
                b.vx[i] += dt * ax_[i]; b.vy[i] += dt * ay_[i];
                b.x[i] += dt * b.vx[i]; b.y[i] += dt * b.vy[i];
            }
            cacheValid_ = false;
            break;
        }
        case IntegratorKind::VelocityVerlet: {
            // Kick-drift-kick:
            //   v_{n+1/2} = v_n + dt/2 a_n
            //   x_{n+1}   = x_n + dt v_{n+1/2}
            //   a_{n+1}   = a(x_{n+1}, v_{n+1/2})
            //   v_{n+1}   = v_{n+1/2} + dt/2 a_{n+1}
            // a_{n+1} is kept for the next step's first kick, so a
            // conservative system costs one force evaluation per step.
            if (!cacheValid_) eval(world, b.x, b.y, b.vx, b.vy, ax_, ay_);
            const double h = 0.5 * dt;
            for (std::size_t i = 0; i < n; ++i) {
                b.vx[i] += h * ax_[i]; b.vy[i] += h * ay_[i];
                b.x[i] += dt * b.vx[i]; b.y[i] += dt * b.vy[i];
            }
            eval(world, b.x, b.y, b.vx, b.vy, ax_, ay_);
            bool velocityDependent = false;
            for (const Spring& s : world.springs) velocityDependent |= (s.damping != 0.0);
            if (velocityDependent) {
                // Dashpot forces need v_{n+1}, which is what we are computing.
                // Using v_{n+1/2} instead leaves an O(dt^2) velocity error per
                // step and the method degrades to first order (caught by the
                // damped-oscillator test). One predictor-corrector pass fixes
                // it: predict v* = v_{n+1/2} + dt/2 a(x_{n+1}, v_{n+1/2}), then
                // re-evaluate a(x_{n+1}, v*). Cost: a second force evaluation.
                tvx_ = b.vx; tvy_ = b.vy;
                for (std::size_t i = 0; i < n; ++i) { tvx_[i] += h * ax_[i]; tvy_[i] += h * ay_[i]; }
                eval(world, b.x, b.y, tvx_, tvy_, ax_, ay_);
            }
            for (std::size_t i = 0; i < n; ++i) {
                b.vx[i] += h * ax_[i]; b.vy[i] += h * ay_[i];
            }
            // ax_ now holds a(x_{n+1}, v_{n+1}) exactly (conservative case) or
            // a(x_{n+1}, v*) with |v* - v_{n+1}| = O(dt^2) (damped case). Reusing
            // it for the next step's first kick adds an O(dt^3) local error, so
            // the method stays second order with 1 (or 2) evaluations per step.
            cacheValid_ = true;
            break;
        }
        case IntegratorKind::RK4: {
            // Classical 4-stage Runge-Kutta on the first-order system
            // (x, v)' = (v, a(x, v)).
            auto stage = [&](int s, double c) {
                // Trial state = y_n + c * dt * k_{s-1}  (c = 0 for the first stage).
                if (s == 0) {
                    tx_ = b.x; ty_ = b.y; tvx_ = b.vx; tvy_ = b.vy;
                } else {
                    for (std::size_t i = 0; i < n; ++i) {
                        tx_[i] = b.x[i] + c * dt * kx_[s - 1][i];
                        ty_[i] = b.y[i] + c * dt * ky_[s - 1][i];
                        tvx_[i] = b.vx[i] + c * dt * kvx_[s - 1][i];
                        tvy_[i] = b.vy[i] + c * dt * kvy_[s - 1][i];
                    }
                }
                kx_[s] = tvx_; ky_[s] = tvy_;
                eval(world, tx_, ty_, tvx_, tvy_, kvx_[s], kvy_[s]);
            };
            stage(0, 0.0);
            stage(1, 0.5);
            stage(2, 0.5);
            stage(3, 1.0);
            const double w = dt / 6.0;
            for (std::size_t i = 0; i < n; ++i) {
                b.x[i] += w * (kx_[0][i] + 2.0 * kx_[1][i] + 2.0 * kx_[2][i] + kx_[3][i]);
                b.y[i] += w * (ky_[0][i] + 2.0 * ky_[1][i] + 2.0 * ky_[2][i] + ky_[3][i]);
                b.vx[i] += w * (kvx_[0][i] + 2.0 * kvx_[1][i] + 2.0 * kvx_[2][i] + kvx_[3][i]);
                b.vy[i] += w * (kvy_[0][i] + 2.0 * kvy_[1][i] + 2.0 * kvy_[2][i] + kvy_[3][i]);
            }
            cacheValid_ = false;
            break;
        }
    }
}

}  // namespace rocksim
