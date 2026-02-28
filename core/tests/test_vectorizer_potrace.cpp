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
    cv::Mat bgr;
    cv::Mat coverage;
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

double MeanDeltaE76(const cv::Mat& a_bgr, const cv::Mat& b_bgr) {
    cv::Mat a32, b32;
    a_bgr.convertTo(a32, CV_32F, 1.0 / 255.0);
    b_bgr.convertTo(b32, CV_32F, 1.0 / 255.0);

    cv::Mat a_lab, b_lab;
    cv::cvtColor(a32, a_lab, cv::COLOR_BGR2Lab);
    cv::cvtColor(b32, b_lab, cv::COLOR_BGR2Lab);

    cv::Mat diff = a_lab - b_lab;
    std::vector<cv::Mat> ch(3);
    cv::split(diff, ch);
    cv::Mat de;
    cv::sqrt(ch[0].mul(ch[0]) + ch[1].mul(ch[1]) + ch[2].mul(ch[2]), de);
    return cv::mean(de)[0];
}

cv::Mat ExtractDarkMask(const cv::Mat& bgr, int threshold = 70) {
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    cv::Mat mask;
    cv::threshold(gray, mask, threshold, 255, cv::THRESH_BINARY_INV);
    return mask;
}

double MaskIoU(const cv::Mat& a, const cv::Mat& b) {
    if (a.empty() || b.empty() || a.size() != b.size()) return 0.0;
    cv::Mat i, u;
    cv::bitwise_and(a, b, i);
    cv::bitwise_or(a, b, u);
    int union_px = cv::countNonZero(u);
    if (union_px <= 0) return 0.0;
    return static_cast<double>(cv::countNonZero(i)) / static_cast<double>(union_px);
}

VectorizerConfig PotraceCfg() {
    VectorizerConfig cfg;
    cfg.num_colors          = 8;
    cfg.min_region_area     = 2;
    cfg.min_contour_area    = 1.0f;
    cfg.contour_simplify    = 0.85f;
    cfg.topology_cleanup    = 0.55f;
    cfg.enable_coverage_fix = true;
    cfg.min_coverage_ratio  = 0.995f;
    cfg.svg_enable_stroke   = false;
    return cfg;
}

} // namespace

TEST(VectorizerPotrace, BaselineMetricsStructuredImage) {
    cv::Mat img(96, 128, CV_8UC3, cv::Scalar(245, 245, 245));
    cv::rectangle(img, cv::Rect(6, 8, 40, 75), cv::Scalar(20, 30, 200), cv::FILLED);
    cv::circle(img, cv::Point(86, 34), 22, cv::Scalar(25, 180, 40), cv::FILLED);
    cv::ellipse(img, cv::Point(72, 72), cv::Size(30, 12), 20.0, 0.0, 360.0, cv::Scalar(200, 70, 30),
                cv::FILLED);
    cv::line(img, cv::Point(0, 95), cv::Point(127, 0), cv::Scalar(0, 0, 0), 2, cv::LINE_AA);

    VectorizerConfig cfg = PotraceCfg();
    auto out             = Vectorize(img, cfg);
    auto raster          = RasterizeSvg(out.svg_content, out.width, out.height);

    double coverage_ratio = static_cast<double>(cv::countNonZero(raster.coverage)) /
                            static_cast<double>(out.width * out.height);
    double mean_de76 = MeanDeltaE76(img, raster.bgr);
    int cubic_count =
        static_cast<int>(std::count(out.svg_content.begin(), out.svg_content.end(), 'C'));

    EXPECT_GT(coverage_ratio, 0.99);
    EXPECT_LT(mean_de76, 17.0);
    EXPECT_LT(cubic_count, 3200);
}

TEST(VectorizerPotrace, PreservesHoleTopologyForRing) {
    cv::Mat img(80, 80, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::circle(img, cv::Point(40, 40), 24, cv::Scalar(0, 0, 0), cv::FILLED, cv::LINE_8);
    cv::circle(img, cv::Point(40, 40), 11, cv::Scalar(255, 255, 255), cv::FILLED, cv::LINE_8);

    VectorizerConfig cfg = PotraceCfg();
    cfg.num_colors       = 2;
    auto out             = Vectorize(img, cfg);
    auto raster          = RasterizeSvg(out.svg_content, out.width, out.height);

    cv::Mat src_mask = ExtractDarkMask(img);
    cv::Mat out_mask = ExtractDarkMask(raster.bgr);
    double iou       = MaskIoU(src_mask, out_mask);

    cv::Vec3b center = raster.bgr.at<cv::Vec3b>(40, 40);
    int center_gray =
        (static_cast<int>(center[0]) + static_cast<int>(center[1]) + static_cast<int>(center[2])) /
        3;

    EXPECT_GT(iou, 0.74);
    EXPECT_GT(center_gray, 180);
}

TEST(VectorizerPotrace, PotracePipelineAvailable) {
    cv::Mat img(48, 48, CV_8UC3, cv::Scalar(255, 255, 255));
    cv::line(img, cv::Point(2, 6), cv::Point(45, 40), cv::Scalar(0, 0, 0), 2, cv::LINE_AA);

    VectorizerConfig cfg = PotraceCfg();
    auto out             = Vectorize(img, cfg);
    EXPECT_EQ(out.width, 48);
    EXPECT_EQ(out.height, 48);
    EXPECT_GT(out.num_shapes, 0);
    EXPECT_FALSE(out.svg_content.empty());
}
