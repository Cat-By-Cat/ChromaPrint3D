#include "chromaprint3d/vectorizer.h"

#include "detail/cv_utils.h"
#include "detail/icc_utils.h"
#include "imgproc/vectorize_potrace_pipeline.h"

#include <stdexcept>

namespace ChromaPrint3D {

namespace {

struct PreparedVectorizeInput {
    cv::Mat bgr;
    cv::Mat opaque_mask;
};

PreparedVectorizeInput PrepareVectorizeInput(const cv::Mat& input) {
    PreparedVectorizeInput prepared;
    if (input.empty()) { return prepared; }

    if (input.channels() == 4) {
        prepared.opaque_mask = detail::ExtractOpaqueMask(input, 0);
        prepared.bgr         = detail::EnsureBgr(input, detail::BgraPolicy::DropAlpha);
        return prepared;
    }

    prepared.bgr = detail::EnsureBgr(input);
    return prepared;
}

} // namespace

VectorizerResult Vectorize(const std::string& image_path, const VectorizerConfig& config) {
    cv::Mat img   = detail::LoadImageIcc(image_path);
    auto prepared = PrepareVectorizeInput(img);
    if (prepared.bgr.empty()) throw std::runtime_error("Failed to load image: " + image_path);
    return detail::VectorizePotracePipeline(prepared.bgr, config, prepared.opaque_mask);
}

VectorizerResult Vectorize(const uint8_t* image_data, size_t image_size,
                           const VectorizerConfig& config) {
    cv::Mat img   = detail::LoadImageIcc(image_data, image_size);
    auto prepared = PrepareVectorizeInput(img);
    if (prepared.bgr.empty()) throw std::runtime_error("Failed to decode image buffer");
    return detail::VectorizePotracePipeline(prepared.bgr, config, prepared.opaque_mask);
}

VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config) {
    auto prepared = PrepareVectorizeInput(bgr_image);
    if (prepared.bgr.empty()) throw std::runtime_error("Empty input image");
    return detail::VectorizePotracePipeline(prepared.bgr, config, prepared.opaque_mask);
}

} // namespace ChromaPrint3D
