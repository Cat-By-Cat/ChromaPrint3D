#pragma once

/// \file vectorizer.h
/// \brief Public API for raster-to-SVG vectorization.

#include "color.h"
#include "common.h"

#include <opencv2/core.hpp>

#include <string>
#include <vector>

namespace ChromaPrint3D {

/// Configuration for the vectorization pipeline.
struct VectorizerConfig {
    // ── Color segmentation ──────────────────────────────────────────────────
    int num_colors         = 16;    ///< K-Means initial palette size.
    float merge_lambda     = 25.0f; ///< Max Lab color distance² for region merge.
    int min_region_area    = 10;    ///< Force-merge regions smaller than this (pixels²).
    ColorSpace color_space = ColorSpace::Lab;

    // ── Contour processing ──────────────────────────────────────────────────
    int morph_kernel_size  = 3;     ///< Median filter kernel for label smoothing (0 = disable).
    float min_contour_area = 10.0f; ///< Discard contours smaller than this (pixels²).
    float min_boundary_perimeter =
        2.0f; ///< Discard loops shorter than this boundary length (pixels).

    // ── Potrace-style fitting ───────────────────────────────────────────────
    float alpha_max       = 1.0f; ///< Corner threshold (0 = all corners, >4/3 = all smooth).
    float opt_tolerance   = 0.2f; ///< Curve optimization merge tolerance.
    bool enable_curve_opt = true; ///< Enable curve optimization pass.

    // ── Schneider fallback ──────────────────────────────────────────────────
    float curve_tolerance  = 2.0f;   ///< Schneider max fitting error (pixels).
    float corner_threshold = 135.0f; ///< Schneider corner angle threshold (degrees).

    // ── SVG output ──────────────────────────────────────────────────────────
    bool svg_enable_stroke = false; ///< Optional stroke output for visual debugging.
    float svg_stroke_width = 0.5f;  ///< Stroke width when svg_enable_stroke is true.
};

/// Result of the vectorization pipeline.
struct VectorizerResult {
    std::string svg_content;  ///< Complete SVG document.
    int width      = 0;       ///< Image width in pixels.
    int height     = 0;       ///< Image height in pixels.
    int num_shapes = 0;       ///< Number of shapes in the SVG.
    std::vector<Rgb> palette; ///< Color palette used.
};

/// Vectorize a raster image file to SVG.
///
/// \param image_path  Path to input image (PNG, JPG, BMP, etc.).
/// \param config      Pipeline configuration.
/// \return            Vectorization result with SVG content.
VectorizerResult Vectorize(const std::string& image_path, const VectorizerConfig& config = {});

/// Vectorize an in-memory image buffer to SVG (ICC-aware).
///
/// \param image_data  Pointer to encoded image bytes (JPEG, PNG, etc.).
/// \param image_size  Number of bytes.
/// \param config      Pipeline configuration.
/// \return            Vectorization result with SVG content.
VectorizerResult Vectorize(const uint8_t* image_data, size_t image_size,
                           const VectorizerConfig& config = {});

/// Vectorize a BGR cv::Mat to SVG.
///
/// \param bgr_image   Input image in BGR uint8 format.
/// \param config      Pipeline configuration.
/// \return            Vectorization result with SVG content.
VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config = {});

} // namespace ChromaPrint3D
