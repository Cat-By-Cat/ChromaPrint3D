#pragma once

/// \file label_boundary_graph.h
/// \brief Build shared boundary loops directly from a region label map.

#include "chromaprint3d/vec2.h"

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

struct RegionBoundaryLoops {
    std::vector<std::vector<Vec2f>> loops;
};

/// Build per-region boundary loops from a CV_32SC1 label map.
///
/// The output loops are extracted on the pixel-corner grid [0..cols] x [0..rows].
/// Adjacent regions therefore share exactly the same geometric boundary.
std::vector<RegionBoundaryLoops> BuildRegionBoundaryLoops(const cv::Mat& labels, int num_regions);

/// Signed polygon area (shoelace), using the loop's current orientation.
double SignedLoopArea(const std::vector<Vec2f>& loop);

/// Closed-loop perimeter length.
double LoopPerimeter(const std::vector<Vec2f>& loop);

} // namespace ChromaPrint3D::detail
