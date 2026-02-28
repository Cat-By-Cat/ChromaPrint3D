#include "chromaprint3d/vectorizer.h"

#include "detail/cv_utils.h"
#include "detail/icc_utils.h"
#include "imgproc/vectorize_potrace_pipeline.h"

namespace ChromaPrint3D {

VectorizerResult Vectorize(const std::string& image_path, const VectorizerConfig& config) {
    cv::Mat img = detail::LoadImageIcc(image_path);
    cv::Mat bgr = detail::EnsureBgr(img);
    if (bgr.empty()) throw std::runtime_error("Failed to load image: " + image_path);
    return Vectorize(bgr, config);
}

VectorizerResult Vectorize(const uint8_t* image_data, size_t image_size,
                           const VectorizerConfig& config) {
    cv::Mat img = detail::LoadImageIcc(image_data, image_size);
    cv::Mat bgr = detail::EnsureBgr(img);
    if (bgr.empty()) throw std::runtime_error("Failed to decode image buffer");
    return Vectorize(bgr, config);
}

VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config) {
    cv::Mat bgr = detail::EnsureBgr(bgr_image);
    if (bgr.empty()) throw std::runtime_error("Empty input image");
    return detail::VectorizePotracePipeline(bgr, config);
}

} // namespace ChromaPrint3D
