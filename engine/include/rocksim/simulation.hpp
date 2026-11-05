#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "rocksim/contacts.hpp"
#include "rocksim/integrators.hpp"
#include "rocksim/world.hpp"

namespace rocksim {

enum class SimStatus { Ok = 0, Unstable = 1 };

struct Diagnostics {
    double time = 0.0;          ///< simulated time [s]
    std::uint64_t steps = 0;
    Invariants now;
    Invariants initial;
    /// (E - E0) / max(|E0|, |K0|, |U0|): a scale-aware energy error that stays
    /// meaningful when E0 is (close to) zero.
    double relativeEnergyError = 0.0;
    ContactStats lastContacts;
};

/// Everything about a run that can change after load: restoring it and
/// stepping again reproduces the original continuation bit for bit. Masses,
/// radii and topology never change within a loaded run, so they are not stored.
struct SimSnapshot {
    std::vector<double> x, y, vx, vy;
    WorldParams params;
    std::optional<Grab> grab;
    IntegratorKind kind = IntegratorKind::VelocityVerlet;
    Integrator::Cache cache;
    double dt = 0.0, time = 0.0, accumulator = 0.0, droppedTime = 0.0;
    std::uint64_t steps = 0;
    ContactStats lastContacts;
    Invariants initial;
    SimStatus status = SimStatus::Ok;
    std::string statusMessage;

    [[nodiscard]] std::size_t bytes() const;
};

/// Owns a World and advances it with a fixed time step.
///
/// Simulated time is decoupled from wall-clock time: `advance()` adds
/// requested simulated seconds to an accumulator and takes as many whole
/// steps of size dt as fit (Fiedler, "Fix Your Timestep!"). Rendering rate,
/// timer jitter and speed controls therefore change *how much* simulated time
/// passes per frame, never the step size, so a run is reproducible: the same
/// world, integrator, dt and step count give bit-identical states.
class Simulation {
public:
    /// Validates the inputs; on failure `errors` is filled and the returned
    /// simulation is empty (`valid() == false`). Never throws on bad input.
    static Simulation create(World world, IntegratorKind integrator, double dt,
                             std::vector<std::string>& errors);

    Simulation() = default;

    [[nodiscard]] bool valid() const { return valid_; }

    /// One step: integrate smooth forces, resolve contacts, check health.
    /// Does nothing while the status is Unstable.
    void step();
    /// Takes `n` steps (stops early if the run becomes unstable). Returns steps taken.
    int stepN(int n);
    /// Accumulator advance. Returns the number of steps taken. If more than
    /// `maxSteps` would be needed, the backlog is dropped (prevents the
    /// "spiral of death" when the machine cannot keep up) and `droppedTime`
    /// accumulates the simulated seconds that were skipped.
    int advance(double simSeconds, int maxSteps);

    [[nodiscard]] const World& world() const { return world_; }
    /// Mutable access for live parameter edits. Call `paramsChanged()` after.
    World& mutableWorld() { return world_; }
    /// Re-validates and invalidates integrator caches. Returns problems (empty = ok);
    /// on problems the previous parameters are restored.
    std::vector<std::string> paramsChanged();

    [[nodiscard]] Diagnostics diagnostics() const;
    [[nodiscard]] SimStatus status() const { return status_; }
    [[nodiscard]] const std::string& statusMessage() const { return statusMessage_; }

    [[nodiscard]] double dt() const { return dt_; }
    /// Rejects dt that is not finite and positive (returns false).
    bool setDt(double dt);
    [[nodiscard]] IntegratorKind integratorKind() const { return integrator_.kind(); }
    void setIntegrator(IntegratorKind k) { integrator_.setKind(k); }

    /// Resets the energy baseline to the current state (see paramsChanged()).
    void rebaseline() { initial_ = computeInvariants(world_); }

    /// Moves a free body and sets its velocity. Rejects (returns false) a bad
    /// index, a fixed body, non-finite values or an unstable run. With walls
    /// on, the position is clamped inside the box. Rebaselines the energy.
    bool setBodyState(std::uint32_t body, Vec2 position, Vec2 velocity);

    /// Hold a free body with the grab force (see Grab). The target is clamped
    /// inside the box when walls are on. The grab adds energy, so the
    /// energy-growth stability check is suspended while it is held and the
    /// baseline is reset on release.
    bool setGrab(std::uint32_t body, Vec2 target);
    bool moveGrab(Vec2 target);
    void releaseGrab();
    [[nodiscard]] bool grabbed() const { return world_.grab.has_value(); }

    [[nodiscard]] SimSnapshot capture() const;
    void restore(const SimSnapshot& snap);

    [[nodiscard]] double time() const { return time_; }
    [[nodiscard]] std::uint64_t steps() const { return steps_; }
    [[nodiscard]] double droppedTime() const { return droppedTime_; }
    [[nodiscard]] long long forceEvaluations() const { return integrator_.forceEvaluations(); }

private:
    void checkHealth();
    [[nodiscard]] bool inputsOk(std::uint32_t body, Vec2 p) const;
    [[nodiscard]] Vec2 clampInside(std::uint32_t body, Vec2 p) const;

    World world_;
    WorldParams lastGoodParams_;
    Integrator integrator_;
    UniformGrid grid_;
    std::vector<IndexPair> pairs_;
    ContactStats lastContacts_;
    Invariants initial_;
    double dt_ = 1e-3;
    double time_ = 0.0;
    double accumulator_ = 0.0;
    double droppedTime_ = 0.0;
    std::uint64_t steps_ = 0;
    SimStatus status_ = SimStatus::Ok;
    std::string statusMessage_;
    bool valid_ = false;
};

/// FNV-1a hash over the raw bits of every position and velocity. Two runs
/// with equal hashes have bit-identical states; used to check reproducibility
/// across compilers, platforms and WebAssembly.
[[nodiscard]] std::uint64_t stateHash(const Bodies& bodies);

}  // namespace rocksim
