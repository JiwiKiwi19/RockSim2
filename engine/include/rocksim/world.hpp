#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "rocksim/vec2.hpp"

namespace rocksim {

/// Point bodies stored as a structure of arrays (SoA). SoA keeps each hot loop
/// streaming through contiguous doubles and maps directly onto the typed-array
/// views handed to JavaScript (no per-frame repacking).
///
/// Units are SI (m, kg, s) unless a scenario states it is non-dimensional
/// (the gravitational N-body scenarios use G = 1).
struct Bodies {
    std::vector<double> x, y;    ///< position [m]
    std::vector<double> vx, vy;  ///< velocity [m/s]
    std::vector<double> mass;    ///< [kg], > 0
    std::vector<double> radius;  ///< contact radius [m], >= 0 (0 = point mass, never collides)
    /// 1 = kinematically fixed (infinite inertia: no force moves it, impulses
    /// treat it as an immovable anchor). Used for spring anchors.
    std::vector<std::uint8_t> fixed;

    [[nodiscard]] std::size_t size() const { return x.size(); }
    void clear();
    void reserve(std::size_t n);
    /// Appends a body and returns its index.
    std::uint32_t add(Vec2 position, Vec2 velocity, double m, double r, bool isFixed = false);
    [[nodiscard]] double invMass(std::size_t i) const { return fixed[i] ? 0.0 : 1.0 / mass[i]; }
};

/// Damped linear (Hookean) spring between bodies i and j.
///   F_j = -[k (|d| - L) + c ((v_j - v_i) . d_hat)] d_hat,  F_i = -F_j,  d = x_j - x_i
struct Spring {
    std::uint32_t i = 0;
    std::uint32_t j = 0;
    double stiffness = 0.0;   ///< k [N/m]
    double restLength = 0.0;  ///< L [m]
    double damping = 0.0;     ///< c [N s/m], dashpot along the spring axis
};

/// Axis-aligned container used for wall contacts.
struct Box {
    double xmin = -1.0, xmax = 1.0, ymin = -1.0, ymax = 1.0;
};

/// Global physical parameters. Everything here may be changed between steps.
struct WorldParams {
    Vec2 gravity{0.0, 0.0};  ///< uniform field g [m/s^2]; acceleration is independent of mass

    bool nbody = false;      ///< pairwise Newtonian gravity between all non-fixed bodies
    double G = 1.0;          ///< gravitational constant (scenario units)
    double softening = 0.0;  ///< Plummer softening length eps: r^2 -> r^2 + eps^2

    bool walls = false;
    Box box{};
    double wallRestitution = 1.0;  ///< coefficient of restitution e_w in [0, 1]

    bool collisions = false;
    double restitution = 1.0;  ///< body-body coefficient of restitution e in [0, 1]
};

struct World {
    Bodies bodies;
    std::vector<Spring> springs;
    WorldParams params;
};

/// Conserved / monitored quantities. Fixed bodies are excluded from kinetic
/// energy and momenta (they are external constraints, so momentum is only
/// conserved in scenarios without anchors or walls).
struct Invariants {
    double kinetic = 0.0;
    double potential = 0.0;
    [[nodiscard]] double total() const { return kinetic + potential; }
    Vec2 momentum{};
    double angularMomentum = 0.0;  ///< about the origin, z-component
    Vec2 centerOfMass{};
};

/// Accelerations from all smooth (non-contact) forces, evaluated at an
/// arbitrary phase-space point (x, y, vx, vy) so that multi-stage integrators
/// such as RK4 can call it at trial states. Masses, radii and topology come
/// from `world`. Fixed bodies always get zero acceleration.
void computeAccelerations(const World& world,
                          std::span<const double> x, std::span<const double> y,
                          std::span<const double> vx, std::span<const double> vy,
                          std::span<double> ax, std::span<double> ay);

[[nodiscard]] Invariants computeInvariants(const World& world);

/// Returns human-readable problems with the world (empty = valid). Never throws.
[[nodiscard]] std::vector<std::string> validateWorld(const World& world);

}  // namespace rocksim
