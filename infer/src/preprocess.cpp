#include "chromaprint3d/infer/preprocess.h"
#include "chromaprint3d/infer/error.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cstring>

namespace ChromaPrint3D::infer {

Tensor PreprocessImage(const cv::Mat& image, const PreprocessConfig& config, PreprocessMeta* meta) {
    if (image.empty()) throw InputError("PreprocessImage: empty input image");

    cv::Mat src = image;

    // Ensure BGR uint8
    if (src.depth() != CV_8U) { src.convertTo(src, CV_8U, 255.0); }
    if (src.channels() == 1) {
        cv::cvtColor(src, src, cv::COLOR_GRAY2BGR);
    } else if (src.channels() == 4) {
        cv::cvtColor(src, src, cv::COLOR_BGRA2BGR);
    }

    const int orig_w = src.cols;
    const int orig_h = src.rows;

    // Target dimensions
    int target_w = config.target_width > 0 ? config.target_width : orig_w;
    int target_h = config.target_height > 0 ? config.target_height : orig_h;

    // Resize
    int pad_left = 0, pad_top = 0;
    int padded_w = target_w, padded_h = target_h;
    float sx = 1.0f, sy = 1.0f;

    if (config.keep_aspect_ratio && (target_w != orig_w || target_h != orig_h)) {
        const float ratio =
            std::min(static_cast<float>(target_w) / orig_w, static_cast<float>(target_h) / orig_h);
        const int new_w = static_cast<int>(std::round(orig_w * ratio));
        const int new_h = static_cast<int>(std::round(orig_h * ratio));

        cv::Mat resized;
        cv::resize(src, resized, cv::Size(new_w, new_h), 0, 0, cv::INTER_LINEAR);

        pad_left             = (target_w - new_w) / 2;
        pad_top              = (target_h - new_h) / 2;
        const int pad_right  = target_w - new_w - pad_left;
        const int pad_bottom = target_h - new_h - pad_top;

        cv::copyMakeBorder(resized, src, pad_top, pad_bottom, pad_left, pad_right,
                           cv::BORDER_CONSTANT,
                           cv::Scalar(config.pad_value, config.pad_value, config.pad_value));

        sx = ratio;
        sy = ratio;
    } else if (target_w != orig_w || target_h != orig_h) {
        cv::resize(src, src, cv::Size(target_w, target_h), 0, 0, cv::INTER_LINEAR);
        sx = static_cast<float>(target_w) / orig_w;
        sy = static_cast<float>(target_h) / orig_h;
    }

    if (meta) {
        meta->original_width  = orig_w;
        meta->original_height = orig_h;
        meta->padded_width    = padded_w;
        meta->padded_height   = padded_h;
        meta->pad_left        = pad_left;
        meta->pad_top         = pad_top;
        meta->scale_x         = sx;
        meta->scale_y         = sy;
    }

    // Channel reorder: BGR → RGB if needed
    if (config.channel_order == ChannelOrder::kRGB) { cv::cvtColor(src, src, cv::COLOR_BGR2RGB); }

    // Convert to float32 and apply scale + normalization
    cv::Mat float_img;
    src.convertTo(float_img, CV_32F);

    if (config.normalize) {
        const float s = config.scale;
        const cv::Scalar mean(config.mean[0], config.mean[1], config.mean[2]);
        const cv::Scalar std_val(config.std[0], config.std[1], config.std[2]);

        // (pixel * scale - mean) / std
        float_img *= s;
        cv::subtract(float_img, mean, float_img);

        // Per-channel divide by std
        std::vector<cv::Mat> channels;
        cv::split(float_img, channels);
        for (int c = 0; c < 3; ++c) {
            if (std_val[c] != 0.0 && std_val[c] != 1.0) {
                channels[static_cast<size_t>(c)] /= static_cast<float>(std_val[c]);
            }
        }
        cv::merge(channels, float_img);
    }

    // Layout conversion
    const int h = float_img.rows;
    const int w = float_img.cols;
    const int c = float_img.channels();

    if (config.layout == LayoutOrder::kNCHW) {
        // HWC → 1×C×H×W
        Tensor t                = Tensor::Allocate(DataType::kFloat32, {1, c, h, w});
        float* dst              = t.DataAs<float>();
        const size_t plane_size = static_cast<size_t>(h) * static_cast<size_t>(w);

        std::vector<cv::Mat> channels;
        cv::split(float_img, channels);
        for (int ch = 0; ch < c; ++ch) {
            const cv::Mat& plane = channels[static_cast<size_t>(ch)];
            std::memcpy(dst + static_cast<size_t>(ch) * plane_size, plane.data,
                        plane_size * sizeof(float));
        }
        return t;
    }

    // NHWC: 1×H×W×C
    Tensor t = Tensor::Allocate(DataType::kFloat32, {1, h, w, c});
    std::memcpy(t.Data(), float_img.data, static_cast<size_t>(h) * w * c * sizeof(float));
    return t;
}

cv::Mat PostprocessMask(const Tensor& output, const PreprocessMeta& meta, float threshold) {
    if (output.Empty()) throw InputError("PostprocessMask: empty tensor");

    // Extract H, W from tensor shape
    const int ndim = output.NDim();
    int h{}, w{};
    bool is_chw = false;

    if (ndim == 4) {
        // N×C×H×W or N×H×W×C
        if (output.Shape(1) == 1) {
            h      = static_cast<int>(output.Shape(2));
            w      = static_cast<int>(output.Shape(3));
            is_chw = true;
        } else {
            h = static_cast<int>(output.Shape(1));
            w = static_cast<int>(output.Shape(2));
        }
    } else if (ndim == 3) {
        if (output.Shape(0) == 1) {
            h      = static_cast<int>(output.Shape(1));
            w      = static_cast<int>(output.Shape(2));
            is_chw = true;
        } else {
            h = static_cast<int>(output.Shape(0));
            w = static_cast<int>(output.Shape(1));
        }
    } else if (ndim == 2) {
        h = static_cast<int>(output.Shape(0));
        w = static_cast<int>(output.Shape(1));
    } else {
        throw InputError("PostprocessMask: unsupported tensor shape");
    }

    // Create float mask from tensor data
    cv::Mat float_mask(h, w, CV_32FC1, const_cast<void*>(output.Data()));

    // Threshold to binary
    cv::Mat binary;
    cv::threshold(float_mask, binary, static_cast<double>(threshold), 255.0, cv::THRESH_BINARY);
    binary.convertTo(binary, CV_8U);

    // Remove padding and resize back to original dimensions
    const int content_w = static_cast<int>(std::round(meta.original_width * meta.scale_x));
    const int content_h = static_cast<int>(std::round(meta.original_height * meta.scale_y));

    int crop_x = meta.pad_left;
    int crop_y = meta.pad_top;
    int crop_w = std::min(content_w, w - crop_x);
    int crop_h = std::min(content_h, h - crop_y);
    crop_w     = std::max(crop_w, 1);
    crop_h     = std::max(crop_h, 1);

    cv::Mat cropped = binary(cv::Rect(crop_x, crop_y, crop_w, crop_h));

    // Resize to original size
    cv::Mat result;
    cv::resize(cropped, result, cv::Size(meta.original_width, meta.original_height), 0, 0,
               cv::INTER_LINEAR);

    // Re-threshold after resize interpolation
    cv::threshold(result, result, 127.0, 255.0, cv::THRESH_BINARY);
    return result;
}

} // namespace ChromaPrint3D::infer
