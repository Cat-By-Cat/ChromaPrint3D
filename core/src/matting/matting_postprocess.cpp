#include "chromaprint3d/matting_postprocess.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <vector>

namespace ChromaPrint3D {

namespace {

cv::Mat EllipseKernel(int radius) {
    int d = std::max(radius * 2 + 1, 1);
    return cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(d, d));
}

cv::Mat CompositeForeground(const cv::Mat& bgr, const cv::Mat& mask) {
    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
    std::vector<cv::Mat> channels(4);
    cv::split(bgra, channels);
    channels[3] = mask;
    cv::merge(channels, bgra);
    return bgra;
}

cv::Mat DrawOutline(const cv::Mat& mask, int width, const std::array<uint8_t, 3>& color_bgr,
                    OutlineMode mode) {
    cv::Mat outline(mask.size(), CV_8UC4, cv::Scalar(0, 0, 0, 0));
    cv::Mat band;

    switch (mode) {
    case OutlineMode::Outer: {
        cv::Mat dilated;
        cv::dilate(mask, dilated, EllipseKernel(width));
        cv::subtract(dilated, mask, band);
        break;
    }
    case OutlineMode::Inner: {
        cv::Mat eroded;
        cv::erode(mask, eroded, EllipseKernel(width));
        cv::subtract(mask, eroded, band);
        break;
    }
    case OutlineMode::Center:
    default: {
        cv::Mat dilated, eroded;
        cv::dilate(mask, dilated, EllipseKernel((width + 1) / 2));
        cv::erode(mask, eroded, EllipseKernel(width / 2));
        cv::subtract(dilated, eroded, band);
        break;
    }
    }

    outline.setTo(cv::Scalar(color_bgr[0], color_bgr[1], color_bgr[2], 255), band);
    return outline;
}

void FilterSmallRegions(cv::Mat& mask, int min_area) {
    cv::Mat labels, stats, centroids;
    int n = cv::connectedComponentsWithStats(mask, labels, stats, centroids);

    cv::Mat filtered = cv::Mat::zeros(mask.size(), CV_8UC1);
    for (int i = 1; i < n; ++i) {
        if (stats.at<int>(i, cv::CC_STAT_AREA) >= min_area) { filtered.setTo(255, labels == i); }
    }
    mask = filtered;
}

} // namespace

MattingPostprocessResult ApplyMattingPostprocess(const cv::Mat& alpha, const cv::Mat& fallback_mask,
                                                 const cv::Mat& original,
                                                 const MattingPostprocessParams& params) {
    MattingPostprocessResult result;

    if (!alpha.empty()) {
        double thresh = std::clamp(static_cast<double>(params.threshold), 0.0, 1.0) * 255.0;
        cv::threshold(alpha, result.mask, thresh, 255.0, cv::THRESH_BINARY);
    } else if (!fallback_mask.empty()) {
        result.mask = fallback_mask.clone();
    } else {
        return result;
    }

    if (params.morph_close_size > 0) {
        int ksize      = params.morph_close_size | 1;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(ksize, ksize));
        int iterations = std::max(params.morph_close_iterations, 1);
        cv::morphologyEx(result.mask, result.mask, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1),
                         iterations);
    }

    if (params.min_region_area > 0) { FilterSmallRegions(result.mask, params.min_region_area); }

    if (!original.empty()) { result.foreground = CompositeForeground(original, result.mask); }

    if (params.outline_enabled && params.outline_width > 0) {
        result.outline = DrawOutline(result.mask, params.outline_width, params.outline_color,
                                     params.outline_mode);
    }

    return result;
}

} // namespace ChromaPrint3D
