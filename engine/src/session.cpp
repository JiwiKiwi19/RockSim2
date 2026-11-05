#include "rocksim/session.hpp"

#include <algorithm>
#include <cmath>

#include "rocksim/analytic.hpp"

namespace rocksim {

namespace {
constexpr double kTickBudgetMs = 10.0;
constexpr double kMaxWallPerTick = 0.1;  // a background tab must not request minutes at once

bool finite2(Vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

bool replaces(const Event& old, const Event& now) {
    if (old.kind != now.kind || old.step != now.step) return false;
    switch (now.kind) {
        case EventKind::GrabMove: return true;
        case EventKind::SetBody: return old.body == now.body;
        case EventKind::SetParam: return old.key == now.key;
        default: return false;
    }
}

CommandResult failure(std::string message) {
    CommandResult r;
    r.errors.push_back(std::move(message));
    return r;
}
}  // namespace

Session::Session(SessionConfig cfg) : cfg_(cfg) {}

bool Session::load(std::string_view scenario, const ParamValues& values, IntegratorKind kind, double dt,
                   std::vector<std::string>& errors) {
    BuiltScenario built;
    if (!buildScenario(scenario, values, built, errors)) return false;
    Simulation sim = Simulation::create(std::move(built.world), kind, dt, errors);
    if (!sim.valid()) return false;

    spec_ = findScenario(scenario);
    scenario_ = std::string(scenario);
    view_ = built.view;
    referencePeriod_ = built.referencePeriod;
    oscillator_ = scenario_ == "spring-oscillator";
    oscX0_ = 0.0;
    for (const ParamSpec& p : spec_->params)
        if (p.key == "x0") {
            const auto it = values.find("x0");
            oscX0_ = it != values.end() ? it->second : p.defaultValue;
        }

    const auto perHundredth = static_cast<std::size_t>(std::max(1.0, std::floor(0.01 / dt + 0.5)));
    trailStride_ = perHundredth;
    runs_.clear();
    Run a;
    a.sim = std::move(sim);
    a.stride = perHundredth;
    runs_.push_back(std::move(a));
    takeKeyframe(runs_[0]);
    record(0, runs_[0]);

    branchStep_ = 0;
    branchTime_ = 0.0;
    running_ = false;
    accumulator_ = 0.0;
    droppedTime_ = 0.0;
    msPerStep_ = 0.05;
    lastSteps_ = 0;
    selected_ = -1;
    selectBody(-1);
    return true;
}

void Session::setSpeed(double s) { speed_ = std::min(16.0, std::max(1.0 / 64.0, s)); }

// ---------------------------------------------------------------- time loop

int Session::tick(double wallSeconds, double lastComputeMs) {
    if (lastSteps_ > 0 && lastComputeMs >= 0.0)
        msPerStep_ = 0.8 * msPerStep_ + 0.2 * (lastComputeMs / lastSteps_);
    secondWall_ += wallSeconds;
    if (secondWall_ >= 1.0) {
        stepsPerSecond_ = static_cast<double>(secondSteps_) / secondWall_;
        secondSteps_ = 0;
        secondWall_ = 0.0;
    }
    lastSteps_ = 0;
    if (!running_ || !loaded()) return 0;

    const int maxSteps = std::max(1, static_cast<int>(kTickBudgetMs / std::max(msPerStep_, 1e-4)));
    accumulator_ += std::min(kMaxWallPerTick, wallSeconds) * speed_;
    int taken = 0;
    while (taken < maxSteps && accumulator_ >= runs_[0].sim.dt()) {
        const double dt = runs_[0].sim.dt();
        if (!advanceOne()) break;
        accumulator_ -= dt;
        ++taken;
    }
    // If the machine cannot keep up, drop the backlog instead of queueing it
    // (no "spiral of death"); the skipped simulated seconds are reported.
    if (accumulator_ >= runs_[0].sim.dt()) {
        droppedTime_ += accumulator_;
        accumulator_ = 0.0;
    }
    for (const Run& r : runs_)
        if (r.sim.status() != SimStatus::Ok) running_ = false;
    lastSteps_ = taken;
    secondSteps_ += taken;
    return taken;
}

int Session::step(int n) {
    running_ = false;
    accumulator_ = 0.0;
    int taken = 0;
    while (taken < n && loaded() && advanceOne()) ++taken;
    return taken;
}

bool Session::advanceOne() {
    for (const Run& r : runs_)
        if (r.sim.status() != SimStatus::Ok) return false;
    bool fresh[2] = {false, false};
    bool keyframed = false;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        Run& r = runs_[i];
        fresh[i] = r.sim.steps() >= r.head;
        r.sim.step();
        if (fresh[i]) {
            r.head = r.sim.steps();
            r.headTime = r.sim.time();
            // Before this boundary's events, so a restore re-applies them.
            if (r.head % static_cast<std::uint64_t>(cfg_.keyframeInterval) == 0) {
                takeKeyframe(r);
                keyframed = true;
            }
        }
        applyDue(r);
    }
    if (keyframed) enforceBudget();
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        Run& r = runs_[i];
        if (fresh[i]) record(static_cast<std::size_t>(i), r);
        if (r.sim.steps() % trailStride_ == 0) pushTrailPoints(r);
    }
    return true;
}

// ---------------------------------------------------------------- keyframes and seeking

void Session::takeKeyframe(Run& r) {
    Keyframe k;
    k.step = r.sim.steps();
    k.time = r.sim.time();
    k.snap = r.sim.capture();
    k.bytes = k.snap.bytes() + sizeof(Keyframe);
    r.keyframes.push_back(std::move(k));
}

std::size_t Session::keyframeBytes() const {
    std::size_t total = 0;
    for (const Run& r : runs_)
        for (const Keyframe& k : r.keyframes) total += k.bytes;
    return total;
}

void Session::enforceBudget() {
    while (keyframeBytes() > cfg_.keyframeBudgetBytes) {
        Run* victim = nullptr;
        for (Run& r : runs_)
            if (r.keyframes.size() > 1 && (!victim || r.keyframes.front().step < victim->keyframes.front().step))
                victim = &r;
        if (!victim) return;
        victim->keyframes.erase(victim->keyframes.begin());
        // Events older than the oldest keyframe can never be replayed again.
        const std::uint64_t oldest = victim->keyframes.front().step;
        const auto cut = std::lower_bound(victim->events.begin(), victim->events.end(), oldest,
                                          [](const Event& e, std::uint64_t s) { return e.step < s; });
        const auto removed = static_cast<std::size_t>(cut - victim->events.begin());
        victim->events.erase(victim->events.begin(), cut);
        victim->nextEvent -= std::min(victim->nextEvent, removed);
    }
}

std::uint64_t Session::oldestStep() const {
    std::uint64_t s = 0;
    for (const Run& r : runs_) s = std::max(s, r.keyframes.front().step);
    return s;
}

double Session::oldestTime() const {
    double t = 0.0;
    for (const Run& r : runs_) t = std::max(t, r.keyframes.front().time);
    return t;
}

std::uint64_t Session::seekLimit() const {
    std::uint64_t s = runs_[0].head;
    for (const Run& r : runs_) s = std::min(s, r.head);
    return s;
}

std::uint64_t Session::cursorStep() const { return runs_.empty() ? 0 : runs_[0].sim.steps(); }
double Session::cursorTime() const { return runs_.empty() ? 0.0 : runs_[0].sim.time(); }

void Session::applyDue(Run& r) {
    while (r.nextEvent < r.events.size() && r.events[r.nextEvent].step <= r.sim.steps())
        applyEvent(r, r.events[r.nextEvent++], nullptr);
}

void Session::seekRun(Run& r, std::uint64_t target) {
    const std::uint64_t cur = r.sim.steps();
    const auto it = std::upper_bound(r.keyframes.begin(), r.keyframes.end(), target,
                                     [](std::uint64_t s, const Keyframe& k) { return s < k.step; });
    const Keyframe& kf = *(it == r.keyframes.begin() ? it : it - 1);
    if (target < cur || kf.step > cur) {
        r.sim.restore(kf.snap);
        r.nextEvent = static_cast<std::size_t>(
            std::lower_bound(r.events.begin(), r.events.end(), kf.step,
                             [](const Event& e, std::uint64_t s) { return e.step < s; }) -
            r.events.begin());
        applyDue(r);
    }
    while (r.sim.steps() < target && r.sim.status() == SimStatus::Ok) {
        r.sim.step();
        applyDue(r);
    }
}

std::uint64_t Session::seek(std::uint64_t step) {
    if (!loaded()) return 0;
    const std::uint64_t target = std::min(std::max(step, oldestStep()), seekLimit());
    for (Run& r : runs_) seekRun(r, target);
    accumulator_ = 0.0;
    resetTrails();
    return target;
}

std::uint64_t Session::stepBack(std::uint64_t n) {
    const std::uint64_t cur = cursorStep();
    return seek(cur > n ? cur - n : 0);
}

std::uint64_t Session::seekTime(double t) {
    if (!loaded()) return 0;
    const Series& s = runs_[0].series;
    if (s.size() == 0 || t >= runs_[0].headTime) return seek(seekLimit());
    const auto it = std::lower_bound(s.t.begin(), s.t.end(), t);
    std::size_t i = static_cast<std::size_t>(it - s.t.begin());
    if (i > 0 && (i == s.size() || t - s.t[i - 1] < s.t[i] - t)) --i;
    return seek(static_cast<std::uint64_t>(s.step[i]));
}

// ---------------------------------------------------------------- samples and trails

void Session::record(std::size_t index, Run& r) {
    const std::uint64_t steps = r.sim.steps();
    if (steps % r.stride != 0) return;
    const Diagnostics d = r.sim.diagnostics();
    Series& s = r.series;
    s.step.push_back(static_cast<double>(steps));
    s.t.push_back(d.time);
    s.K.push_back(d.now.kinetic);
    s.U.push_back(d.now.potential);
    s.E.push_back(d.now.total());
    s.rel.push_back(d.relativeEnergyError);
    // Lockstep: when B records, A is at the same step.
    s.div.push_back(index == 1 && runs_.size() == 2 ? divergence() : 0.0);
    if (oscillator_) {
        s.numX.push_back(r.sim.world().bodies.x[1]);
        s.refX.push_back(referenceX(static_cast<int>(index)));
    }
    if (s.size() > cfg_.maxSamples) {
        r.stride *= 2;
        std::vector<double>* cols[] = {&s.step, &s.t, &s.K, &s.U, &s.E, &s.rel, &s.div, &s.numX, &s.refX};
        std::vector<char> keep(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) keep[i] = static_cast<std::uint64_t>(s.step[i]) % r.stride == 0;
        for (auto* c : cols) {
            if (c->size() != keep.size()) continue;
            std::size_t w = 0;
            for (std::size_t i = 0; i < keep.size(); ++i)
                if (keep[i]) (*c)[w++] = (*c)[i];
            c->resize(w);
        }
    }
}

void Session::resetTrails() {
    for (Run& r : runs_) {
        r.trails.assign(traced_.size(), Trail{std::vector<double>(2 * cfg_.trailCapacity), 0, 0});
        pushTrailPoints(r);
    }
}

void Session::pushTrailPoints(Run& r) {
    const Bodies& b = r.sim.world().bodies;
    const std::size_t cap = cfg_.trailCapacity;
    for (std::size_t k = 0; k < traced_.size() && k < r.trails.size(); ++k) {
        Trail& t = r.trails[k];
        std::size_t slot = (t.start + t.len) % cap;
        if (t.len == cap) { slot = t.start; t.start = (t.start + 1) % cap; }
        else ++t.len;
        const auto i = static_cast<std::size_t>(traced_[k]);
        t.pts[2 * slot] = b.x[i];
        t.pts[2 * slot + 1] = b.y[i];
    }
}

std::vector<double> Session::trailPoints(int run, std::vector<double>& counts) const {
    std::vector<double> out;
    counts.clear();
    const Run& r = runs_[static_cast<std::size_t>(run)];
    const std::size_t cap = cfg_.trailCapacity;
    for (const Trail& t : r.trails) {
        counts.push_back(static_cast<double>(t.len));
        for (std::size_t j = 0; j < t.len; ++j) {
            const std::size_t slot = (t.start + j) % cap;
            out.push_back(t.pts[2 * slot]);
            out.push_back(t.pts[2 * slot + 1]);
        }
    }
    return out;
}

void Session::selectBody(int body) {
    if (!loaded()) return;
    const int n = static_cast<int>(runs_[0].sim.world().bodies.size());
    selected_ = body >= 0 && body < n ? body : -1;
    traced_.clear();
    if (scenario_ == "kepler" || scenario_ == "figure-eight") {
        for (int i = 0; i < n; ++i) traced_.push_back(i);
    } else if (selected_ >= 0) {
        traced_.push_back(selected_);
    }
    resetTrails();
}

double Session::divergence() const {
    if (runs_.size() < 2) return 0.0;
    const Bodies& a = runs_[0].sim.world().bodies;
    const Bodies& b = runs_[1].sim.world().bodies;
    double sum = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const double dx = a.x[i] - b.x[i], dy = a.y[i] - b.y[i];
        sum += dx * dx + dy * dy;
    }
    return a.size() ? std::sqrt(sum / static_cast<double>(a.size())) : 0.0;
}

double Session::referenceX(int run) const {
    const Run& r = runs_[static_cast<std::size_t>(run)];
    if (!oscillator_ || r.sim.world().springs.empty()) return 0.0;
    const Spring& sp = r.sim.world().springs[0];
    return analytic::dampedOscillator(r.sim.world().bodies.mass[1], sp.stiffness, sp.damping, oscX0_, 0.0,
                                      r.sim.time()).x;
}

int Session::pick(int run, double x, double y, double tol) const {
    if (run < 0 || run >= runCount()) return -1;
    const Bodies& b = runs_[static_cast<std::size_t>(run)].sim.world().bodies;
    int best = -1;
    double bestD = tol * tol;
    for (std::size_t i = 0; i < b.size(); ++i) {
        const double edge = std::max(0.0, std::sqrt((b.x[i] - x) * (b.x[i] - x) + (b.y[i] - y) * (b.y[i] - y)) - b.radius[i]);
        if (edge * edge < bestD) { bestD = edge * edge; best = static_cast<int>(i); }
    }
    return best;
}

// ---------------------------------------------------------------- commands

bool Session::applyEvent(Run& r, const Event& ev, std::vector<std::string>* errors) {
    auto fail = [&](const char* msg) {
        if (errors) errors->push_back(msg);
        return false;
    };
    switch (ev.kind) {
        case EventKind::GrabStart:
            return r.sim.setGrab(ev.body, {ev.a, ev.b}) || fail("That body cannot be grabbed.");
        case EventKind::GrabMove:
            return r.sim.moveGrab({ev.a, ev.b}) || fail("Nothing is being held.");
        case EventKind::GrabEnd:
            r.sim.releaseGrab();
            return true;
        case EventKind::SetParam: {
            const std::string& key = spec_->params[static_cast<std::size_t>(ev.key)].key;
            if (!applyLiveParam(scenario_, key, ev.a, r.sim.mutableWorld()))
                return fail("That parameter cannot be changed while running, or the value is out of range.");
            const auto errs = r.sim.paramsChanged();
            if (!errs.empty() && errors) errors->insert(errors->end(), errs.begin(), errs.end());
            return errs.empty();
        }
        case EventKind::SetDt:
            return r.sim.setDt(ev.a) || fail("The time step must be a positive number.");
        case EventKind::SetIntegrator:
            r.sim.setIntegrator(static_cast<IntegratorKind>(ev.key));
            return true;
        case EventKind::SetBody:
            return r.sim.setBodyState(ev.body, {ev.a, ev.b}, {ev.c, ev.d}) ||
                   fail("That body cannot be moved.");
    }
    return false;
}

void Session::truncateAtCursor(Run& r) {
    const std::uint64_t s = r.sim.steps();
    r.events.resize(r.nextEvent);
    while (!r.keyframes.empty() && r.keyframes.back().step > s) r.keyframes.pop_back();
    Series& ser = r.series;
    std::size_t keep = ser.size();
    while (keep > 0 && static_cast<std::uint64_t>(ser.step[keep - 1]) > s) --keep;
    for (auto* c : {&ser.step, &ser.t, &ser.K, &ser.U, &ser.E, &ser.rel, &ser.div, &ser.numX, &ser.refX})
        if (c->size() > keep) c->resize(keep);
    r.head = s;
    r.headTime = r.sim.time();
}

CommandResult Session::command(int run, Event ev) {
    if (run < 0 || run >= runCount()) return failure("There is no such run.");
    // A drag that began in the past keeps editing the branch it started.
    if (run == 0 && runs_.size() == 2 && branchStep_ == runs_[0].sim.steps() && runs_[0].sim.steps() < runs_[0].head &&
        runs_[1].sim.steps() == branchStep_ && runs_[1].head == branchStep_ && !runs_[1].events.empty() &&
        replaces(runs_[1].events.back(), [&] { Event e = ev; e.step = branchStep_; return e; }()))
        run = 1;
    const auto idx = static_cast<std::size_t>(run);
    if (runs_[idx].sim.status() != SimStatus::Ok)
        return failure("The run is unstable. Rewind or restart to continue.");
    applyDue(runs_[idx]);  // events already logged at this step come first

    CommandResult res;
    const bool behind = runs_[idx].sim.steps() < runs_[idx].head;
    Run local;
    Run* target = &runs_[idx];
    if (behind) {
        local = runs_[idx];
        truncateAtCursor(local);
        target = &local;
    }
    ev.step = target->sim.steps();
    ev.time = target->sim.time();
    if (!applyEvent(*target, ev, &res.errors)) return res;

    // Many moves between two steps (a drag) collapse to the newest: every one of
    // these events sets an absolute value, so only the last one matters.
    if (!behind && !target->events.empty() && replaces(target->events.back(), ev))
        target->events.back() = ev;
    else
        target->events.push_back(ev);
    target->nextEvent = target->events.size();

    if (behind) {
        // A past edit never erases A's future: it lives on as run B.
        const std::size_t slot = run == 0 ? 1 : static_cast<std::size_t>(run);
        if (run == 0) {
            branchStep_ = target->sim.steps();
            branchTime_ = target->sim.time();
        }
        if (slot < runs_.size()) runs_[slot] = std::move(local);
        else runs_.push_back(std::move(local));
        res.branched = true;
        enforceBudget();
    }
    res.ok = true;
    return res;
}

CommandResult Session::grabStart(int run, std::uint32_t body, Vec2 target) {
    if (!finite2(target)) return failure("The target must be finite.");
    Event e; e.kind = EventKind::GrabStart; e.body = body; e.a = target.x; e.b = target.y;
    return command(run, e);
}

CommandResult Session::grabMove(int run, Vec2 target) {
    if (!finite2(target)) return failure("The target must be finite.");
    Event e; e.kind = EventKind::GrabMove; e.a = target.x; e.b = target.y;
    return command(run, e);
}

CommandResult Session::grabEnd(int run) {
    Event e; e.kind = EventKind::GrabEnd;
    return command(run, e);
}

CommandResult Session::setParam(int run, std::string_view key, double value) {
    if (!loaded()) return failure("No scenario is loaded.");
    for (std::size_t i = 0; i < spec_->params.size(); ++i) {
        if (spec_->params[i].key != key) continue;
        if (!spec_->params[i].live)
            return failure("'" + std::string(key) + "' cannot be changed while running; reset to apply it");
        Event e; e.kind = EventKind::SetParam; e.key = static_cast<int>(i); e.a = value;
        return command(run, e);
    }
    return failure("Unknown parameter '" + std::string(key) + "'");
}

CommandResult Session::setDt(int run, double dt) {
    Event e; e.kind = EventKind::SetDt; e.a = dt;
    return command(run, e);
}

CommandResult Session::setIntegrator(int run, IntegratorKind kind) {
    Event e; e.kind = EventKind::SetIntegrator; e.key = static_cast<int>(kind);
    return command(run, e);
}

CommandResult Session::setBody(int run, std::uint32_t body, Vec2 position, Vec2 velocity) {
    Event e; e.kind = EventKind::SetBody; e.body = body;
    e.a = position.x; e.b = position.y; e.c = velocity.x; e.d = velocity.y;
    return command(run, e);
}

}  // namespace rocksim
