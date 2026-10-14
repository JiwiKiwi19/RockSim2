#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "rocksim/integrators.hpp"
#include "rocksim/world.hpp"

namespace rocksim {

/// One user-tunable scenario parameter, with the metadata a UI or CLI needs
/// to render and validate it.
struct ParamSpec {
    std::string key;
    std::string label;
    std::string unit;  ///< "" for dimensionless
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.0;
    double step = 0.0;     ///< UI granularity
    bool integer = false;
    /// true: can be applied to a running simulation via applyLiveParam()
    /// without rebuilding the world.
    bool live = false;
    std::string help;
};

struct ScenarioSpec {
    std::string id;
    std::string title;
    std::string summary;
    std::string units;  ///< e.g. "SI (m, kg, s)" or "non-dimensional, G = 1"
    std::vector<ParamSpec> params;
    IntegratorKind defaultIntegrator = IntegratorKind::VelocityVerlet;
    double defaultDt = 1e-3;
    std::string validation;  ///< what this scenario can be checked against
};

using ParamValues = std::map<std::string, double, std::less<>>;

struct BuiltScenario {
    World world;
    Box view;                    ///< suggested viewport in world coordinates
    double referencePeriod = 0;  ///< analytic period if one exists, else 0
};

[[nodiscard]] const std::vector<ScenarioSpec>& scenarioCatalog();
[[nodiscard]] const ScenarioSpec* findScenario(std::string_view id);

/// Builds a scenario. Missing parameters take their defaults; out-of-range or
/// non-integer values are reported in `errors` (and nothing is built).
bool buildScenario(std::string_view id, const ParamValues& values, BuiltScenario& out,
                   std::vector<std::string>& errors);

/// Applies a parameter marked `live` to an existing world. Returns false if the
/// key is not live for this scenario (the caller should rebuild instead).
bool applyLiveParam(std::string_view scenarioId, std::string_view key, double value, World& world);

}  // namespace rocksim
