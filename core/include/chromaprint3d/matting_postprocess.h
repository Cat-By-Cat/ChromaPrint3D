#pragma once

/// \file matting_postprocess.h
/// \brief Post-processing utilities for matting results: thresholding, morphological
///        closing, outline drawing, small-region filtering, and optional foreground reframe.

#include "export.h"

#include <opencv2/core/mat.hpp>

#include <array>
#include <cstdint>

namespace ChromaPrint3D {

/// Outline drawing mode (morphology-based).
enum class OutlineMode : uint8_t {
    Center = 0, ///< Straddles the mask boundary (dilate + erode).
    Inner  = 1, ///< Drawn inside the foreground region.
    Outer  = 2, ///< Drawn outside the foreground region.
};

/// Parameters controlling matting post-processing.
struct CHROMAPRINT3D_API MattingPostprocessParams {
    float threshold            = 0.5f; ///< Alpha binarization threshold in [0, 1].
    int morph_close_size       = 0;    ///< Morphological close kernel diameter (0 = skip).
    int morph_close_iterations = 1;    ///< Number of close iterations (>=1).
    int min_region_area        = 0; ///< Remove connected components smaller than this (0 = skip).

    bool reframe_enabled   = false; ///< Crop to foreground bbox with optional outer padding.
    int reframe_padding_px = 16;    ///< Extra pixels reserved around bbox when reframing.

    bool outline_enabled = false;
    int outline_width    = 2; ///< Base outline width in pixels (auto-scales up
                              ///< for large images by mask short-side).
    std::array<uint8_t, 3> outline_color = {0, 0, 255}; ///< Outline color (BGR).
    OutlineMode outline_mode             = OutlineMode::Center;
};

/// Result of matting post-processing.
struct CHROMAPRINT3D_API MattingPostprocessResult {
    cv::Mat mask;       ///< Processed binary mask (CV_8UC1, 0/255).
    cv::Mat foreground; ///< Foreground composite (BGRA).
    cv::Mat outline;    ///< Outline overlay (BGRA, transparent background). Empty when disabled.
};

/// Apply post-processing to a raw alpha map.
/// \param alpha    Raw alpha map (CV_8UC1, 0-255). If empty, \p fallback_mask is used instead.
/// \param fallback_mask Binary mask to use when alpha is unavailable (CV_8UC1).
/// \param original Original BGR image for foreground compositing.
/// \param params   Post-processing parameters.
CHROMAPRINT3D_API MattingPostprocessResult
ApplyMattingPostprocess(const cv::Mat& alpha, const cv::Mat& fallback_mask, const cv::Mat& original,
                        const MattingPostprocessParams& params);

} // namespace ChromaPrint3D
