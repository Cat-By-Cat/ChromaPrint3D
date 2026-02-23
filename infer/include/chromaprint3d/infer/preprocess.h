#pragma once

/// \file preprocess.h
/// \brief Image preprocessing utilities for inference (cv::Mat <-> Tensor).

#include "tensor.h"

#include <array>
#include <cstdint>

namespace cv {
class Mat;
}

namespace ChromaPrint3D::infer {

enum class ChannelOrder : uint8_t { kRGB = 0, kBGR = 1 };
enum class LayoutOrder : uint8_t { kNCHW = 0, kNHWC = 1 };

/// Configuration for image-to-tensor preprocessing.
struct PreprocessConfig {
    int target_width       = 0;    ///< Target width (0 = keep original).
    int target_height      = 0;    ///< Target height (0 = keep original).
    bool keep_aspect_ratio = true; ///< Pad to target size preserving aspect ratio.
    int pad_value          = 0;    ///< Padding fill value.

    ChannelOrder channel_order = ChannelOrder::kRGB;
    LayoutOrder layout         = LayoutOrder::kNCHW;

    bool normalize            = true;
    std::array<float, 3> mean = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> std  = {1.0f, 1.0f, 1.0f};
    float scale               = 1.0f / 255.0f; ///< Applied before mean/std normalization.
};

/// Metadata produced by PreprocessImage, needed to undo transforms in postprocessing.
struct PreprocessMeta {
    int original_width  = 0;
    int original_height = 0;
    int padded_width    = 0;
    int padded_height   = 0;
    int pad_left        = 0;
    int pad_top         = 0;
    float scale_x       = 1.0f;
    float scale_y       = 1.0f;
};

/// Convert a cv::Mat (BGR uint8) into an inference-ready float32 Tensor.
Tensor PreprocessImage(const cv::Mat& image, const PreprocessConfig& config,
                       PreprocessMeta* meta = nullptr);

/// Convert a model output mask tensor back to a cv::Mat at the original image size.
/// The output is CV_8UC1 with values 0 or 255.
cv::Mat PostprocessMask(const Tensor& output, const PreprocessMeta& meta, float threshold = 0.5f);

} // namespace ChromaPrint3D::infer
