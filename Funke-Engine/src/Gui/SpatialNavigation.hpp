#pragma once
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace Funke::SpatialNavigation {
    struct Point { double X, Y; };
    inline constexpr size_t None = std::numeric_limits<size_t>::max();

    // Axis-aligned direction, Y up. Equal scores keep the caller's stable draw order.
    inline size_t Pick(Point origin, Point direction, std::span<const Point> candidates, bool wrap) {
        size_t best = None;
        double bestScore = std::numeric_limits<double>::infinity();
        if (!std::isfinite(origin.X) || !std::isfinite(origin.Y)) return None;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const double x = candidates[i].X - origin.X, y = candidates[i].Y - origin.Y;
            const double forward = x * direction.X + y * direction.Y;
            if (!(forward > 0)) continue;
            const double score = (x * x + y * y) / forward;
            if (score < bestScore) { bestScore = score; best = i; }
        }
        if (best != None || !wrap) return best;
        // At an edge, wrap to the opposite edge in the closest row/column.
        double bestLateral = std::numeric_limits<double>::infinity();
        double bestForward = 0;
        for (size_t i = 0; i < candidates.size(); ++i) {
            const double x = candidates[i].X - origin.X, y = candidates[i].Y - origin.Y;
            const double forward = x * direction.X + y * direction.Y;
            const double lateral = std::abs(x * direction.Y - y * direction.X);
            if (!(forward < 0) || !std::isfinite(lateral) || !std::isfinite(forward)) continue;
            if (lateral < bestLateral || (lateral == bestLateral && forward < bestForward)) {
                bestLateral = lateral; bestForward = forward; best = i;
            }
        }
        return best;
    }
}
