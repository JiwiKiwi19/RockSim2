// Embind interface between the C++ engine and the browser.
//
// Design: the engine stays free of any Emscripten dependency; this file is the
// only place that knows about JavaScript. Bulk data (positions, velocities,
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

class WasmSimulation {
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

        BuiltScenario built;
        if (!buildScenario(scenario, values, built, errors)) return result(false, errors);
        Simulation sim = Simulation::create(built.world, kind, dt, errors);
        if (!sim.valid()) return result(false, errors);

        sim_ = std::move(sim);
        scenario_ = scenario;
        view_ = built.view;
        referencePeriod_ = built.referencePeriod;
        springPairs_.clear();
        springRest_.clear();
        for (const Spring& s : sim_.world().springs) {
            springPairs_.push_back(s.i);
            springPairs_.push_back(s.j);
            springRest_.push_back(s.restLength);
        }
        return result(true, {});
    }

    int advance(double simSeconds, int maxSteps) { return sim_.advance(simSeconds, maxSteps); }
    int stepN(int n) { return sim_.stepN(n); }

    val setLiveParam(const std::string& key, double value) {
        if (!applyLiveParam(scenario_, key, value, sim_.mutableWorld()))
            return result(false, {"'" + key + "' cannot be changed while running; reset to apply it"});
        const auto errs = sim_.paramsChanged();
        return result(errs.empty(), errs);
    }

    bool setIntegrator(const std::string& id) {
        IntegratorKind k{};
        if (!parseIntegrator(id, k)) return false;
        sim_.setIntegrator(k);
        return true;
    }
    bool setDt(double dt) { return sim_.setDt(dt); }

    int count() const { return static_cast<int>(sim_.world().bodies.size()); }
    val x() const { return view(sim_.world().bodies.x); }
    val y() const { return view(sim_.world().bodies.y); }
    val vx() const { return view(sim_.world().bodies.vx); }
    val vy() const { return view(sim_.world().bodies.vy); }
    val radius() const { return view(sim_.world().bodies.radius); }
    val mass() const { return view(sim_.world().bodies.mass); }
    val fixed() const {
        const auto& f = sim_.world().bodies.fixed;
        return val(emscripten::typed_memory_view(f.size(), f.data()));
    }
    val springs() const { return val(emscripten::typed_memory_view(springPairs_.size(), springPairs_.data())); }
    val springRest() const { return view(springRest_); }

    val diagnostics() const {
        const Diagnostics d = sim_.diagnostics();
        val o = val::object();
        o.set("time", d.time);
        o.set("steps", static_cast<double>(d.steps));
        o.set("now", invariantsToVal(d.now));
        o.set("initial", invariantsToVal(d.initial));
        o.set("relativeEnergyError", d.relativeEnergyError);
        o.set("contacts", static_cast<double>(d.lastContacts.bodyContacts));
        o.set("impulses", static_cast<double>(d.lastContacts.impulses));
        o.set("wallHits", static_cast<double>(d.lastContacts.wallHits));
        o.set("droppedTime", sim_.droppedTime());
        o.set("forceEvaluations", static_cast<double>(sim_.forceEvaluations()));
        o.set("status", sim_.status() == SimStatus::Ok ? std::string("ok") : std::string("unstable"));
        o.set("statusMessage", sim_.statusMessage());
        return o;
    }

    /// Hex FNV-1a hash of the state bits (see rocksim::stateHash).
    std::string hash() const {
        char buf[17];
        std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(stateHash(sim_.world().bodies)));
        return buf;
    }

    val meta() const {
        val o = val::object();
        o.set("scenario", scenario_);
        o.set("xmin", view_.xmin); o.set("xmax", view_.xmax);
        o.set("ymin", view_.ymin); o.set("ymax", view_.ymax);
        o.set("referencePeriod", referencePeriod_);
        const WorldParams& p = sim_.world().params;
        o.set("walls", p.walls);
        o.set("boxXmin", p.box.xmin); o.set("boxXmax", p.box.xmax);
        o.set("boxYmin", p.box.ymin); o.set("boxYmax", p.box.ymax);
        o.set("dt", sim_.dt());
        o.set("integrator", std::string(integratorInfo(sim_.integratorKind()).id));
        return o;
    }

private:
    static val view(const std::vector<double>& v) { return val(emscripten::typed_memory_view(v.size(), v.data())); }

    Simulation sim_;
    std::string scenario_;
    Box view_{};
    double referencePeriod_ = 0.0;
    std::vector<std::uint32_t> springPairs_;
    std::vector<double> springRest_;
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

val oscillatorExact(double m, double k, double c, double x0, double v0, double t) {
    const auto s = analytic::dampedOscillator(m, k, c, x0, v0, t);
    val o = val::object();
    o.set("x", s.x);
    o.set("v", s.v);
    return o;
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
    emscripten::class_<WasmSimulation>("Simulation")
        .constructor<>()
        .function("load", &WasmSimulation::load)
        .function("advance", &WasmSimulation::advance)
        .function("stepN", &WasmSimulation::stepN)
        .function("setLiveParam", &WasmSimulation::setLiveParam)
        .function("setIntegrator", &WasmSimulation::setIntegrator)
        .function("setDt", &WasmSimulation::setDt)
        .function("count", &WasmSimulation::count)
        .function("x", &WasmSimulation::x)
        .function("y", &WasmSimulation::y)
        .function("vx", &WasmSimulation::vx)
        .function("vy", &WasmSimulation::vy)
        .function("radius", &WasmSimulation::radius)
        .function("mass", &WasmSimulation::mass)
        .function("fixed", &WasmSimulation::fixed)
        .function("springs", &WasmSimulation::springs)
        .function("springRest", &WasmSimulation::springRest)
        .function("diagnostics", &WasmSimulation::diagnostics)
        .function("meta", &WasmSimulation::meta)
        .function("hash", &WasmSimulation::hash);
    emscripten::function("catalog", &catalog);
    emscripten::function("integrators", &integrators);
    emscripten::function("oscillatorExact", &oscillatorExact);
    emscripten::function("convergence", &convergence);
    emscripten::function("energyDrift", &energyDrift);
}
