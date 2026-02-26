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
    int num_colors         = 32;     ///< K-Means initial palette size.
    float merge_lambda     = 500.0f; ///< Region merge threshold (higher = more merging).
    int min_region_area    = 10;     ///< Force-merge regions smaller than this (pixels^2).
    ColorSpace color_space = ColorSpace::Lab;

    // ── Contour processing ──────────────────────────────────────────────────
    int morph_kernel_size  = 3;     ///< Morphological kernel size (0 = disable).
    float min_contour_area = 10.0f; ///< Discard contours smaller than this (pixels^2).

    // ── Potrace-style fitting ───────────────────────────────────────────────
    float alpha_max       = 1.0f; ///< Corner threshold (0 = all corners, >4/3 = all smooth).
    float opt_tolerance   = 0.2f; ///< Curve optimization merge tolerance.
    bool enable_curve_opt = true; ///< Enable curve optimization pass.

    // ── Schneider fallback ──────────────────────────────────────────────────
    float curve_tolerance  = 2.0f;   ///< Schneider max fitting error (pixels).
    float corner_threshold = 135.0f; ///< Schneider corner angle threshold (degrees).
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

/// Vectorize a BGR cv::Mat to SVG.
///
/// \param bgr_image   Input image in BGR uint8 format.
/// \param config      Pipeline configuration.
/// \return            Vectorization result with SVG content.
VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config = {});

} // namespace ChromaPrint3D
