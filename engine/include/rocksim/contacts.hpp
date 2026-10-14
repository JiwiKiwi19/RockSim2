#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "rocksim/world.hpp"

namespace rocksim {

using IndexPair = std::pair<std::uint32_t, std::uint32_t>;

/// Uniform-grid broad phase for disc overlap queries.
///
/// Correctness argument: cells are at least 2 * r_max wide on each axis. Two
/// discs overlap only if their centres are closer than r_i + r_j <= 2 r_max,
/// so overlapping centres are never more than one cell apart on either axis
/// and a 3x3 neighbourhood search is exhaustive. Every body (not every
/// occupied cell) drives the search, and each unordered pair is reported once
/// from its lower index. Together these fix the two broad-phase defects found
/// in the original app (audit findings E2 and E7).
///
/// Cost: O(N + cells + candidate pairs). Cells are bucketed with a counting
/// sort into flat arrays, so no per-cell heap allocation happens per step.
class UniformGrid {
public:
    /// Writes every pair (i < j) of non-fixed-or-fixed bodies with r > 0 whose
    /// discs overlap (distance < r_i + r_j), sorted lexicographically.
    void findOverlaps(const Bodies& bodies, std::vector<IndexPair>& out);

    /// Narrow-phase distance tests performed by the last query (for benchmarks).
    [[nodiscard]] std::size_t lastCandidateTests() const { return candidateTests_; }
    [[nodiscard]] int lastGridWidth() const { return nx_; }
    [[nodiscard]] int lastGridHeight() const { return ny_; }

private:
    std::vector<std::uint32_t> cellOf_;
    std::vector<std::uint32_t> cellStart_;
    std::vector<std::uint32_t> sorted_;
    std::vector<std::uint32_t> cursor_;
    std::size_t candidateTests_ = 0;
    int nx_ = 0, ny_ = 0;
};

/// O(N^2) reference used by tests and benchmarks.
void bruteForceOverlaps(const Bodies& bodies, std::vector<IndexPair>& out);

struct ContactStats {
    std::size_t bodyContacts = 0;  ///< overlapping pairs found this step
    std::size_t impulses = 0;      ///< pairs that were approaching and received an impulse
    std::size_t wallHits = 0;
};

/// Resolves body-body and body-wall contacts after the integrator step
/// (operator splitting: smooth forces are integrated, then contacts are
/// projected). Each overlapping pair receives
///   * a positional correction that removes the overlap, split by inverse mass
///     (moves the centre of mass not at all, so momentum is unaffected), and
///   * if approaching (v_n < 0), the normal impulse
///         J = -(1 + e) v_n / (1/m_a + 1/m_b),
///     applied as -J/m_a to a and +J/m_b to b. This conserves linear momentum
///     exactly and sets the post-collision normal velocity to -e v_n.
/// Pairs are processed once, in sorted order (deterministic sequential
/// impulses); simultaneous multi-body contacts are therefore approximate.
ContactStats resolveContacts(World& world, UniformGrid& grid, std::vector<IndexPair>& scratch);

}  // namespace rocksim
