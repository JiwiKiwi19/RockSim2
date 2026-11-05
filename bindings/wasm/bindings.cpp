// Embind interface between the C++ engine and the browser.
//
// Design: the engine stays free of any Emscripten dependency; this file is the
// only place that knows about JavaScript. WasmSession is a thin shell over
// rocksim::Session: it converts types and holds no logic of its own. Bulk data (positions, velocities,
// radii, spring topology) is exposed as typed-array *views* into WebAssembly
// memory, so reading it costs no copy here. Views are invalidated if memory
// grows, so the worker copies them into its outgoing snapshot immediately.
// Nothing here throws: errors are returned as { ok, errors } objects.
#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdio>
#include <string>
#include <vector>

#include "rocksim/rocksim.hpp"

using emscripten::val;
using namespace rocksim;

namespace {

val toArray(const std::vector<std::string>& v) {
    val a = val::array();
    for (std::size_t i = 0; i < v.size(); ++i) a.set(i, v[i]);
    return a;
}

val result(bool ok, const std::vector<std::string>& errors) {
    val r = val::object();
    r.set("ok", ok);
    r.set("errors", toArray(errors));
    return r;
}

val invariantsToVal(const Invariants& inv) {
    val o = val::object();
    o.set("kinetic", inv.kinetic);
    o.set("potential", inv.potential);
    o.set("total", inv.total());
    o.set("px", inv.momentum.x);
    o.set("py", inv.momentum.y);
    o.set("angularMomentum", inv.angularMomentum);
    o.set("comX", inv.centerOfMass.x);
    o.set("comY", inv.centerOfMass.y);
    return o;
}

class WasmSession {
public:
    /// params: plain JS object { key: number }. integrator: id string.
    val load(const std::string& scenario, const val& params, const std::string& integrator, double dt) {
        std::vector<std::string> errors;
        ParamValues values;
        const val keys = val::global("Object").call<val>("keys", params);
        const int n = keys["length"].as<int>();
        for (int i = 0; i < n; ++i) {
            const std::string k = keys[i].as<std::string>();
            const val v = params[k];
            if (v.typeOf().as<std::string>() != "number") {
                errors.push_back("Parameter '" + k + "' must be a number");
                continue;
            }
            values[k] = v.as<double>();
        }
        IntegratorKind kind{};
        if (!parseIntegrator(integrator, kind)) errors.push_back("Unknown integrator '" + integrator + "'");
        if (!errors.empty()) return result(false, errors);
        if (!session_.load(scenario, values, kind, dt, errors)) return result(false, errors);

        springPairs_.clear();
        springRest_.clear();
        for (const Spring& s : session_.sim(0).world().springs) {
            springPairs_.push_back(s.i);
            springPairs_.push_back(s.j);
            springRest_.push_back(s.restLength);
        }
        return result(true, {});
    }

    // time loop
    void play() { session_.play(); }
    void pause() { session_.pause(); }
    bool running() const { return session_.running(); }
    void setSpeed(double v) { session_.setSpeed(v); }
    int tick(double wallSeconds, double lastComputeMs) { return session_.tick(wallSeconds, lastComputeMs); }
    int step(int n) { return session_.step(n); }

    // time travel
    double seek(double step) { return static_cast<double>(session_.seek(toStep(step))); }
    double seekTime(double t) { return static_cast<double>(session_.seekTime(t)); }
    double stepBack(double n) { return static_cast<double>(session_.stepBack(toStep(n))); }

    // commands
    val grabStart(int run, int body, double x, double y) {
        if (body < 0) return result(false, {"That body cannot be grabbed."});
        return command(session_.grabStart(run, static_cast<std::uint32_t>(body), {x, y}));
    }
    val grabMove(int run, double x, double y) { return command(session_.grabMove(run, {x, y})); }
    val grabEnd(int run) { return command(session_.grabEnd(run)); }
    val setParam(int run, const std::string& key, double value) { return command(session_.setParam(run, key, value)); }
    val setDt(int run, double dt) { return command(session_.setDt(run, dt)); }
    val setIntegrator(int run, const std::string& id) {
        IntegratorKind k{};
        if (!parseIntegrator(id, k)) return result(false, {"Unknown integrator '" + id + "'"});
        return command(session_.setIntegrator(run, k));
    }
    val setBody(int run, int body, double x, double y, double vx, double vy) {
        if (body < 0) return result(false, {"That body cannot be moved."});
        return command(session_.setBody(run, static_cast<std::uint32_t>(body), {x, y}, {vx, vy}));
    }
    int pick(int run, double x, double y, double tolerance) const { return session_.pick(run, x, y, tolerance); }
    void selectBody(int body) { session_.selectBody(body); }

    // state of the bodies
    int runCount() const { return session_.runCount(); }
    int count() const { return static_cast<int>(session_.sim(0).world().bodies.size()); }
    val x(int run) const { return view(session_.sim(run).world().bodies.x); }
    val y(int run) const { return view(session_.sim(run).world().bodies.y); }
    val vx(int run) const { return view(session_.sim(run).world().bodies.vx); }
    val vy(int run) const { return view(session_.sim(run).world().bodies.vy); }
    val radius() const { return view(session_.sim(0).world().bodies.radius); }
    val mass() const { return view(session_.sim(0).world().bodies.mass); }
    val fixed() const {
        const auto& f = session_.sim(0).world().bodies.fixed;
        return val(emscripten::typed_memory_view(f.size(), f.data()));
    }
    val springs() const { return val(emscripten::typed_memory_view(springPairs_.size(), springPairs_.data())); }
    val springRest() const { return view(springRest_); }

    val trailPoints(int run) {
        trailScratch_ = session_.trailPoints(run, trailCounts_);
        return view(trailScratch_);
    }
    val trailCounts() const { return view(trailCounts_); }
    val traced() const {
        val a = val::array();
        const auto& t = session_.traced();
        for (std::size_t i = 0; i < t.size(); ++i) a.set(i, t[i]);
        return a;
    }
    /// Flat (step, time, kind) triples.
    val events(int run) {
        eventScratch_.clear();
        for (const Event& e : session_.events(run)) {
            eventScratch_.push_back(static_cast<double>(e.step));
            eventScratch_.push_back(e.time);
            eventScratch_.push_back(static_cast<double>(e.kind));
        }
        return view(eventScratch_);
    }
    val series(int run) const {
        const Series& s = session_.series(run);
        val o = val::object();
        o.set("step", view(s.step)); o.set("t", view(s.t));
        o.set("K", view(s.K)); o.set("U", view(s.U)); o.set("E", view(s.E));
        o.set("rel", view(s.rel)); o.set("div", view(s.div));
        o.set("numX", view(s.numX)); o.set("refX", view(s.refX));
        return o;
    }

    val diagnostics(int run) const {
        const Simulation& sim = session_.sim(run);
        const Diagnostics d = sim.diagnostics();
        val o = val::object();
        o.set("time", d.time);
        o.set("steps", static_cast<double>(d.steps));
        o.set("now", invariantsToVal(d.now));
        o.set("initial", invariantsToVal(d.initial));
        o.set("relativeEnergyError", d.relativeEnergyError);
        o.set("contacts", static_cast<double>(d.lastContacts.bodyContacts));
        o.set("impulses", static_cast<double>(d.lastContacts.impulses));
        o.set("wallHits", static_cast<double>(d.lastContacts.wallHits));
        o.set("droppedTime", session_.droppedTime());
        o.set("forceEvaluations", static_cast<double>(sim.forceEvaluations()));
        o.set("status", sim.status() == SimStatus::Ok ? std::string("ok") : std::string("unstable"));
        o.set("statusMessage", sim.statusMessage());
        o.set("dt", sim.dt());
        o.set("integrator", std::string(integratorInfo(sim.integratorKind()).id));
        o.set("grabbed", sim.grabbed());
        o.set("eventCount", static_cast<double>(session_.events(run).size()));
        o.set("headStep", static_cast<double>(session_.headStep(run)));
        o.set("headTime", session_.headTime(run));
        if (session_.hasReference()) o.set("referenceX", session_.referenceX(run));
        return o;
    }

    /// Current values of the live parameters of a run, { key: number }.
    val liveParams(int run) const {
        val o = val::object();
        const ScenarioSpec* spec = findScenario(session_.scenarioId());
        if (!spec) return o;
        for (const ParamSpec& p : spec->params) {
            double v = 0.0;
            if (p.live && readLiveParam(p.key, session_.sim(run).world(), v)) o.set(p.key, v);
        }
        return o;
    }

    /// Timeline and loop state shared by all runs.
    val timeline() const {
        val o = val::object();
        o.set("runs", session_.runCount());
        o.set("running", session_.running());
        o.set("cursorStep", static_cast<double>(session_.cursorStep()));
        o.set("cursorTime", session_.cursorTime());
        o.set("oldestStep", static_cast<double>(session_.oldestStep()));
        o.set("oldestTime", session_.oldestTime());
        o.set("seekLimit", static_cast<double>(session_.seekLimit()));
        o.set("stepsPerSecond", session_.stepsPerSecond());
        o.set("divergence", session_.divergence());
        o.set("selected", session_.selected());
        o.set("branchTime", session_.branchTime());
        o.set("keyframeBytes", static_cast<double>(session_.keyframeBytes()));
        return o;
    }

    /// Hex FNV-1a hash of the state bits (see rocksim::stateHash).
    std::string hash(int run) const {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx",
                      static_cast<unsigned long long>(stateHash(session_.sim(run).world().bodies)));
        return buf;
    }

    val meta() const {
        val o = val::object();
        const Box& v = session_.view();
        o.set("scenario", session_.scenarioId());
        o.set("xmin", v.xmin); o.set("xmax", v.xmax);
        o.set("ymin", v.ymin); o.set("ymax", v.ymax);
        o.set("referencePeriod", session_.referencePeriod());
        const WorldParams& p = session_.sim(0).world().params;
        o.set("walls", p.walls);
        o.set("boxXmin", p.box.xmin); o.set("boxXmax", p.box.xmax);
        o.set("boxYmin", p.box.ymin); o.set("boxYmax", p.box.ymax);
        return o;
    }

private:
    static val view(const std::vector<double>& v) { return val(emscripten::typed_memory_view(v.size(), v.data())); }
    static std::uint64_t toStep(double v) { return v > 0.0 ? static_cast<std::uint64_t>(v) : 0; }
    static val command(const CommandResult& r) {
        val o = result(r.ok, r.errors);
        o.set("branched", r.branched);
        return o;
    }

    Session session_;
    std::vector<std::uint32_t> springPairs_;
    std::vector<double> springRest_;
    std::vector<double> trailScratch_, trailCounts_, eventScratch_;
};

val catalog() {
    val list = val::array();
    int i = 0;
    for (const ScenarioSpec& s : scenarioCatalog()) {
        val o = val::object();
        o.set("id", s.id);
        o.set("title", s.title);
        o.set("summary", s.summary);
        o.set("units", s.units);
        o.set("validation", s.validation);
        val tryThis = val::array();
        for (std::size_t k = 0; k < s.tryThis.size(); ++k) tryThis.set(k, s.tryThis[k]);
        o.set("tryThis", tryThis);
        o.set("defaultIntegrator", std::string(integratorInfo(s.defaultIntegrator).id));
        o.set("defaultDt", s.defaultDt);
        val params = val::array();
        int j = 0;
        for (const ParamSpec& p : s.params) {
            val q = val::object();
            q.set("key", p.key); q.set("label", p.label); q.set("unit", p.unit);
            q.set("min", p.min); q.set("max", p.max); q.set("default", p.defaultValue);
            q.set("step", p.step); q.set("integer", p.integer); q.set("live", p.live); q.set("help", p.help);
            params.set(j++, q);
        }
        o.set("params", params);
        list.set(i++, o);
    }
    return list;
}

val integrators() {
    val list = val::array();
    int i = 0;
    for (auto k : {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler, IntegratorKind::VelocityVerlet,
                   IntegratorKind::RK4}) {
        const auto info = integratorInfo(k);
        val o = val::object();
        o.set("id", std::string(info.id));
        o.set("name", std::string(info.name));
        o.set("order", info.order);
        o.set("symplectic", info.symplectic);
        list.set(i++, o);
    }
    return list;
}

val convergence(const std::string& id, double damping, int levels) {
    IntegratorKind k{};
    val o = val::object();
    if (!parseIntegrator(id, k) || levels < 2 || levels > 10 || !(damping >= 0.0)) {
        o.set("ok", false);
        return o;
    }
    const auto r = analysis::convergenceStudy(k, damping, levels);
    val pts = val::array();
    int i = 0;
    for (const auto& p : r.points) {
        val q = val::object();
        q.set("dt", p.dt);
        q.set("error", p.error);
        q.set("steps", static_cast<double>(p.steps));
        q.set("forceEvaluations", static_cast<double>(p.forceEvaluations));
        pts.set(i++, q);
    }
    o.set("ok", true);
    o.set("integrator", id);
    o.set("observedOrder", r.observedOrder);
    o.set("points", pts);
    return o;
}

val energyDrift(const std::string& id, double dt, double ecc, double orbits, int samples) {
    IntegratorKind k{};
    val o = val::object();
    if (!parseIntegrator(id, k) || !(dt > 0.0) || !(ecc >= 0.0 && ecc < 1.0) || !(orbits > 0.0) || samples < 2) {
        o.set("ok", false);
        return o;
    }
    const auto s = analysis::energyDriftStudy(k, dt, ecc, orbits, samples);
    val t = val::array(), e = val::array();
    for (std::size_t i = 0; i < s.size(); ++i) {
        t.set(i, s[i].t);
        e.set(i, s[i].relativeEnergyError);
    }
    o.set("ok", true);
    o.set("integrator", id);
    o.set("t", t);
    o.set("error", e);
    return o;
}

}  // namespace

EMSCRIPTEN_BINDINGS(rocksim) {
    emscripten::class_<WasmSession>("Session")
        .constructor<>()
        .function("load", &WasmSession::load)
        .function("play", &WasmSession::play)
        .function("pause", &WasmSession::pause)
        .function("running", &WasmSession::running)
        .function("setSpeed", &WasmSession::setSpeed)
        .function("tick", &WasmSession::tick)
        .function("step", &WasmSession::step)
        .function("seek", &WasmSession::seek)
        .function("seekTime", &WasmSession::seekTime)
        .function("stepBack", &WasmSession::stepBack)
        .function("grabStart", &WasmSession::grabStart)
        .function("grabMove", &WasmSession::grabMove)
        .function("grabEnd", &WasmSession::grabEnd)
        .function("setParam", &WasmSession::setParam)
        .function("setDt", &WasmSession::setDt)
        .function("setIntegrator", &WasmSession::setIntegrator)
        .function("setBody", &WasmSession::setBody)
        .function("pick", &WasmSession::pick)
        .function("selectBody", &WasmSession::selectBody)
        .function("runCount", &WasmSession::runCount)
        .function("count", &WasmSession::count)
        .function("x", &WasmSession::x)
        .function("y", &WasmSession::y)
        .function("vx", &WasmSession::vx)
        .function("vy", &WasmSession::vy)
        .function("radius", &WasmSession::radius)
        .function("mass", &WasmSession::mass)
        .function("fixed", &WasmSession::fixed)
        .function("springs", &WasmSession::springs)
        .function("springRest", &WasmSession::springRest)
        .function("trailPoints", &WasmSession::trailPoints)
        .function("trailCounts", &WasmSession::trailCounts)
        .function("traced", &WasmSession::traced)
        .function("events", &WasmSession::events)
        .function("series", &WasmSession::series)
        .function("diagnostics", &WasmSession::diagnostics)
        .function("liveParams", &WasmSession::liveParams)
        .function("timeline", &WasmSession::timeline)
        .function("hash", &WasmSession::hash)
        .function("meta", &WasmSession::meta);
    emscripten::function("catalog", &catalog);
    emscripten::function("integrators", &integrators);
    emscripten::function("convergence", &convergence);
    emscripten::function("energyDrift", &energyDrift);
}
