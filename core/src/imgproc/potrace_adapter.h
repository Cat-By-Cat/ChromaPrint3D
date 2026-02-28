#pragma once

/// \file potrace_adapter.h
/// \brief Mask-to-polygon tracing adapter (Potrace-style interface).

#include "chromaprint3d/vec2.h"

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

struct TracedPolygonGroup {
    std::vector<Vec2f> outer;
    std::vector<std::vector<Vec2f>> holes;
    double area = 0.0;
};

/// Trace a binary mask into polygon groups with Potrace.
std::vector<TracedPolygonGroup> TraceMaskWithPotrace(const cv::Mat& mask, float simplify_epsilon);

double SignedArea(const std::vector<Vec2f>& ring);

} // namespace ChromaPrint3D::detail
