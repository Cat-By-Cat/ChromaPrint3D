#include "chromaprint3d/vectorizer.h"

#include "detail/cv_utils.h"
#include "imgproc/curve_fit.h"
#include "imgproc/optimal_polygon.h"
#include "imgproc/region_merge.h"
#include "imgproc/svg_writer.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D {

namespace {

using detail::BezierContour;
using detail::CurveSegment;
using detail::VectorizedShape;

// ── Stage 1: Color quantization + region merging ────────────────────────────

struct SegmentationResult {
    cv::Mat labels; // CV_32SC1, per-pixel region id
    int num_regions;
    std::vector<Rgb> palette;
};

SegmentationResult QuantizeAndMerge(const cv::Mat& bgr, const VectorizerConfig& cfg) {
    // Convert to Lab
    cv::Mat lab = detail::BgrToLab(bgr);

    // Reshape for K-Means: (N, 3) float
    cv::Mat samples = lab.reshape(1, lab.rows * lab.cols);
    samples.convertTo(samples, CV_32F);

    // K-Means clustering
    int K = std::min(cfg.num_colors, samples.rows);
    cv::Mat km_labels, centers;
    cv::kmeans(samples, K, km_labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 1.0), 3,
               cv::KMEANS_PP_CENTERS, centers);

    // Build per-pixel label map
    cv::Mat label_map = km_labels.reshape(1, bgr.rows); // CV_32SC1

    // Connected component labeling: subdivide each color cluster into
    // spatially connected regions
    cv::Mat final_labels(bgr.rows, bgr.cols, CV_32SC1);
    int global_id = 0;

    for (int k = 0; k < K; ++k) {
        cv::Mat mask = (label_map == k);
        mask.convertTo(mask, CV_8UC1, 255);

        cv::Mat cc_labels;
        int n_cc = cv::connectedComponents(mask, cc_labels, 8, CV_32S);

        for (int r = 0; r < bgr.rows; ++r) {
            const int* lrow  = label_map.ptr<int>(r);
            const int* ccrow = cc_labels.ptr<int>(r);
            int* frow        = final_labels.ptr<int>(r);
            for (int c = 0; c < bgr.cols; ++c) {
                if (lrow[c] == k && ccrow[c] > 0) { frow[c] = global_id + ccrow[c] - 1; }
            }
        }
        global_id += std::max(0, n_cc - 1);
    }

    // Region merging
    int num_regions =
        detail::MergeRegions(final_labels, lab, cfg.merge_lambda, cfg.min_region_area);

    // Compute palette: average color per region
    std::unordered_map<int, std::array<double, 3>> color_sums;
    std::unordered_map<int, int> region_areas;

    for (int r = 0; r < bgr.rows; ++r) {
        const int* lrow       = final_labels.ptr<int>(r);
        const cv::Vec3b* brow = bgr.ptr<cv::Vec3b>(r);
        for (int c = 0; c < bgr.cols; ++c) {
            int lid  = lrow[c];
            auto& cs = color_sums[lid];
            cs[0] += brow[c][2] / 255.0; // R
            cs[1] += brow[c][1] / 255.0; // G
            cs[2] += brow[c][0] / 255.0; // B
            region_areas[lid]++;
        }
    }

    std::vector<Rgb> palette(num_regions);
    for (auto& [id, cs] : color_sums) {
        int area = region_areas[id];
        if (id < num_regions && area > 0) {
            float r = static_cast<float>(cs[0] / area);
            float g = static_cast<float>(cs[1] / area);
            float b = static_cast<float>(cs[2] / area);
            // Convert from sRGB gamma to linear
            palette[id] = Rgb(SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b));
        }
    }

    return {final_labels, num_regions, palette};
}

// ── Stage 2-4: Per-region contour extraction + fitting ──────────────────────

struct RegionShapes {
    int region_id;
    double area;
    std::vector<BezierContour> contours;
};

RegionShapes ProcessRegion(const cv::Mat& labels, int region_id, const VectorizerConfig& cfg) {
    RegionShapes result;
    result.region_id = region_id;
    result.area      = 0;

    // Stage 2: Binary mask for this region
    cv::Mat mask = (labels == region_id);
    mask.convertTo(mask, CV_8UC1, 255);

    // Morphological cleanup
    if (cfg.morph_kernel_size > 0) {
        cv::Mat kernel = cv::getStructuringElement(
            cv::MORPH_ELLIPSE, cv::Size(cfg.morph_kernel_size, cfg.morph_kernel_size));
        cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
        cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);
    }

    // Find contours with hierarchy (RETR_CCOMP for 2-level hierarchy)
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_CCOMP, cv::CHAIN_APPROX_NONE);

    if (contours.empty()) return result;

    // Process each top-level contour (outer boundary)
    for (int idx = 0; idx >= 0; idx = hierarchy[idx][0]) {
        double contour_area = std::abs(cv::contourArea(contours[idx]));
        if (contour_area < cfg.min_contour_area) continue;

        result.area += contour_area;

        // Convert cv::Point contour to Vec2f
        auto to_vec2f = [](const std::vector<cv::Point>& pts) {
            std::vector<Vec2f> out;
            out.reserve(pts.size());
            for (auto& p : pts) out.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});
            return out;
        };

        auto fit_contour = [&](const std::vector<Vec2f>& points) -> BezierContour {
            if (points.size() < 4) {
                // Too few points — create degenerate bezier
                BezierContour bc;
                if (points.size() >= 2) {
                    for (size_t i = 0; i + 1 < points.size(); ++i) {
                        auto& a  = points[i];
                        auto& b  = points[i + 1];
                        Vec2f m1 = a + (b - a) * (1.0f / 3.0f);
                        Vec2f m2 = a + (b - a) * (2.0f / 3.0f);
                        bc.segments.push_back({a, m1, m2, b});
                    }
                }
                bc.closed = true;
                return bc;
            }

            // Potrace pipeline: optimal polygon -> vertex adjustment -> alpha curve fit
            auto polygon = detail::FindOptimalPolygon(points);

            if (polygon.vertices.size() < 3) {
                // Fallback to Schneider
                auto corners = detail::DetectCorners(points, cfg.corner_threshold);
                return detail::FitSchneider(points, corners, cfg.curve_tolerance);
            }

            auto adjusted = detail::AdjustVertices(points, polygon);
            auto segments = detail::FitPotraceCurve(adjusted, cfg.alpha_max);

            if (cfg.enable_curve_opt) {
                segments = detail::OptimizeCurve(segments, cfg.opt_tolerance);
            }

            return detail::SegmentsToBezierContour(segments);
        };

        auto pts            = to_vec2f(contours[idx]);
        BezierContour outer = fit_contour(pts);
        result.contours.push_back(std::move(outer));

        // Process holes (children of this contour)
        for (int child = hierarchy[idx][2]; child >= 0; child = hierarchy[child][0]) {
            double hole_area = std::abs(cv::contourArea(contours[child]));
            if (hole_area < cfg.min_contour_area) continue;

            auto hpts          = to_vec2f(contours[child]);
            BezierContour hole = fit_contour(hpts);
            result.contours.push_back(std::move(hole));
        }
    }

    return result;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

VectorizerResult Vectorize(const std::string& image_path, const VectorizerConfig& config) {
    cv::Mat bgr = cv::imread(image_path, cv::IMREAD_COLOR);
    if (bgr.empty()) { throw std::runtime_error("Failed to load image: " + image_path); }
    return Vectorize(bgr, config);
}

VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config) {
    cv::Mat bgr = detail::EnsureBgr(bgr_image);
    if (bgr.empty()) { throw std::runtime_error("Empty input image"); }

    // Stage 1: Quantize + Merge
    auto seg = QuantizeAndMerge(bgr, config);

    // Stage 2-4: Per-region processing (parallelized)
    std::vector<RegionShapes> region_shapes(seg.num_regions);

#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int rid = 0; rid < seg.num_regions; ++rid) {
        region_shapes[rid] = ProcessRegion(seg.labels, rid, config);
    }

    // Stage 5: Assemble shapes and sort by area (large first = background)
    std::vector<detail::VectorizedShape> shapes;
    for (auto& rs : region_shapes) {
        if (rs.contours.empty()) continue;

        detail::VectorizedShape shape;
        shape.contours = std::move(rs.contours);
        shape.color    = (rs.region_id < static_cast<int>(seg.palette.size()))
                             ? seg.palette[rs.region_id]
                             : Rgb(0, 0, 0);
        shape.area     = rs.area;
        shapes.push_back(std::move(shape));
    }

    // Sort by area descending (painter's algorithm: background first)
    std::sort(shapes.begin(), shapes.end(),
              [](const auto& a, const auto& b) { return a.area > b.area; });

    // Generate SVG
    VectorizerResult result;
    result.width       = bgr.cols;
    result.height      = bgr.rows;
    result.num_shapes  = static_cast<int>(shapes.size());
    result.palette     = seg.palette;
    result.svg_content = detail::WriteSvg(shapes, bgr.cols, bgr.rows);

    return result;
}

} // namespace ChromaPrint3D
