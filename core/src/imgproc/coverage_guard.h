#pragma once

/// \file coverage_guard.h
/// \brief Coverage validation and patching for vectorized output.

#include "imgproc/svg_writer.h"

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

void ApplyCoverageGuard(std::vector<VectorizedShape>& shapes, const cv::Mat& labels,
                        const std::vector<Rgb>& palette, float min_ratio, float tracing_epsilon,
                        float min_patch_area);

} // namespace ChromaPrint3D::detail
