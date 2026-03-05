#include "imgproc/thin_line_vectorizer.h"

#include "imgproc/curve_fitting.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <queue>
#include <vector>

namespace ChromaPrint3D::detail {

cv::Mat DetectThinRegion(const cv::Mat& mask, float max_radius) {
    if (mask.empty()) return {};
    cv::Mat dist;
    cv::distanceTransform(mask, dist, cv::DIST_L2, cv::DIST_MASK_PRECISE);
    cv::Mat thin;
    cv::compare(dist, max_radius, thin, cv::CMP_LE);
    cv::bitwise_and(thin, mask, thin);
    return thin;
}

cv::Mat ZhangSuenThinning(const cv::Mat& binary_mask) {
    cv::Mat img;
    binary_mask.convertTo(img, CV_8UC1);
    img /= 255;

    cv::Mat prev = cv::Mat::zeros(img.size(), CV_8UC1);
    cv::Mat marker;

    while (true) {
        marker = cv::Mat::zeros(img.size(), CV_8UC1);
        for (int r = 1; r < img.rows - 1; ++r) {
            for (int c = 1; c < img.cols - 1; ++c) {
                if (img.at<uint8_t>(r, c) == 0) continue;
                uint8_t p2 = img.at<uint8_t>(r - 1, c);
                uint8_t p3 = img.at<uint8_t>(r - 1, c + 1);
                uint8_t p4 = img.at<uint8_t>(r, c + 1);
                uint8_t p5 = img.at<uint8_t>(r + 1, c + 1);
                uint8_t p6 = img.at<uint8_t>(r + 1, c);
                uint8_t p7 = img.at<uint8_t>(r + 1, c - 1);
                uint8_t p8 = img.at<uint8_t>(r, c - 1);
                uint8_t p9 = img.at<uint8_t>(r - 1, c - 1);

                int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (B < 2 || B > 6) continue;

                int A = (p2 == 0 && p3 == 1) + (p3 == 0 && p4 == 1) + (p4 == 0 && p5 == 1) +
                        (p5 == 0 && p6 == 1) + (p6 == 0 && p7 == 1) + (p7 == 0 && p8 == 1) +
                        (p8 == 0 && p9 == 1) + (p9 == 0 && p2 == 1);
                if (A != 1) continue;

                if (p2 * p4 * p6 != 0) continue;
                if (p4 * p6 * p8 != 0) continue;
                marker.at<uint8_t>(r, c) = 1;
            }
        }
        img -= marker;

        marker = cv::Mat::zeros(img.size(), CV_8UC1);
        for (int r = 1; r < img.rows - 1; ++r) {
            for (int c = 1; c < img.cols - 1; ++c) {
                if (img.at<uint8_t>(r, c) == 0) continue;
                uint8_t p2 = img.at<uint8_t>(r - 1, c);
                uint8_t p3 = img.at<uint8_t>(r - 1, c + 1);
                uint8_t p4 = img.at<uint8_t>(r, c + 1);
                uint8_t p5 = img.at<uint8_t>(r + 1, c + 1);
                uint8_t p6 = img.at<uint8_t>(r + 1, c);
                uint8_t p7 = img.at<uint8_t>(r + 1, c - 1);
                uint8_t p8 = img.at<uint8_t>(r, c - 1);
                uint8_t p9 = img.at<uint8_t>(r - 1, c - 1);

                int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                if (B < 2 || B > 6) continue;

                int A = (p2 == 0 && p3 == 1) + (p3 == 0 && p4 == 1) + (p4 == 0 && p5 == 1) +
                        (p5 == 0 && p6 == 1) + (p6 == 0 && p7 == 1) + (p7 == 0 && p8 == 1) +
                        (p8 == 0 && p9 == 1) + (p9 == 0 && p2 == 1);
                if (A != 1) continue;

                if (p2 * p4 * p8 != 0) continue;
                if (p2 * p6 * p8 != 0) continue;
                marker.at<uint8_t>(r, c) = 1;
            }
        }
        img -= marker;

        if (cv::countNonZero(img != prev) == 0) break;
        img.copyTo(prev);
    }

    img *= 255;
    return img;
}

namespace {

constexpr int kDr8[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
constexpr int kDc8[8] = {0, 1, 1, 1, 0, -1, -1, -1};

int CountNeighbors(const cv::Mat& skel, int r, int c) {
    int count = 0;
    for (int k = 0; k < 8; ++k) {
        int nr = r + kDr8[k];
        int nc = c + kDc8[k];
        if (nr >= 0 && nr < skel.rows && nc >= 0 && nc < skel.cols &&
            skel.at<uint8_t>(nr, nc) != 0) {
            ++count;
        }
    }
    return count;
}

std::vector<Vec2f> TracePath(const cv::Mat& skel, cv::Mat& visited, int sr, int sc) {
    std::vector<Vec2f> path;
    path.push_back({static_cast<float>(sc), static_cast<float>(sr)});
    visited.at<uint8_t>(sr, sc) = 1;

    int cr = sr, cc = sc;
    while (true) {
        bool found = false;
        for (int k = 0; k < 8; ++k) {
            int nr = cr + kDr8[k];
            int nc = cc + kDc8[k];
            if (nr < 0 || nr >= skel.rows || nc < 0 || nc >= skel.cols) continue;
            if (skel.at<uint8_t>(nr, nc) == 0 || visited.at<uint8_t>(nr, nc) != 0) continue;
            visited.at<uint8_t>(nr, nc) = 1;
            path.push_back({static_cast<float>(nc), static_cast<float>(nr)});
            cr    = nr;
            cc    = nc;
            found = true;
            break;
        }
        if (!found) break;
    }
    return path;
}

} // namespace

std::vector<VectorizedShape> ExtractStrokePaths(const cv::Mat& skeleton, const cv::Mat& dist_map,
                                                const Rgb& color, float min_length) {
    std::vector<VectorizedShape> shapes;
    if (skeleton.empty()) return shapes;

    cv::Mat visited = cv::Mat::zeros(skeleton.size(), CV_8UC1);

    // Find endpoints (1 neighbor) and start tracing from them
    std::vector<cv::Point> endpoints;
    for (int r = 0; r < skeleton.rows; ++r) {
        for (int c = 0; c < skeleton.cols; ++c) {
            if (skeleton.at<uint8_t>(r, c) == 0) continue;
            if (CountNeighbors(skeleton, r, c) == 1) { endpoints.push_back({c, r}); }
        }
    }

    // Trace from endpoints first
    for (const auto& ep : endpoints) {
        if (visited.at<uint8_t>(ep.y, ep.x) != 0) continue;
        auto path = TracePath(skeleton, visited, ep.y, ep.x);
        if (static_cast<float>(path.size()) < min_length) continue;

        // Estimate stroke width from distance transform
        float total_width = 0.0f;
        int width_samples = 0;
        for (const auto& p : path) {
            int pr = static_cast<int>(p.y);
            int pc = static_cast<int>(p.x);
            if (pr >= 0 && pr < dist_map.rows && pc >= 0 && pc < dist_map.cols) {
                total_width += dist_map.at<float>(pr, pc) * 2.0f;
                ++width_samples;
            }
        }
        float avg_width =
            width_samples > 0 ? total_width / static_cast<float>(width_samples) : 1.0f;
        avg_width = std::max(0.5f, avg_width);

        CurveFitConfig fit_cfg;
        fit_cfg.error_threshold = 1.0f;
        auto beziers            = FitBezierToPolyline(path, fit_cfg);
        if (beziers.empty()) continue;

        BezierContour contour;
        contour.closed   = false;
        contour.segments = std::move(beziers);

        VectorizedShape shape;
        shape.color        = color;
        shape.is_stroke    = true;
        shape.stroke_width = avg_width;
        shape.area         = 0.0;
        shape.contours.push_back(std::move(contour));
        shapes.push_back(std::move(shape));
    }

    // Trace remaining (closed loops in skeleton)
    for (int r = 0; r < skeleton.rows; ++r) {
        for (int c = 0; c < skeleton.cols; ++c) {
            if (skeleton.at<uint8_t>(r, c) == 0 || visited.at<uint8_t>(r, c) != 0) continue;
            auto path = TracePath(skeleton, visited, r, c);
            if (static_cast<float>(path.size()) < min_length) continue;

            float total_width = 0.0f;
            int width_samples = 0;
            for (const auto& p : path) {
                int pr = static_cast<int>(p.y);
                int pc = static_cast<int>(p.x);
                if (pr >= 0 && pr < dist_map.rows && pc >= 0 && pc < dist_map.cols) {
                    total_width += dist_map.at<float>(pr, pc) * 2.0f;
                    ++width_samples;
                }
            }
            float avg_width =
                width_samples > 0 ? total_width / static_cast<float>(width_samples) : 1.0f;
            avg_width = std::max(0.5f, avg_width);

            CurveFitConfig fit_cfg;
            fit_cfg.error_threshold = 1.0f;
            auto beziers            = FitBezierToPolyline(path, fit_cfg);
            if (beziers.empty()) continue;

            BezierContour contour;
            contour.closed   = false;
            contour.segments = std::move(beziers);

            VectorizedShape shape;
            shape.color        = color;
            shape.is_stroke    = true;
            shape.stroke_width = avg_width;
            shape.area         = 0.0;
            shape.contours.push_back(std::move(contour));
            shapes.push_back(std::move(shape));
        }
    }

    return shapes;
}

} // namespace ChromaPrint3D::detail
