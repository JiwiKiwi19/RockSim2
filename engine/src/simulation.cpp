#include "rocksim/simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace rocksim {

namespace {
constexpr double kMaxGrabOmega = 30.0;  // [1/s]; min(30, 0.2 / dt) keeps omega * dt <= 0.2 at any dt

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
    if (world_.grab) world_.grab->omega = std::min(kMaxGrabOmega, 0.2 / dt_);
    return true;
}

bool Simulation::inputsOk(std::uint32_t body, Vec2 p) const {
    return valid_ && status_ == SimStatus::Ok && body < world_.bodies.size() && !world_.bodies.fixed[body] &&
           std::isfinite(p.x) && std::isfinite(p.y);
}

Vec2 Simulation::clampInside(std::uint32_t body, Vec2 p) const {
    const WorldParams& wp = world_.params;
    if (!wp.walls) return p;
    const double r = world_.bodies.radius[body];
    p.x = std::clamp(p.x, wp.box.xmin + r, wp.box.xmax - r);
    p.y = std::clamp(p.y, wp.box.ymin + r, wp.box.ymax - r);
    return p;
}

bool Simulation::setBodyState(std::uint32_t body, Vec2 position, Vec2 velocity) {
    if (!inputsOk(body, position) || !std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return false;
    const Vec2 p = clampInside(body, position);
    Bodies& b = world_.bodies;
    b.x[body] = p.x; b.y[body] = p.y;
    b.vx[body] = velocity.x; b.vy[body] = velocity.y;
    integrator_.invalidate();
    rebaseline();
    return true;
}

bool Simulation::setGrab(std::uint32_t body, Vec2 target) {
    if (!inputsOk(body, target)) return false;
    world_.grab = Grab{body, clampInside(body, target), std::min(kMaxGrabOmega, 0.2 / dt_)};
    integrator_.invalidate();
    return true;
}

bool Simulation::moveGrab(Vec2 target) {
    if (!world_.grab || !inputsOk(world_.grab->body, target)) return false;
    world_.grab->target = clampInside(world_.grab->body, target);
    integrator_.invalidate();
    return true;
}

void Simulation::releaseGrab() {
    if (!world_.grab) return;
    world_.grab.reset();
    integrator_.invalidate();
    rebaseline();
}

SimSnapshot Simulation::capture() const {
    SimSnapshot s;
    const Bodies& b = world_.bodies;
    s.x = b.x; s.y = b.y; s.vx = b.vx; s.vy = b.vy;
    s.params = world_.params;
    s.grab = world_.grab;
    s.kind = integrator_.kind();
    s.cache = integrator_.cache();
    s.dt = dt_; s.time = time_; s.accumulator = accumulator_; s.droppedTime = droppedTime_;
    s.steps = steps_;
    s.lastContacts = lastContacts_;
    s.initial = initial_;
    s.status = status_;
    s.statusMessage = statusMessage_;
    return s;
}

void Simulation::restore(const SimSnapshot& s) {
    Bodies& b = world_.bodies;
    b.x = s.x; b.y = s.y; b.vx = s.vx; b.vy = s.vy;
    world_.params = s.params;
    lastGoodParams_ = s.params;
    world_.grab = s.grab;
    integrator_.setKind(s.kind);  // invalidates; the saved cache is put back next
    integrator_.restoreCache(s.cache);
    dt_ = s.dt; time_ = s.time; accumulator_ = s.accumulator; droppedTime_ = s.droppedTime;
    steps_ = s.steps;
    lastContacts_ = s.lastContacts;
    initial_ = s.initial;
    status_ = s.status;
    statusMessage_ = s.statusMessage;
}

std::size_t SimSnapshot::bytes() const {
    return sizeof(SimSnapshot) + sizeof(double) * (x.capacity() + y.capacity() + vx.capacity() + vy.capacity() +
                                                   cache.ax.capacity() + cache.ay.capacity()) +
           statusMessage.capacity();
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
    // energy), so a large energy *increase* can only be numerical. The user's
    // grab does add energy, so the check is suspended while one is held. The check is
    // O(N) except for N-body (O(N^2) potential), which is checked less often
    // to keep it out of the hot path.
    const std::uint64_t every = world_.params.nbody ? 64 : 8;
    if (!world_.grab && steps_ % every == 0) {
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
