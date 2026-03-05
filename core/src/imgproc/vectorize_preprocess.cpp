#include "imgproc/vectorize_preprocess.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace ChromaPrint3D::detail {

PreprocessResult PreprocessForVectorize(const cv::Mat& bgr, bool enable_color_smoothing) {
    PreprocessResult result;
    result.bgr   = bgr;
    result.scale = 1.0f;

    const int h            = bgr.rows;
    const int w            = bgr.cols;
    const int short_edge   = std::min(h, w);
    const int total_pixels = h * w;

    if (short_edge < 600 && total_pixels < 1000000) {
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

    if (enable_color_smoothing) {
        cv::Mat filtered;
        cv::pyrMeanShiftFiltering(result.bgr, filtered, 15, 25);
        result.bgr = filtered;
    }

    return result;
}

} // namespace ChromaPrint3D::detail
