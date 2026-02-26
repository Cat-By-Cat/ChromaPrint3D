#pragma once

/// \file optimal_polygon.h
/// \brief Potrace-style optimal polygon approximation of closed pixel paths.
/// Based on Peter Selinger, "Potrace: a polygon-based tracing algorithm", 2003.

#include "chromaprint3d/vec2.h"

#include <vector>

namespace ChromaPrint3D::detail {

struct OptimalPolygon {
    std::vector<int> vertices;
    int segment_count    = 0;
    double total_penalty = 0.0;
};

/// Find the optimal polygon for a closed path of integer-coordinate points.
///
/// The algorithm finds a polygon with the minimum number of segments, and
/// among those, the one with the minimum total penalty. A segment from i to j
/// is "possible" if the sub-path can be approximated by a straight line.
///
/// Complexity: O(n * m) where n = path length, m = max segment span.
///
/// \param path  Closed path of pixel-corner coordinates (path.back() == path.front()
///              is NOT required; the path is implicitly closed).
/// \return      OptimalPolygon with vertex indices into `path`.
OptimalPolygon FindOptimalPolygon(const std::vector<Vec2f>& path);

/// Adjust polygon vertices for best fit to the original path.
/// Each vertex is moved within a half-pixel box to minimize distance to
/// the best-fit lines of adjacent segments.
///
/// \param path      Original closed path.
/// \param polygon   Vertex indices from FindOptimalPolygon.
/// \return          Adjusted vertex positions (same count as polygon.vertices).
std::vector<Vec2f> AdjustVertices(const std::vector<Vec2f>& path, const OptimalPolygon& polygon);

} // namespace ChromaPrint3D::detail
