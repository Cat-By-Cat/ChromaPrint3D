#include <gtest/gtest.h>

#include "chromaprint3d/vectorizer.h"
#include "imgproc/bezier.h"

#include <nanosvg/nanosvg.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace ChromaPrint3D;

namespace {

struct RasterizedSvg {
    cv::Mat bgr;      // CV_8UC3
    cv::Mat coverage; // CV_8UC1
};

cv::Scalar NsvgColorToBgr(unsigned int color) {
    int r = static_cast<int>((color >> 0) & 0xFF);
    int g = static_cast<int>((color >> 8) & 0xFF);
    int b = static_cast<int>((color >> 16) & 0xFF);
    return cv::Scalar(b, g, r);
}

std::vector<cv::Point> FlattenPathToPixels(const NSVGpath* path, int width, int height) {
    std::vector<cv::Point> out;
    if (!path || path->npts < 4 || width <= 0 || height <= 0) return out;

    std::vector<Vec2f> contour;
    contour.reserve(static_cast<size_t>(path->npts) * 2);
    contour.push_back({path->pts[0], path->pts[1]});

    for (int i = 0; i < path->npts - 1; i += 3) {
        const float* p = &path->pts[i * 2];
        Vec2f p0{p[0], p[1]};
        Vec2f p1{p[2], p[3]};
        Vec2f p2{p[4], p[5]};
        Vec2f p3{p[6], p[7]};
        detail::FlattenCubicBezier(p0, p1, p2, p3, 0.4f, contour);
    }

    if (path->closed && contour.size() > 1) {
        const Vec2f& a = contour.front();
        const Vec2f& b = contour.back();
        if (std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f) contour.pop_back();
    }

    out.reserve(contour.size());
    for (const Vec2f& p : contour) {
        int x = std::clamp(static_cast<int>(std::lround(p.x)), 0, width - 1);
        int y = std::clamp(static_cast<int>(std::lround(p.y)), 0, height - 1);
        out.emplace_back(x, y);
    }
    return out;
}

RasterizedSvg RasterizeSvg(const std::string& svg, int width, int height) {
    RasterizedSvg result;
    result.bgr      = cv::Mat(height, width, CV_8UC3, cv::Scalar(255, 255, 255));
    result.coverage = cv::Mat(height, width, CV_8UC1, cv::Scalar(0));

    std::vector<char> buf(svg.begin(), svg.end());
    buf.push_back('\0');

    NSVGimage* image = nsvgParse(buf.data(), "px", 96.0f);
    if (!image) return result;

    for (const NSVGshape* shape = image->shapes; shape != nullptr; shape = shape->next) {
        if (!(shape->flags & NSVG_FLAGS_VISIBLE)) continue;
        if (shape->fill.type != NSVG_PAINT_COLOR) continue;

        std::vector<std::vector<cv::Point>> contours;
        for (const NSVGpath* path = shape->paths; path != nullptr; path = path->next) {
            auto poly = FlattenPathToPixels(path, width, height);
            if (poly.size() >= 3) contours.push_back(std::move(poly));
        }
        if (contours.empty()) continue;

        cv::fillPoly(result.coverage, contours, cv::Scalar(255));
        cv::fillPoly(result.bgr, contours, NsvgColorToBgr(shape->fill.color));
    }

    nsvgDelete(image);
    return result;
}

VectorizerConfig BaseConfig() {
    VectorizerConfig cfg;
    cfg.num_colors             = 8;
    cfg.merge_lambda           = 25.0f;
    cfg.min_region_area        = 1;
    cfg.morph_kernel_size      = 0;
    cfg.min_contour_area       = 1.0f;
    cfg.min_boundary_perimeter = 1.0f;
    cfg.alpha_max              = 1.0f;
    cfg.opt_tolerance          = 0.2f;
    cfg.enable_curve_opt       = true;
    cfg.svg_enable_stroke      = false;
    cfg.svg_stroke_width       = 0.5f;
    return cfg;
}

} // namespace

TEST(Vectorizer, KeepsTopLeftRegionAndNoNegativePathCoords) {
    cv::Mat img(32, 32, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::rectangle(img, cv::Rect(0, 0, 8, 8), cv::Scalar(0, 0, 0), cv::FILLED);

    VectorizerConfig cfg = BaseConfig();
    cfg.num_colors       = 2;
    auto out             = Vectorize(img, cfg);

    EXPECT_EQ(out.width, 32);
    EXPECT_EQ(out.height, 32);
    EXPECT_EQ(out.svg_content.find("M-"), std::string::npos);
    EXPECT_EQ(out.svg_content.find("C-"), std::string::npos);

    auto raster  = RasterizeSvg(out.svg_content, out.width, out.height);
    cv::Vec3b px = raster.bgr.at<cv::Vec3b>(1, 1);
    EXPECT_LT(static_cast<int>(px[0]), 100);
    EXPECT_LT(static_cast<int>(px[1]), 100);
    EXPECT_LT(static_cast<int>(px[2]), 100);
}

TEST(Vectorizer, CoverageNearFullForSolidPartitionImage) {
    cv::Mat img(36, 48, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::rectangle(img, cv::Rect(0, 0, 16, 36), cv::Scalar(0, 0, 255), cv::FILLED);  // red
    cv::rectangle(img, cv::Rect(16, 0, 16, 36), cv::Scalar(0, 255, 0), cv::FILLED); // green
    cv::rectangle(img, cv::Rect(32, 0, 16, 36), cv::Scalar(255, 0, 0), cv::FILLED); // blue

    VectorizerConfig cfg = BaseConfig();
    cfg.num_colors       = 3;
    auto out             = Vectorize(img, cfg);
    auto raster          = RasterizeSvg(out.svg_content, out.width, out.height);

    int filled   = cv::countNonZero(raster.coverage);
    int total    = out.width * out.height;
    double ratio = (total > 0) ? static_cast<double>(filled) / static_cast<double>(total) : 0.0;

    EXPECT_GT(ratio, 0.995);
}

TEST(Vectorizer, KeepsOnePixelBlackLineContinuous) {
    cv::Mat img(64, 64, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::line(img, cv::Point(0, 32), cv::Point(63, 32), cv::Scalar(0, 0, 0), 1, cv::LINE_8);

    VectorizerConfig cfg = BaseConfig();
    cfg.num_colors       = 2;
    auto out             = Vectorize(img, cfg);
    auto raster          = RasterizeSvg(out.svg_content, out.width, out.height);

    cv::Mat gray;
    cv::cvtColor(raster.bgr, gray, cv::COLOR_BGR2GRAY);

    cv::Mat dark;
    cv::threshold(gray, dark, 60, 255, cv::THRESH_BINARY_INV);

    cv::Mat cc_labels;
    int cc = cv::connectedComponents(dark, cc_labels, 8, CV_32S);
    std::vector<int> areas(std::max(0, cc), 0);
    for (int r = 0; r < cc_labels.rows; ++r) {
        const int* row = cc_labels.ptr<int>(r);
        for (int c = 0; c < cc_labels.cols; ++c) {
            int id = row[c];
            if (id >= 0 && id < static_cast<int>(areas.size())) areas[id]++;
        }
    }

    int significant_components = 0;
    int max_area               = 0;
    for (int id = 1; id < static_cast<int>(areas.size()); ++id) {
        if (areas[id] >= 10) significant_components++;
        max_area = std::max(max_area, areas[id]);
    }

    EXPECT_LE(significant_components, 1);
    EXPECT_GE(max_area, 40);

    int covered_columns = 0;
    for (int x = 0; x < out.width; ++x) {
        bool has_dark = false;
        for (int y = 29; y <= 35 && y < out.height; ++y) {
            if (y >= 0 && dark.at<uint8_t>(y, x) > 0) {
                has_dark = true;
                break;
            }
        }
        if (has_dark) covered_columns++;
    }
    EXPECT_GE(covered_columns, 58);
}
