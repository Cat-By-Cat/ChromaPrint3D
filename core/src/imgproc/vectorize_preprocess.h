#pragma once

/// \file vectorize_preprocess.h
/// \brief Preprocessing for the vectorization pipeline (Lanczos upscale + Mean Shift smoothing).

#include <opencv2/core.hpp>

namespace ChromaPrint3D::detail {

struct PreprocessResult {
    cv::Mat bgr;
    float scale = 1.0f;
};

/// Conditionally upscale small images and apply color smoothing.
///
/// Lanczos 2x upscale triggers only when short_edge < 600 AND total_pixels < 1MP.
/// Mean Shift filtering is applied when \p enable_color_smoothing is true (skip for binary mode).
PreprocessResult PreprocessForVectorize(const cv::Mat& bgr, bool enable_color_smoothing = true);

} // namespace ChromaPrint3D::detail
