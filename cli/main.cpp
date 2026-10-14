// rocksim-cli: run scenarios headless and produce reproducible data.
//
//   rocksim-cli list
//   rocksim-cli run <scenario> [--integrator verlet] [--dt 0.001] [--time 10 | --steps N]
//                              [--every 10] [--set key=value ...] [--out file.csv]
//   rocksim-cli convergence [--integrators euler,symplectic-euler,verlet,rk4] [--out file.csv]
//   rocksim-cli drift [--dt 0.01] [--eccentricity 0.6] [--orbits 50]
//
// `run` writes a CSV time series of the diagnostics. `convergence` measures
// the global error of each integrator on the damped oscillator against its
// closed-form solution over a range of dt and fits the observed order.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rocksim/rocksim.hpp"

using namespace rocksim;

namespace {

int usage() {
    std::cerr << "usage:\n"
                 "  rocksim-cli list\n"
                 "  rocksim-cli run <scenario> [--integrator ID] [--dt S] [--time S | --steps N]\n"
                 "                 [--every N] [--set key=value]... [--out FILE.csv]\n"
                 "  rocksim-cli convergence [--integrators a,b,...] [--damping C] [--out FILE.csv]\n"
                 "  rocksim-cli drift [--dt S] [--eccentricity E] [--orbits N]\n"
                 "  rocksim-cli hash [--steps N]   (state hash of every scenario after N steps)\n"
                 "integrator IDs: euler, symplectic-euler, verlet, rk4\n";
    return 2;
}

bool parseDouble(const char* s, double& out) {
    char* end = nullptr;
    out = std::strtod(s, &end);
    return end && *end == '\0' && std::isfinite(out);
}

int listScenarios() {
    for (const auto& s : scenarioCatalog()) {
        std::printf("%s  —  %s\n  %s\n  units: %s\n  default: %s, dt = %g\n  validation: %s\n  parameters:\n",
                    s.id.c_str(), s.title.c_str(), s.summary.c_str(), s.units.c_str(),
                    std::string(integratorInfo(s.defaultIntegrator).id).c_str(), s.defaultDt, s.validation.c_str());
        for (const auto& p : s.params)
            std::printf("    %-16s %-10g [%g, %g] %s%s\n", p.key.c_str(), p.defaultValue, p.min, p.max, p.unit.c_str(),
                        p.live ? "  (live)" : "");
        std::printf("\n");
    }
    return 0;
}

int run(int argc, char** argv) {
    if (argc < 1) return usage();
    const std::string id = argv[0];
    const ScenarioSpec* spec = findScenario(id);
    if (!spec) {
        std::cerr << "unknown scenario '" << id << "' (try: rocksim-cli list)\n";
        return 2;
    }
    IntegratorKind kind = spec->defaultIntegrator;
    double dt = spec->defaultDt, time = 10.0;
    long long steps = -1, every = 10;
    ParamValues params;
    std::string outPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto need = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::cerr << a << " needs " << what << "\n"; std::exit(2); }
            return argv[++i];
        };
        double v = 0;
        if (a == "--integrator") {
            if (!parseIntegrator(need("an id"), kind)) { std::cerr << "unknown integrator\n"; return 2; }
        } else if (a == "--dt") {
            if (!parseDouble(need("seconds"), dt) || dt <= 0) { std::cerr << "--dt must be > 0\n"; return 2; }
        } else if (a == "--time") {
            if (!parseDouble(need("seconds"), time) || time <= 0) { std::cerr << "--time must be > 0\n"; return 2; }
        } else if (a == "--steps") {
            if (!parseDouble(need("a count"), v) || v < 1) { std::cerr << "--steps must be >= 1\n"; return 2; }
            steps = static_cast<long long>(v);
        } else if (a == "--every") {
            if (!parseDouble(need("a count"), v) || v < 1) { std::cerr << "--every must be >= 1\n"; return 2; }
            every = static_cast<long long>(v);
        } else if (a == "--set") {
            const std::string kv = need("key=value");
            const auto eq = kv.find('=');
            if (eq == std::string::npos || !parseDouble(kv.c_str() + eq + 1, v)) {
                std::cerr << "--set expects key=number\n";
                return 2;
            }
            params[kv.substr(0, eq)] = v;
        } else if (a == "--out") {
            outPath = need("a file");
        } else {
            std::cerr << "unknown option " << a << "\n";
            return usage();
        }
    }
    if (steps < 0) steps = static_cast<long long>(std::llround(time / dt));

    BuiltScenario built;
    std::vector<std::string> errs;
    if (!buildScenario(id, params, built, errs)) {
        for (const auto& e : errs) std::cerr << "error: " << e << "\n";
        return 1;
    }
    Simulation sim = Simulation::create(std::move(built.world), kind, dt, errs);
    if (!sim.valid()) {
        for (const auto& e : errs) std::cerr << "error: " << e << "\n";
        return 1;
    }

    std::ofstream file;
    std::ostream* out = &std::cout;
    if (!outPath.empty()) {
        file.open(outPath);
        if (!file) { std::cerr << "cannot write " << outPath << "\n"; return 1; }
        out = &file;
    }
    char line[512];
    *out << "# rocksim-cli run " << id << " integrator=" << integratorInfo(kind).id << " dt=" << dt << "\n";
    *out << "t,steps,kinetic,potential,total,rel_energy_error,px,py,angular_momentum,contacts\n";
    auto emit = [&] {
        const Diagnostics d = sim.diagnostics();
        std::snprintf(line, sizeof line, "%.10g,%llu,%.12g,%.12g,%.12g,%.6e,%.12g,%.12g,%.12g,%zu\n", d.time,
                      static_cast<unsigned long long>(d.steps), d.now.kinetic, d.now.potential, d.now.total(),
                      d.relativeEnergyError, d.now.momentum.x, d.now.momentum.y, d.now.angularMomentum,
                      d.lastContacts.bodyContacts);
        *out << line;
    };
    emit();
    for (long long s = 1; s <= steps; ++s) {
        sim.step();
        if (sim.status() != SimStatus::Ok) {
            emit();
            std::cerr << "stopped: " << sim.statusMessage() << "\n";
            return 3;
        }
        if (s % every == 0 || s == steps) emit();
    }
    if (!outPath.empty()) std::cerr << "wrote " << outPath << " (" << steps << " steps)\n";
    return 0;
}

int convergence(int argc, char** argv) {
    std::vector<IntegratorKind> kinds = {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler,
                                         IntegratorKind::VelocityVerlet, IntegratorKind::RK4};
    double damping = 0.0;
    std::string outPath;
    for (int i = 0; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--integrators" && i + 1 < argc) {
            kinds.clear();
            std::stringstream ss(argv[++i]);
            std::string tok;
            while (std::getline(ss, tok, ',')) {
                IntegratorKind k{};
                if (!parseIntegrator(tok, k)) { std::cerr << "unknown integrator " << tok << "\n"; return 2; }
                kinds.push_back(k);
            }
        } else if (a == "--damping" && i + 1 < argc) {
            if (!parseDouble(argv[++i], damping) || damping < 0) { std::cerr << "--damping must be >= 0\n"; return 2; }
        } else if (a == "--out" && i + 1 < argc) {
            outPath = argv[++i];
        } else {
            return usage();
        }
    }
    std::ofstream file;
    if (!outPath.empty()) {
        file.open(outPath);
        if (!file) { std::cerr << "cannot write " << outPath << "\n"; return 1; }
        file << "integrator,dt,steps,error,force_evaluations\n";
    }
    std::printf("Oscillator m = k = 1, x0 = 1, c = %g, t_end = 5.12; error vs closed form\n", damping);
    std::printf("%-18s %8s %14s %14s\n", "integrator", "order", "err@dt=0.005", "evals@0.005");
    for (IntegratorKind k : kinds) {
        const auto res = analysis::convergenceStudy(k, damping, 7);
        double e5 = 0;
        long long f5 = 0;
        for (const auto& p : res.points) {
            if (std::abs(p.dt - 0.005) < 1e-12) { e5 = p.error; f5 = p.forceEvaluations; }
            if (file) {
                char line[160];
                std::snprintf(line, sizeof line, "%s,%.6g,%lld,%.6e,%lld\n", std::string(integratorInfo(k).id).c_str(),
                              p.dt, p.steps, p.error, p.forceEvaluations);
                file << line;
            }
        }
        std::printf("%-18s %8.3f %14.3e %14lld\n", std::string(integratorInfo(k).name).c_str(), res.observedOrder, e5, f5);
    }
    return 0;
}

int drift(int argc, char** argv) {
    double dt = 0.01, ecc = 0.6, orbits = 50;
    for (int i = 0; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        double v = 0;
        if (!parseDouble(argv[i + 1], v)) return usage();
        if (a == "--dt" && v > 0) dt = v;
        else if (a == "--eccentricity" && v >= 0 && v < 1) ecc = v;
        else if (a == "--orbits" && v > 0) orbits = v;
        else return usage();
    }
    std::printf("Kepler orbit e = %g, dt = %g, %g orbits: relative energy error |dE/E0|\n", ecc, dt, orbits);
    std::printf("%-18s %12s %12s\n", "integrator", "max", "final");
    for (auto k : {IntegratorKind::ExplicitEuler, IntegratorKind::SymplecticEuler, IntegratorKind::VelocityVerlet,
                   IntegratorKind::RK4}) {
        const auto s = analysis::energyDriftStudy(k, dt, ecc, orbits, 400);
        double mx = 0;
        for (const auto& p : s) mx = std::max(mx, std::abs(p.relativeEnergyError));
        std::printf("%-18s %12.3e %12.3e\n", std::string(integratorInfo(k).name).c_str(), mx,
                    s.empty() ? 0.0 : std::abs(s.back().relativeEnergyError));
    }
    return 0;
}

int hashAll(int argc, char** argv) {
    long long steps = 3000;
    if (argc == 2 && std::string(argv[0]) == "--steps") {
        double v = 0;
        if (!parseDouble(argv[1], v) || v < 0) return usage();
        steps = static_cast<long long>(v);
    } else if (argc != 0) {
        return usage();
    }
    for (const auto& sc : scenarioCatalog()) {
        BuiltScenario s;
        std::vector<std::string> errs;
        if (!buildScenario(sc.id, {}, s, errs)) return 1;
        Simulation sim = Simulation::create(std::move(s.world), sc.defaultIntegrator, sc.defaultDt, errs);
        for (long long i = 0; i < steps; ++i) sim.step();
        std::printf("%-18s %016llx\n", sc.id.c_str(), static_cast<unsigned long long>(stateHash(sim.world().bodies)));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    if (cmd == "list") return listScenarios();
    if (cmd == "run") return run(argc - 2, argv + 2);
    if (cmd == "convergence") return convergence(argc - 2, argv + 2);
    if (cmd == "drift") return drift(argc - 2, argv + 2);
    if (cmd == "hash") return hashAll(argc - 2, argv + 2);
    return usage();
}
