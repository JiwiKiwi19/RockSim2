#include "rocksim/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rocksim {

namespace {
double energyScale(const Invariants& inv) {
    return std::max({std::abs(inv.total()), std::abs(inv.kinetic), std::abs(inv.potential), 1e-300});
}
}  // namespace

Simulation Simulation::create(World world, IntegratorKind integrator, double dt,
                              std::vector<std::string>& errors) {
    errors = validateWorld(world);
    if (!std::isfinite(dt) || dt <= 0.0) errors.emplace_back("time step dt must be a positive number");
    Simulation sim;
    if (!errors.empty()) return sim;
    sim.world_ = std::move(world);
    sim.lastGoodParams_ = sim.world_.params;
    sim.integrator_.setKind(integrator);
    sim.dt_ = dt;
    sim.initial_ = computeInvariants(sim.world_);
    sim.valid_ = true;
    return sim;
}

bool Simulation::setDt(double dt) {
    if (!std::isfinite(dt) || dt <= 0.0) return false;
    dt_ = dt;
    accumulator_ = 0.0;
    return true;
}

std::vector<std::string> Simulation::paramsChanged() {
    auto errs = validateWorld(world_);
    if (!errs.empty()) {
        world_.params = lastGoodParams_;
        return errs;
    }
    lastGoodParams_ = world_.params;
    integrator_.invalidate();
    // A parameter change (e.g. gravity) changes the energy function itself, so
    // drift is measured from the moment of the change.
    initial_ = computeInvariants(world_);
    return errs;
}

void Simulation::step() {
    if (!valid_ || status_ != SimStatus::Ok) return;
    integrator_.step(world_, dt_);
    if (world_.params.collisions || world_.params.walls) {
        lastContacts_ = resolveContacts(world_, grid_, pairs_);
        if (lastContacts_.impulses || lastContacts_.wallHits || lastContacts_.bodyContacts)
            integrator_.invalidate();
    }
    time_ += dt_;
    ++steps_;
    checkHealth();
}

int Simulation::stepN(int n) {
    int taken = 0;
    for (; taken < n && status_ == SimStatus::Ok; ++taken) step();
    return taken;
}

int Simulation::advance(double simSeconds, int maxSteps) {
    if (!valid_ || status_ != SimStatus::Ok || !(simSeconds > 0.0)) return 0;
    accumulator_ += simSeconds;
    int taken = 0;
    while (accumulator_ >= dt_ && taken < maxSteps && status_ == SimStatus::Ok) {
        step();
        accumulator_ -= dt_;
        ++taken;
    }
    if (accumulator_ >= dt_) {
        droppedTime_ += accumulator_;
        accumulator_ = 0.0;
    }
    return taken;
}

void Simulation::checkHealth() {
    const Bodies& b = world_.bodies;
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (!std::isfinite(b.x[i]) || !std::isfinite(b.y[i]) || !std::isfinite(b.vx[i]) || !std::isfinite(b.vy[i])) {
            status_ = SimStatus::Unstable;
            char buf[200];
            std::snprintf(buf, sizeof buf,
                          "Body %zu reached a non-finite state at t = %.6g s. The time step is too "
                          "large for this integrator and these parameters; reduce dt and reset.",
                          i, time_);
            statusMessage_ = buf;
            return;
        }
    }
    // None of the models inject energy (no drivers; e <= 1; dampers only remove
    // energy), so a large energy *increase* can only be numerical. The check is
    // O(N) except for N-body (O(N^2) potential), which is checked less often
    // to keep it out of the hot path.
    const std::uint64_t every = world_.params.nbody ? 64 : 8;
    if (steps_ % every == 0) {
        const Invariants now = computeInvariants(world_);
        const double growth = (now.total() - initial_.total()) / energyScale(initial_);
        if (!std::isfinite(growth) || growth > 10.0) {
            status_ = SimStatus::Unstable;
            char buf[220];
            std::snprintf(buf, sizeof buf,
                          "Total energy grew by %.3g x its initial scale by t = %.6g s although no force "
                          "adds energy: numerical instability. Reduce dt or use a stable integrator.",
                          growth, time_);
            statusMessage_ = buf;
        }
    }
}

Diagnostics Simulation::diagnostics() const {
    Diagnostics d;
    d.time = time_;
    d.steps = steps_;
    d.now = computeInvariants(world_);
    d.initial = initial_;
    d.relativeEnergyError = (d.now.total() - initial_.total()) / energyScale(initial_);
    d.lastContacts = lastContacts_;
    return d;
}

std::uint64_t stateHash(const Bodies& b) {
    std::uint64_t h = 1469598103934665603ULL;
    for (const auto* v : {&b.x, &b.y, &b.vx, &b.vy})
        for (double d : *v) {
            std::uint64_t u = 0;
            std::memcpy(&u, &d, sizeof u);
            h ^= u;
            h *= 1099511628211ULL;
        }
    return h;
}

}  // namespace rocksim
