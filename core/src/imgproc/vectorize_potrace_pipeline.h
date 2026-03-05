#pragma once

#include "chromaprint3d/vectorizer.h"

#include <opencv2/core.hpp>

namespace ChromaPrint3D::detail {

VectorizerResult VectorizePotracePipeline(const cv::Mat& bgr, const VectorizerConfig& cfg,
                                          const cv::Mat& opaque_mask = cv::Mat());

} // namespace ChromaPrint3D::detail
