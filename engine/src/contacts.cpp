#include "rocksim/contacts.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace rocksim {

namespace {
bool overlaps(const Bodies& b, std::size_t i, std::size_t j) {
    const double dx = b.x[j] - b.x[i];
    const double dy = b.y[j] - b.y[i];
    const double rs = b.radius[i] + b.radius[j];
    return dx * dx + dy * dy < rs * rs;
}
}  // namespace

void bruteForceOverlaps(const Bodies& b, std::vector<IndexPair>& out) {
    out.clear();
    const std::size_t n = b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (b.radius[i] <= 0.0) continue;
        for (std::size_t j = i + 1; j < n; ++j) {
            if (b.radius[j] <= 0.0) continue;
            if (overlaps(b, i, j)) out.emplace_back(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j));
        }
    }
}

void UniformGrid::findOverlaps(const Bodies& b, std::vector<IndexPair>& out) {
    out.clear();
    candidateTests_ = 0;
    const std::size_t n = b.size();
    nx_ = ny_ = 0;
    if (n < 2) return;

    double rmax = 0.0;
    double xmin = std::numeric_limits<double>::infinity(), ymin = xmin;
    double xmax = -xmin, ymax = -xmin;
    for (std::size_t i = 0; i < n; ++i) {
        if (b.radius[i] <= 0.0 || !std::isfinite(b.x[i]) || !std::isfinite(b.y[i])) continue;
        rmax = std::max(rmax, b.radius[i]);
        xmin = std::min(xmin, b.x[i]); xmax = std::max(xmax, b.x[i]);
        ymin = std::min(ymin, b.y[i]); ymax = std::max(ymax, b.y[i]);
    }
    if (rmax <= 0.0) return;

    // Cell size >= 2 r_max (the invariant that makes the 3x3 search exact).
    // If the bounding box is huge relative to the bodies (e.g. a few escaping
    // bodies), the cell count is capped and cells grow instead: larger cells
    // remain correct, only slower.
    const double minCell = 2.0 * rmax;
    const auto capPerAxis = static_cast<double>(std::max<std::size_t>(
        8, static_cast<std::size_t>(std::ceil(std::sqrt(4.0 * static_cast<double>(n))))));
    const double cw = std::max(minCell, (xmax - xmin) / capPerAxis);
    const double ch = std::max(minCell, (ymax - ymin) / capPerAxis);
    nx_ = static_cast<int>(std::floor((xmax - xmin) / cw)) + 1;
    ny_ = static_cast<int>(std::floor((ymax - ymin) / ch)) + 1;
    const std::size_t cells = static_cast<std::size_t>(nx_) * static_cast<std::size_t>(ny_);

    // Counting sort of bodies into cells. Bodies without a radius get a
    // sentinel cell and never take part.
    const auto none = static_cast<std::uint32_t>(cells);
    cellOf_.assign(n, none);
    cellStart_.assign(cells + 2, 0);
    for (std::size_t i = 0; i < n; ++i) {
        if (b.radius[i] <= 0.0 || !std::isfinite(b.x[i]) || !std::isfinite(b.y[i])) continue;
        const int gx = std::min(nx_ - 1, static_cast<int>((b.x[i] - xmin) / cw));
        const int gy = std::min(ny_ - 1, static_cast<int>((b.y[i] - ymin) / ch));
        cellOf_[i] = static_cast<std::uint32_t>(gy * nx_ + gx);
        ++cellStart_[cellOf_[i] + 1];
    }
    for (std::size_t c = 1; c < cellStart_.size(); ++c) cellStart_[c] += cellStart_[c - 1];
    sorted_.resize(n);
    cursor_.assign(cellStart_.begin(), cellStart_.end() - 1);
    for (std::size_t i = 0; i < n; ++i) {
        if (cellOf_[i] == none) continue;
        sorted_[cursor_[cellOf_[i]]++] = static_cast<std::uint32_t>(i);
    }

    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = cellOf_[i];
        if (c == none) continue;
        const int gx = static_cast<int>(c) % nx_;
        const int gy = static_cast<int>(c) / nx_;
        for (int oy = -1; oy <= 1; ++oy) {
            const int cy = gy + oy;
            if (cy < 0 || cy >= ny_) continue;
            for (int ox = -1; ox <= 1; ++ox) {
                const int cx = gx + ox;
                if (cx < 0 || cx >= nx_) continue;
                const std::size_t cell = static_cast<std::size_t>(cy * nx_ + cx);
                for (std::uint32_t k = cellStart_[cell]; k < cellStart_[cell + 1]; ++k) {
                    const std::uint32_t j = sorted_[k];
                    if (j <= i) continue;  // report each unordered pair once, from its lower index
                    ++candidateTests_;
                    if (overlaps(b, i, j)) out.emplace_back(static_cast<std::uint32_t>(i), j);
                }
            }
        }
    }
    std::sort(out.begin(), out.end());
}

ContactStats resolveContacts(World& world, UniformGrid& grid, std::vector<IndexPair>& pairs) {
    ContactStats stats;
    Bodies& b = world.bodies;
    const WorldParams& p = world.params;

    if (p.collisions) {
        grid.findOverlaps(b, pairs);
        stats.bodyContacts = pairs.size();
        const double e = p.restitution;
        for (const auto& [i, j] : pairs) {
            const double wi = b.invMass(i);
            const double wj = b.invMass(j);
            const double wsum = wi + wj;
            if (wsum <= 0.0) continue;  // two anchors

            const double dx = b.x[j] - b.x[i];
            const double dy = b.y[j] - b.y[i];
            const double dist = std::sqrt(dx * dx + dy * dy);
            const double overlap = b.radius[i] + b.radius[j] - dist;
            if (overlap <= 0.0) continue;  // separated by an earlier correction this step
            double nx = 1.0, ny = 0.0;     // coincident centres: deterministic fallback normal
            if (dist > 1e-15) { nx = dx / dist; ny = dy / dist; }

            // Positional projection, weighted so the centre of mass is unchanged.
            b.x[i] -= nx * overlap * wi / wsum; b.y[i] -= ny * overlap * wi / wsum;
            b.x[j] += nx * overlap * wj / wsum; b.y[j] += ny * overlap * wj / wsum;

            const double vn = (b.vx[j] - b.vx[i]) * nx + (b.vy[j] - b.vy[i]) * ny;
            if (vn >= 0.0) continue;  // already separating
            const double J = -(1.0 + e) * vn / wsum;
            b.vx[i] -= J * wi * nx; b.vy[i] -= J * wi * ny;
            b.vx[j] += J * wj * nx; b.vy[j] += J * wj * ny;
            ++stats.impulses;
        }
    }

    if (p.walls) {
        // A body that has crossed a wall is moved back inside by e_w times its
        // penetration depth (an exact mirror for e_w = 1, the usual event-free
        // treatment of a bounce) and its normal velocity is reflected and scaled.
        const Box& bx = p.box;
        const double e = p.wallRestitution;
        for (std::size_t i = 0; i < b.size(); ++i) {
            if (b.fixed[i]) continue;
            const double r = b.radius[i];
            bool hit = false;
            if (b.x[i] - r < bx.xmin) { const double d = bx.xmin + r - b.x[i]; b.x[i] = bx.xmin + r + e * d; if (b.vx[i] < 0) b.vx[i] = -e * b.vx[i]; hit = true; }
            if (b.x[i] + r > bx.xmax) { const double d = b.x[i] + r - bx.xmax; b.x[i] = bx.xmax - r - e * d; if (b.vx[i] > 0) b.vx[i] = -e * b.vx[i]; hit = true; }
            if (b.y[i] - r < bx.ymin) { const double d = bx.ymin + r - b.y[i]; b.y[i] = bx.ymin + r + e * d; if (b.vy[i] < 0) b.vy[i] = -e * b.vy[i]; hit = true; }
            if (b.y[i] + r > bx.ymax) { const double d = b.y[i] + r - bx.ymax; b.y[i] = bx.ymax - r - e * d; if (b.vy[i] > 0) b.vy[i] = -e * b.vy[i]; hit = true; }
            // A body faster than the box is wide could still be outside after one
            // reflection; clamp so the state stays valid.
            b.x[i] = std::clamp(b.x[i], bx.xmin + r, bx.xmax - r);
            b.y[i] = std::clamp(b.y[i], bx.ymin + r, bx.ymax - r);
            if (hit) ++stats.wallHits;
        }
    }
    return stats;
}

}  // namespace rocksim
