#include "imgproc/vectorize_preprocess.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ChromaPrint3D::detail {

PreprocessResult PreprocessForVectorize(const cv::Mat& bgr, bool enable_color_smoothing,
                                        float smoothing_spatial, float smoothing_color,
                                        int upscale_short_edge) {
    PreprocessResult result;
    result.bgr   = bgr;
    result.scale = 1.0f;

    const int h            = bgr.rows;
    const int w            = bgr.cols;
    const int short_edge   = std::min(h, w);
    const int total_pixels = h * w;

    const bool enable_upscale = upscale_short_edge > 0;
    if (enable_upscale && short_edge < upscale_short_edge && total_pixels < 1000000) {
        float factor = 2.0f;
        int new_h    = static_cast<int>(h * factor);
        int new_w    = static_cast<int>(w * factor);
        if (new_h * new_w > 4000000) {
            factor = std::sqrt(4000000.0f / static_cast<float>(total_pixels));
            new_h  = static_cast<int>(h * factor);
            new_w  = static_cast<int>(w * factor);
        }
        if (factor > 1.05f) {
            cv::resize(bgr, result.bgr, cv::Size(new_w, new_h), 0, 0, cv::INTER_LANCZOS4);
            result.scale = factor;
        }
    }

    const float sp = std::max(0.0f, smoothing_spatial);
    const float sr = std::max(0.0f, smoothing_color);
    if (enable_color_smoothing && sp > 0.0f && sr > 0.0f) {
        cv::Mat filtered;
        cv::pyrMeanShiftFiltering(result.bgr, filtered, sp, sr);
        result.bgr = filtered;
    }

    return result;
}

} // namespace ChromaPrint3D::detail
