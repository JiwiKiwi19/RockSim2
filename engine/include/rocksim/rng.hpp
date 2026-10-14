#pragma once

#include <cmath>
#include <cstdint>

#include "rocksim/vec2.hpp"

namespace rocksim {

/// PCG32 (O'Neill, "PCG: A Family of Simple Fast Space-Efficient Statistically
/// Good Algorithms for Random Number Generation", 2014), XSH-RR variant.
///
/// Why not std::mt19937 + std::uniform_real_distribution? The *engine* is
/// portable, but the standard distributions are implementation-defined, so
/// libstdc++ (native Linux), libc++ (macOS, Emscripten) and MSVC can produce
/// different initial conditions from the same seed. Every value drawn here is
/// defined bit-for-bit by this header, so a scenario seed reproduces the same
/// world natively and in WebAssembly.
class Pcg32 {
public:
    explicit constexpr Pcg32(std::uint64_t seed, std::uint64_t stream = 0xda3e39cb94b95bdbULL)
        : inc_((stream << 1u) | 1u) {
        next_u32();
        state_ += seed;
        next_u32();
    }

    constexpr std::uint32_t next_u32() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((32u - rot) & 31u));
    }

    /// Uniform double in [0, 1) with 53 random bits (two draws, exact in binary64).
    constexpr double uniform01() {
        const std::uint64_t hi = next_u32() >> 5u;  // 27 bits
        const std::uint64_t lo = next_u32() >> 6u;  // 26 bits
        return static_cast<double>((hi << 26u) | lo) * (1.0 / 9007199254740992.0);  // 2^-53
    }

    /// Uniform double in [lo, hi).
    constexpr double uniform(double lo, double hi) { return lo + (hi - lo) * uniform01(); }

    /// Uniformly distributed direction, by rejection sampling in the unit disc.
    /// Uses only +, *, / and sqrt (all correctly rounded in IEEE 754), unlike
    /// cos/sin whose last-bit results differ between libm implementations.
    Vec2 unitVector() {
        for (;;) {
            const double x = uniform(-1.0, 1.0), y = uniform(-1.0, 1.0);
            const double r2 = x * x + y * y;
            if (r2 > 1e-6 && r2 <= 1.0) {
                const double r = std::sqrt(r2);
                return {x / r, y / r};
            }
        }
    }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_;
};

/// Cube root of x > 0 using only correctly rounded IEEE operations (fixed
/// Newton iteration), so it gives the same bits on every platform. std::cbrt
/// is not required to be correctly rounded and differs between glibc and musl.
inline double portableCbrt(double x) {
    if (!(x > 0.0)) return 0.0;
    double y = x > 1.0 ? 1.0 + (x - 1.0) / 3.0 : 1.0;  // above the root for x > 1, converges from above
    for (int i = 0; i < 200; ++i) y = y - (y * y * y - x) / (3.0 * y * y);
    return y;
}

}  // namespace rocksim
