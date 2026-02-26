#pragma once

/// \file raster_proc.h
/// \brief Raster image preprocessing (resize, denoise, alpha mask, color conversion).

#include "common.h"

#include <cstdint>
#include <string>
#include <vector>

#include <opencv2/core.hpp>

namespace ChromaPrint3D {

/// Result of raster image preprocessing.
struct RasterProcResult {
    std::string name;

    int width  = 0;
    int height = 0;

    cv::Mat rgb;  ///< H x W, CV_32FC3 linear RGB
    cv::Mat lab;  ///< H x W, CV_32FC3 CIE Lab
    cv::Mat mask; ///< H x W, CV_8UC1
};

/// Configuration for raster image preprocessing.
struct RasterProcConfig {
    float scale = 1.0f; ///< Requested scale factor.

    int max_width  = 0; ///< Maximum output width (0 = no limit).
    int max_height = 0; ///< Maximum output height (0 = no limit).

    bool use_alpha_mask     = true;
    uint8_t alpha_threshold = 1; ///< alpha <= threshold is treated as transparent.

    ResizeMethod upsample_method   = ResizeMethod::Nearest;
    ResizeMethod downsample_method = ResizeMethod::Area;

    DenoiseMethod denoise_method = DenoiseMethod::None;
    int denoise_kernel           = 3;
    int bilateral_diameter       = 5;
    float bilateral_sigma_color  = 25.0f;
    float bilateral_sigma_space  = 5.0f;
};

/// Raster image preprocessor: resizes, denoises, extracts alpha mask, and
/// converts to linear RGB and Lab.
class RasterProc {
public:
    explicit RasterProc(const RasterProcConfig& config = {});

    /// Process an image from a file path.
    RasterProcResult Run(const std::string& path) const;

    /// Process an already-loaded cv::Mat.
    RasterProcResult Run(const cv::Mat& input, const std::string& name = "") const;

    /// Process an image from an in-memory buffer (PNG, JPEG, etc.).
    RasterProcResult RunFromBuffer(const std::vector<uint8_t>& buffer,
                                   const std::string& name = "") const;

    /// Read-only access to the current configuration.
    const RasterProcConfig& config() const { return config_; }

private:
    RasterProcConfig config_;

    void Resize(const cv::Mat& input, cv::Mat& resized) const;
    void ExtractAlphaMask(const cv::Mat& input, const cv::Size& target_size, cv::Mat& mask) const;
    void Denoise(const cv::Mat& input, cv::Mat& denoised) const;
};

} // namespace ChromaPrint3D
