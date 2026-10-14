#pragma once

#include <cmath>

namespace rocksim {

/// Minimal 2-D vector used at API boundaries. Hot loops work on the
/// structure-of-arrays storage in `Bodies` directly.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(double s) const { return {x * s, y * s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }

    [[nodiscard]] constexpr double dot(Vec2 o) const { return x * o.x + y * o.y; }
    /// z-component of the 3-D cross product (this x o).
    [[nodiscard]] constexpr double cross(Vec2 o) const { return x * o.y - y * o.x; }
    [[nodiscard]] double norm() const { return std::sqrt(x * x + y * y); }
};

constexpr Vec2 operator*(double s, Vec2 v) { return v * s; }

}  // namespace rocksim
