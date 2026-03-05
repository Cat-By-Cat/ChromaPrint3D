#pragma once

/// \file potrace_adapter.h
/// \brief Mask-to-polygon tracing adapter (Potrace-style interface).

#include "bezier.h"
#include "chromaprint3d/vec2.h"

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

struct TracedPolygonGroup {
    std::vector<Vec2f> outer;
    std::vector<std::vector<Vec2f>> holes;
    double area = 0.0;
};

/// Trace a binary mask into polygon groups with Potrace (legacy polygon path).
std::vector<TracedPolygonGroup> TraceMaskWithPotrace(const cv::Mat& mask, float simplify_epsilon);

double SignedArea(const std::vector<Vec2f>& ring);

struct TracedBezierGroup {
    BezierContour outer;
    std::vector<BezierContour> holes;
    double area = 0.0;
};

/// Trace a binary mask preserving Potrace's native cubic Bezier curves and path hierarchy.
std::vector<TracedBezierGroup> TraceMaskWithPotraceBezier(const cv::Mat& mask, int turdsize = 2,
                                                          double opttolerance = 0.2);

} // namespace ChromaPrint3D::detail
