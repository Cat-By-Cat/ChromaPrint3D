#include "chromaprint3d/vectorizer.h"

#include "detail/cv_utils.h"
#include "detail/icc_utils.h"
#include "imgproc/curve_fit.h"
#include "imgproc/label_boundary_graph.h"
#include "imgproc/optimal_polygon.h"
#include "imgproc/region_merge.h"
#include "imgproc/svg_writer.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D {

namespace {

using detail::BezierContour;
using detail::VectorizedShape;

// ── Stage 1: Color quantization + region merging ────────────────────────────

struct SegmentationResult {
    cv::Mat labels; // CV_32SC1, per-pixel region id
    int num_regions;
    std::vector<Rgb> palette;
};

// Maximum pixel count for K-Means input; larger images are downsampled.
constexpr int kKMeansMaxPixels = 500 * 500;

SegmentationResult QuantizeAndMerge(const cv::Mat& bgr, const VectorizerConfig& cfg) {
    cv::Mat lab = detail::BgrToLab(bgr);

    // Downsample for K-Means if the image is large
    cv::Mat lab_small;
    double scale = 1.0;
    int total_px = bgr.rows * bgr.cols;
    if (total_px > kKMeansMaxPixels) {
        scale = std::sqrt(static_cast<double>(kKMeansMaxPixels) / total_px);
        cv::resize(lab, lab_small, cv::Size(), scale, scale, cv::INTER_AREA);
    } else {
        lab_small = lab;
    }

    cv::Mat samples = lab_small.reshape(1, lab_small.rows * lab_small.cols);
    samples.convertTo(samples, CV_32F);

    int K = std::min(cfg.num_colors, samples.rows);
    cv::Mat km_labels_small, centers;
    cv::kmeans(samples, K, km_labels_small,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 1.0), 3,
               cv::KMEANS_PP_CENTERS, centers);

    // Assign every full-resolution pixel to the nearest cluster center
    cv::Mat label_map(bgr.rows, bgr.cols, CV_32SC1);
    if (scale < 1.0) {
        // centers: K x 3 float
        for (int r = 0; r < bgr.rows; ++r) {
            const cv::Vec3f* lrow = lab.ptr<cv::Vec3f>(r);
            int* orow             = label_map.ptr<int>(r);
            for (int c = 0; c < bgr.cols; ++c) {
                float best_dist = std::numeric_limits<float>::max();
                int best_k      = 0;
                for (int k = 0; k < K; ++k) {
                    float dl = lrow[c][0] - centers.at<float>(k, 0);
                    float da = lrow[c][1] - centers.at<float>(k, 1);
                    float db = lrow[c][2] - centers.at<float>(k, 2);
                    float d  = dl * dl + da * da + db * db;
                    if (d < best_dist) {
                        best_dist = d;
                        best_k    = k;
                    }
                }
                orow[c] = best_k;
            }
        }
    } else {
        label_map = km_labels_small.reshape(1, bgr.rows);
    }

    // Global median filter on cluster labels to remove salt-and-pepper noise.
    // Replaces per-region morphological ops, which created inter-region gaps.
    if (cfg.morph_kernel_size > 0) {
        int ksize = cfg.morph_kernel_size | 1; // ensure odd
        cv::Mat label8;
        label_map.convertTo(label8, CV_8UC1);
        cv::medianBlur(label8, label8, ksize);
        label8.convertTo(label_map, CV_32SC1);
    }

    // Connected component labeling per cluster (parallelizable)
    // Collect per-cluster offsets first so we can parallelize CCL
    struct ClusterCCL {
        cv::Mat cc_labels;
        int n_cc = 0;
    };

    std::vector<ClusterCCL> cluster_ccls(K);

#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int k = 0; k < K; ++k) {
        cv::Mat mask = (label_map == k);
        mask.convertTo(mask, CV_8UC1, 255);
        cluster_ccls[k].n_cc = cv::connectedComponents(mask, cluster_ccls[k].cc_labels, 8, CV_32S);
    }

    // Sequential pass to assign globally unique region ids
    cv::Mat final_labels(bgr.rows, bgr.cols, CV_32SC1, cv::Scalar(0));
    int global_id = 0;
    for (int k = 0; k < K; ++k) {
        auto& ccl = cluster_ccls[k];
        for (int r = 0; r < bgr.rows; ++r) {
            const int* lrow  = label_map.ptr<int>(r);
            const int* ccrow = ccl.cc_labels.ptr<int>(r);
            int* frow        = final_labels.ptr<int>(r);
            for (int c = 0; c < bgr.cols; ++c) {
                if (lrow[c] == k && ccrow[c] > 0) { frow[c] = global_id + ccrow[c] - 1; }
            }
        }
        global_id += std::max(0, ccl.n_cc - 1);
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

// ── Stage 2-4: Shared-boundary extraction + fitting ─────────────────────────

struct RegionShapes {
    int region_id;
    double area;
    std::vector<BezierContour> contours;
};

BezierContour FitContour(const std::vector<Vec2f>& points, const VectorizerConfig& cfg) {
    if (points.size() < 4) {
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

    auto polygon = detail::FindOptimalPolygon(points);

    if (polygon.vertices.size() < 3) {
        auto corners = detail::DetectCorners(points, cfg.corner_threshold);
        return detail::FitSchneider(points, corners, cfg.curve_tolerance);
    }

    auto adjusted = detail::AdjustVertices(points, polygon);
    auto segments = detail::FitPotraceCurve(adjusted, cfg.alpha_max);

    if (cfg.enable_curve_opt) { segments = detail::OptimizeCurve(segments, cfg.opt_tolerance); }

    return detail::SegmentsToBezierContour(segments);
}

BezierContour PolylineToBezierContour(const std::vector<Vec2f>& points) {
    BezierContour out;
    out.closed = true;
    if (points.size() < 2) return out;
    out.segments.reserve(points.size());
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec2f& a = points[i];
        const Vec2f& b = points[(i + 1) % points.size()];
        Vec2f d        = b - a;
        if (d.LengthSquared() < 1e-8f) continue;
        out.segments.push_back({a, a + d * (1.0f / 3.0f), a + d * (2.0f / 3.0f), b});
    }
    return out;
}

std::vector<Vec2f> SimplifyLoop(const std::vector<Vec2f>& points, float epsilon) {
    if (points.size() < 4) return points;
    std::vector<cv::Point2f> in;
    in.reserve(points.size());
    for (const auto& p : points) in.push_back({p.x, p.y});

    std::vector<cv::Point2f> approx;
    cv::approxPolyDP(in, approx, std::max(0.1f, epsilon), true);
    if (approx.size() < 3) return points;

    std::vector<Vec2f> out;
    out.reserve(approx.size());
    for (const auto& p : approx) out.push_back({p.x, p.y});

    if (out.size() > 1) {
        const Vec2f& a = out.front();
        const Vec2f& b = out.back();
        if ((a - b).LengthSquared() < 1e-8f) out.pop_back();
    }
    return out;
}

float ComputeAdaptiveSimplifyEpsilon(const std::vector<Vec2f>& loop, const VectorizerConfig& cfg,
                                     int image_w, int image_h) {
    if (loop.size() < 3) return 0.06f;
    float diag       = std::hypot(static_cast<float>(image_w), static_cast<float>(image_h));
    float res_scale  = std::clamp(diag / 1024.0f, 0.35f, 1.8f);
    double perimeter = detail::LoopPerimeter(loop);
    float avg_step =
        static_cast<float>(perimeter / static_cast<double>(std::max<size_t>(1, loop.size())));
    float local_cap = std::clamp(avg_step * 0.35f, 0.06f, 0.9f);
    float epsilon   = cfg.curve_tolerance * 0.25f * res_scale;
    return std::clamp(std::min(epsilon, local_cap), 0.06f, 1.0f);
}

float ComputeAdaptiveDeviationThreshold(const VectorizerConfig& cfg, double perimeter,
                                        size_t point_count, int image_w, int image_h) {
    float diag      = std::hypot(static_cast<float>(image_w), static_cast<float>(image_h));
    float res_scale = std::clamp(diag / 1024.0f, 0.35f, 1.6f);
    float avg_step =
        static_cast<float>(perimeter / static_cast<double>(std::max<size_t>(1, point_count)));
    float geometric_cap = std::clamp(avg_step * 0.5f, 0.2f, 1.0f);
    float threshold     = cfg.curve_tolerance * 0.22f * res_scale;
    return std::clamp(std::min(threshold, geometric_cap), 0.2f, 1.0f);
}

double PolygonAreaAbs(const std::vector<Vec2f>& poly) {
    if (poly.size() < 3) return 0.0;
    double acc = 0.0;
    for (size_t i = 0, n = poly.size(); i < n; ++i) {
        const auto& a = poly[i];
        const auto& b = poly[(i + 1) % n];
        acc += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::abs(0.5 * acc);
}

std::vector<Vec2f> SampleBezierContour(const BezierContour& contour, int samples_per_seg) {
    std::vector<Vec2f> pts;
    if (contour.segments.empty()) return pts;
    samples_per_seg = std::max(2, samples_per_seg);
    pts.reserve(contour.segments.size() * samples_per_seg);
    for (const auto& seg : contour.segments) {
        for (int i = 0; i < samples_per_seg; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(samples_per_seg);
            pts.push_back(detail::EvalBezier(seg, t));
        }
    }
    return pts;
}

float PointSegmentDistSq(const Vec2f& p, const Vec2f& a, const Vec2f& b) {
    Vec2f ab  = b - a;
    float den = ab.LengthSquared();
    if (den < 1e-8f) return (p - a).LengthSquared();
    float t    = std::clamp((p - a).Dot(ab) / den, 0.0f, 1.0f);
    Vec2f proj = a + ab * t;
    return (p - proj).LengthSquared();
}

float MaxDeviationFromPolyline(const BezierContour& contour, const std::vector<Vec2f>& polyline) {
    if (contour.segments.empty() || polyline.size() < 2) return std::numeric_limits<float>::max();

    float max_dev_sq = 0.0f;
    auto eval_dev    = [&](const Vec2f& p) {
        float best_sq = std::numeric_limits<float>::max();
        for (size_t i = 0; i < polyline.size(); ++i) {
            const Vec2f& a = polyline[i];
            const Vec2f& b = polyline[(i + 1) % polyline.size()];
            best_sq        = std::min(best_sq, PointSegmentDistSq(p, a, b));
        }
        max_dev_sq = std::max(max_dev_sq, best_sq);
    };

    for (const auto& seg : contour.segments) {
        eval_dev(seg.p0);
        eval_dev(detail::EvalBezier(seg, 0.25f));
        eval_dev(detail::EvalBezier(seg, 0.5f));
        eval_dev(detail::EvalBezier(seg, 0.75f));
        eval_dev(seg.p3);
    }
    return std::sqrt(max_dev_sq);
}

void ClampContourToImage(BezierContour& contour, int image_w, int image_h) {
    auto clamp_point = [image_w, image_h](Vec2f& p) {
        p.x = std::clamp(p.x, 0.0f, static_cast<float>(image_w));
        p.y = std::clamp(p.y, 0.0f, static_cast<float>(image_h));
    };
    for (auto& seg : contour.segments) {
        clamp_point(seg.p0);
        clamp_point(seg.p1);
        clamp_point(seg.p2);
        clamp_point(seg.p3);
    }
}

RegionShapes ProcessRegionLoops(const detail::RegionBoundaryLoops& loops, int region_id,
                                const VectorizerConfig& cfg, int image_w, int image_h) {
    RegionShapes result;
    result.region_id = region_id;
    result.area      = 0;
    if (loops.loops.empty()) return result;

    for (const auto& loop : loops.loops) {
        double signed_area = detail::SignedLoopArea(loop);
        double area        = std::abs(signed_area);
        if (area < cfg.min_contour_area) continue;

        float simplify_epsilon = ComputeAdaptiveSimplifyEpsilon(loop, cfg, image_w, image_h);
        auto simplified        = SimplifyLoop(loop, simplify_epsilon);

        // Keep loops strictly inside the image domain to avoid viewBox clipping.
        std::vector<Vec2f> clamped;
        clamped.reserve(simplified.size());
        for (const auto& p : simplified) {
            float x = std::clamp(p.x, 0.0f, static_cast<float>(image_w));
            float y = std::clamp(p.y, 0.0f, static_cast<float>(image_h));
            clamped.push_back({x, y});
        }

        double perimeter = detail::LoopPerimeter(clamped);
        if (perimeter < cfg.min_boundary_perimeter) continue;

        result.area += area;
        BezierContour contour = FitContour(clamped, cfg);
        double fitted_area    = PolygonAreaAbs(SampleBezierContour(contour, 8));
        float max_dev         = MaxDeviationFromPolyline(contour, clamped);
        float allowed_dev =
            ComputeAdaptiveDeviationThreshold(cfg, perimeter, clamped.size(), image_w, image_h);
        if (fitted_area < area * 0.85 || fitted_area > area * 1.15 || max_dev > allowed_dev ||
            contour.segments.empty()) {
            contour = PolylineToBezierContour(clamped);
        }
        ClampContourToImage(contour, image_w, image_h);
        result.contours.push_back(std::move(contour));
    }

    return result;
}

} // namespace

// ── Public API ──────────────────────────────────────────────────────────────

VectorizerResult Vectorize(const std::string& image_path, const VectorizerConfig& config) {
    cv::Mat img = detail::LoadImageIcc(image_path);
    cv::Mat bgr = detail::EnsureBgr(img);
    if (bgr.empty()) { throw std::runtime_error("Failed to load image: " + image_path); }
    return Vectorize(bgr, config);
}

VectorizerResult Vectorize(const uint8_t* image_data, size_t image_size,
                           const VectorizerConfig& config) {
    cv::Mat img = detail::LoadImageIcc(image_data, image_size);
    cv::Mat bgr = detail::EnsureBgr(img);
    if (bgr.empty()) { throw std::runtime_error("Failed to decode image buffer"); }
    return Vectorize(bgr, config);
}

VectorizerResult Vectorize(const cv::Mat& bgr_image, const VectorizerConfig& config) {
    cv::Mat bgr = detail::EnsureBgr(bgr_image);
    if (bgr.empty()) { throw std::runtime_error("Empty input image"); }

    // Stage 1: Quantize + Merge
    auto seg = QuantizeAndMerge(bgr, config);

    // Stage 2: Extract shared boundaries once from the global label map.
    auto region_loops = detail::BuildRegionBoundaryLoops(seg.labels, seg.num_regions);

    // Stage 3-4: Per-region fitting from shared loops (parallelized).
    std::vector<RegionShapes> region_shapes(seg.num_regions);

#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int rid = 0; rid < seg.num_regions; ++rid) {
        region_shapes[rid] = ProcessRegionLoops(region_loops[rid], rid, config, bgr.cols, bgr.rows);
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
    result.svg_content = detail::WriteSvg(shapes, bgr.cols, bgr.rows, config.svg_enable_stroke,
                                          config.svg_stroke_width);

    return result;
}

} // namespace ChromaPrint3D
