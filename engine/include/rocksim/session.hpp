#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rocksim/scenarios.hpp"
#include "rocksim/simulation.hpp"

namespace rocksim {

/// A user action that changes the state of a run. Stamped with the step at
/// which it was applied; replaying the same events at the same steps from the
/// same start gives the same states.
enum class EventKind : std::uint8_t { GrabStart, GrabMove, GrabEnd, SetParam, SetDt, SetIntegrator, SetBody };

struct Event {
    std::uint64_t step = 0;
    double time = 0.0;
    EventKind kind = EventKind::GrabStart;
    std::uint32_t body = 0;
    int key = 0;  ///< SetParam: index into the scenario's params; SetIntegrator: IntegratorKind
    double a = 0.0, b = 0.0, c = 0.0, d = 0.0;
};

struct SessionConfig {
    int keyframeInterval = 250;                      ///< steps between keyframes
    std::size_t keyframeBudgetBytes = 64ull << 20;   ///< shared by all runs
    std::size_t maxSamples = 4000;                   ///< per run; halves its resolution when full
    std::size_t trailCapacity = 900;                 ///< points per traced body
};

struct CommandResult {
    bool ok = false;
    bool branched = false;  ///< the command was made in the past and started (or replaced) run B
    std::vector<std::string> errors;
};

/// Chart data for one run, one row per sample. All columns have equal length.
struct Series {
    std::vector<double> step, t, K, U, E, rel, div, numX, refX;
    [[nodiscard]] std::size_t size() const { return t.size(); }
};

/// Owns what a run is and what happens to it over time: the time loop, the
/// event log, keyframes, seeking, the A/B branch, chart samples, trails and
/// picking. Free of Emscripten; the bindings and the worker only marshal.
///
/// A run is its Simulation plus an append-only event log. The cursor is the
/// run's current step; the head is the furthest step ever computed. Every
/// state-changing command is applied between steps and logged with its step.
/// A command made while the cursor is behind the head does not erase the
/// future: it copies the run into B and applies the command there.
/// With two runs, both always sit at the same step and advance in lockstep.
class Session {
public:
    explicit Session(SessionConfig cfg = {});

    /// Builds run A from a scenario and drops run B. On failure `errors` is
    /// filled and the previous session is left untouched.
    bool load(std::string_view scenario, const ParamValues& values, IntegratorKind kind, double dt,
              std::vector<std::string>& errors);
    [[nodiscard]] bool loaded() const { return !runs_.empty(); }

    // --- time loop ---
    void play() { running_ = loaded(); }
    void pause() { running_ = false; }
    [[nodiscard]] bool running() const { return running_; }
    void setSpeed(double s);
    [[nodiscard]] double speed() const { return speed_; }
    /// Called every ~16 ms with the wall time since the last call and the wall
    /// time the previous call's steps took. Advances by wall x speed simulated
    /// seconds, within a step budget adapted to the measured step cost. Stops
    /// running when a run becomes unstable. Returns steps taken.
    int tick(double wallSeconds, double lastComputeMs);
    /// Pauses, then advances up to `n` steps (replaying the log, or new ones at the head).
    int step(int n);
    [[nodiscard]] double stepsPerSecond() const { return stepsPerSecond_; }
    [[nodiscard]] double droppedTime() const { return droppedTime_; }

    // --- time travel (all runs together; the target is clamped to what is reachable) ---
    std::uint64_t seek(std::uint64_t step);
    std::uint64_t seekTime(double t);
    std::uint64_t stepBack(std::uint64_t n);
    [[nodiscard]] std::uint64_t cursorStep() const;
    [[nodiscard]] double cursorTime() const;
    /// Furthest step computed by a run.
    [[nodiscard]] std::uint64_t headStep(int run) const { return runs_[static_cast<std::size_t>(run)].head; }
    [[nodiscard]] double headTime(int run) const { return runs_[static_cast<std::size_t>(run)].headTime; }
    /// The reachable range is [oldestStep(), seekLimit()]: back to the oldest
    /// surviving keyframe of every run, forward to the shortest head.
    [[nodiscard]] std::uint64_t oldestStep() const;
    [[nodiscard]] double oldestTime() const;
    [[nodiscard]] std::uint64_t seekLimit() const;

    // --- commands, addressed to run 0 (A) or 1 (B) ---
    CommandResult grabStart(int run, std::uint32_t body, Vec2 target);
    CommandResult grabMove(int run, Vec2 target);
    CommandResult grabEnd(int run);
    CommandResult setParam(int run, std::string_view key, double value);
    CommandResult setDt(int run, double dt);
    CommandResult setIntegrator(int run, IntegratorKind kind);
    CommandResult setBody(int run, std::uint32_t body, Vec2 position, Vec2 velocity);

    /// Index of the body nearest (x, y) within `toleranceWorld` of its edge, or -1.
    [[nodiscard]] int pick(int run, double x, double y, double toleranceWorld) const;
    /// -1 clears. Kepler and figure-eight trace every body; other scenarios trace the selection.
    void selectBody(int body);
    [[nodiscard]] int selected() const { return selected_; }

    // --- queries ---
    [[nodiscard]] int runCount() const { return static_cast<int>(runs_.size()); }
    [[nodiscard]] const Simulation& sim(int run) const { return runs_[static_cast<std::size_t>(run)].sim; }
    [[nodiscard]] const Series& series(int run) const { return runs_[static_cast<std::size_t>(run)].series; }
    [[nodiscard]] const std::vector<Event>& events(int run) const { return runs_[static_cast<std::size_t>(run)].events; }
    /// Where run B split off from A (valid when runCount() == 2).
    [[nodiscard]] std::uint64_t branchStep() const { return branchStep_; }
    [[nodiscard]] double branchTime() const { return branchTime_; }
    [[nodiscard]] const std::vector<int>& traced() const { return traced_; }
    /// Flattened (x, y) points of every traced body, oldest first; counts[k] points belong to traced()[k].
    [[nodiscard]] std::vector<double> trailPoints(int run, std::vector<double>& counts) const;
    [[nodiscard]] double divergence() const;  ///< RMS position difference A vs B (0 without B)
    [[nodiscard]] bool hasReference() const { return oscillator_; }
    [[nodiscard]] double referenceX(int run) const;  ///< exact oscillator position at the run's time
    [[nodiscard]] std::size_t keyframeCount(int run) const { return runs_[static_cast<std::size_t>(run)].keyframes.size(); }
    [[nodiscard]] std::size_t keyframeBytes() const;
    [[nodiscard]] const std::string& scenarioId() const { return scenario_; }
    [[nodiscard]] const Box& view() const { return view_; }
    [[nodiscard]] double referencePeriod() const { return referencePeriod_; }

private:
    struct Keyframe {
        std::uint64_t step = 0;
        double time = 0.0;
        SimSnapshot snap;
        std::size_t bytes = 0;
    };
    struct Trail {
        std::vector<double> pts;
        std::size_t start = 0, len = 0;  // ring buffer of (x, y) pairs
    };
    struct Run {
        Simulation sim;
        std::vector<Event> events;
        std::size_t nextEvent = 0;  // events before this index are applied
        std::vector<Keyframe> keyframes;
        Series series;
        std::size_t stride = 1;  // sample stride in steps
        std::uint64_t head = 0;
        double headTime = 0.0;
        std::vector<Trail> trails;
    };

    CommandResult command(int run, Event ev);
    bool applyEvent(Run& r, const Event& ev, std::vector<std::string>* errors);
    void applyDue(Run& r);
    bool advanceOne();
    void takeKeyframe(Run& r);
    void enforceBudget();
    void record(std::size_t index, Run& r);
    void truncateAtCursor(Run& r);
    void seekRun(Run& r, std::uint64_t target);
    void resetTrails();
    void pushTrailPoints(Run& r);

    SessionConfig cfg_;
    std::vector<Run> runs_;
    std::string scenario_;
    const ScenarioSpec* spec_ = nullptr;
    Box view_{};
    double referencePeriod_ = 0.0;
    bool oscillator_ = false;
    double oscX0_ = 0.0;
    std::vector<int> traced_;
    std::size_t trailStride_ = 1;
    int selected_ = -1;
    std::uint64_t branchStep_ = 0;
    double branchTime_ = 0.0;

    bool running_ = false;
    double speed_ = 1.0;
    double accumulator_ = 0.0;
    double droppedTime_ = 0.0;
    double msPerStep_ = 0.05;
    int lastSteps_ = 0;
    double secondWall_ = 0.0;
    long long secondSteps_ = 0;
    double stepsPerSecond_ = 0.0;
};

}  // namespace rocksim
