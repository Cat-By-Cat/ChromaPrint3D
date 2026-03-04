#pragma once

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

struct SlicConfig {
    int target_superpixels = 256;
    float compactness      = 10.0f;
    int iterations         = 10;
    float min_region_ratio = 0.25f;
};

struct SlicResult {
    cv::Mat labels; // H x W, CV_32SC1, -1 means invalid pixel.
    std::vector<cv::Vec3f> centers;
};

SlicResult SegmentBySlic(const cv::Mat& target, const cv::Mat& mask, const SlicConfig& cfg);

} // namespace ChromaPrint3D::detail
