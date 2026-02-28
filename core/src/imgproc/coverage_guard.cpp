#include "imgproc/coverage_guard.h"

#include "imgproc/bezier.h"
#include "imgproc/potrace_adapter.h"
#include "imgproc/topology_repair.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D::detail {

namespace {

BezierContour RingToBezier(const std::vector<Vec2f>& ring) {
    BezierContour contour;
    contour.closed = true;
    if (ring.size() < 3) return contour;
    contour.segments.reserve(ring.size());

    for (size_t i = 0; i < ring.size(); ++i) {
        const Vec2f& a = ring[i];
        const Vec2f& b = ring[(i + 1) % ring.size()];
        Vec2f d        = b - a;
        if (d.LengthSquared() < 1e-8f) continue;
        contour.segments.push_back({a, a + d * (1.0f / 3.0f), a + d * (2.0f / 3.0f), b});
    }
    return contour;
}

std::vector<cv::Point> FlattenContour(const BezierContour& contour, int width, int height) {
    std::vector<cv::Point> poly;
    if (contour.segments.empty()) return poly;
    std::vector<Vec2f> pts;
    pts.reserve(contour.segments.size() * 8 + 1);
    pts.push_back(contour.segments.front().p0);
    for (const auto& seg : contour.segments) FlattenCubicBezier(seg, 0.45f, pts);

    poly.reserve(pts.size());
    for (const auto& p : pts) {
        int x = std::clamp(static_cast<int>(std::lround(p.x)), 0, width - 1);
        int y = std::clamp(static_cast<int>(std::lround(p.y)), 0, height - 1);
        poly.emplace_back(x, y);
    }
    if (poly.size() > 1 && poly.front() == poly.back()) poly.pop_back();
    return poly;
}

cv::Mat RasterizeCoverage(const std::vector<VectorizedShape>& shapes, int width, int height) {
    cv::Mat coverage(height, width, CV_8UC1, cv::Scalar(0));
    for (const auto& shape : shapes) {
        std::vector<std::vector<cv::Point>> polys;
        for (const auto& contour : shape.contours) {
            auto poly = FlattenContour(contour, width, height);
            if (poly.size() >= 3) polys.push_back(std::move(poly));
        }
        if (!polys.empty()) cv::fillPoly(coverage, polys, cv::Scalar(255));
    }
    return coverage;
}

} // namespace

void ApplyCoverageGuard(std::vector<VectorizedShape>& shapes, const cv::Mat& labels,
                        const std::vector<Rgb>& palette, float min_ratio, float tracing_epsilon,
                        float min_patch_area) {
    if (labels.empty() || labels.type() != CV_32SC1) return;
    const int h = labels.rows;
    const int w = labels.cols;

    cv::Mat source_mask(h, w, CV_8UC1, cv::Scalar(0));
    for (int r = 0; r < h; ++r) {
        const int* row = labels.ptr<int>(r);
        uint8_t* out   = source_mask.ptr<uint8_t>(r);
        for (int c = 0; c < w; ++c) out[c] = (row[c] >= 0) ? 255 : 0;
    }

    cv::Mat coverage = RasterizeCoverage(shapes, w, h);
    cv::Mat covered;
    cv::bitwise_and(source_mask, coverage, covered);

    int source_px  = cv::countNonZero(source_mask);
    int covered_px = cv::countNonZero(covered);
    if (source_px <= 0) return;

    float ratio = static_cast<float>(covered_px) / static_cast<float>(source_px);
    if (ratio >= min_ratio) return;

    cv::Mat missing;
    cv::bitwise_not(coverage, missing);
    cv::bitwise_and(missing, source_mask, missing);

    cv::Mat cc_labels;
    int ncc = cv::connectedComponents(missing, cc_labels, 8, CV_32S);
    if (ncc <= 1) return;

    for (int cid = 1; cid < ncc; ++cid) {
        cv::Mat comp_mask(h, w, CV_8UC1, cv::Scalar(0));
        std::unordered_map<int, int> label_hist;
        int area = 0;

        for (int r = 0; r < h; ++r) {
            const int* cc_row = cc_labels.ptr<int>(r);
            const int* lb_row = labels.ptr<int>(r);
            uint8_t* out      = comp_mask.ptr<uint8_t>(r);
            for (int c = 0; c < w; ++c) {
                if (cc_row[c] != cid) continue;
                out[c] = 255;
                ++area;
                label_hist[lb_row[c]]++;
            }
        }

        if (area < static_cast<int>(std::max(1.0f, min_patch_area))) continue;
        if (label_hist.empty()) continue;

        int best_label = -1;
        int best_count = -1;
        for (const auto& kv : label_hist) {
            if (kv.second > best_count) {
                best_count = kv.second;
                best_label = kv.first;
            }
        }
        if (best_label < 0 || best_label >= static_cast<int>(palette.size())) continue;

        auto traced = TraceMaskWithPotrace(comp_mask, tracing_epsilon * 0.8f);
        auto fixed = RepairTopology(traced, tracing_epsilon * 0.6f, min_patch_area, min_patch_area);

        for (auto& g : fixed) {
            VectorizedShape patch;
            patch.color = palette[best_label];
            patch.area  = g.area;
            patch.contours.push_back(RingToBezier(g.outer));
            for (const auto& hole : g.holes) patch.contours.push_back(RingToBezier(hole));
            if (!patch.contours.empty()) shapes.push_back(std::move(patch));
        }
    }
}

} // namespace ChromaPrint3D::detail
