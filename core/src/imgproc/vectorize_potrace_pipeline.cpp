#include "imgproc/vectorize_potrace_pipeline.h"

#include "detail/cv_utils.h"
#include "imgproc/coverage_guard.h"
#include "imgproc/potrace_adapter.h"
#include "imgproc/svg_writer.h"
#include "imgproc/topology_repair.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D::detail {

namespace {

constexpr int kMaxKMeansPixels = 900 * 900;

struct SegmentationResult {
    cv::Mat labels; // CV_32SC1
    std::vector<cv::Vec3f> centers_lab;
};

std::vector<Vec2f> SimplifyRing(const std::vector<Vec2f>& ring, float epsilon) {
    if (ring.size() < 4) return ring;

    std::vector<cv::Point2f> in;
    in.reserve(ring.size());
    for (const auto& p : ring) in.push_back({p.x, p.y});

    std::vector<cv::Point2f> out_cv;
    cv::approxPolyDP(in, out_cv, std::max(0.2f, epsilon), true);
    if (out_cv.size() < 3) return ring;

    std::vector<Vec2f> out;
    out.reserve(out_cv.size());
    for (const auto& p : out_cv) out.push_back({p.x, p.y});
    if (out.size() > 1 && (out.front() - out.back()).LengthSquared() < 1e-8f) out.pop_back();
    return out;
}

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

SegmentationResult SegmentByKMeans(const cv::Mat& bgr, int num_colors) {
    cv::Mat lab = BgrToLab(bgr);
    SegmentationResult out;
    out.labels = cv::Mat(bgr.rows, bgr.cols, CV_32SC1, cv::Scalar(0));
    if (lab.empty()) return out;

    // For binary-like inputs (line art / logos), Otsu is more robust than K-Means
    // and avoids dropping tiny dark structures.
    if (num_colors <= 2) {
        cv::Mat gray, bw;
        cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
        cv::threshold(gray, bw, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);

        out.labels = cv::Mat(bgr.rows, bgr.cols, CV_32SC1);
        out.centers_lab.resize(2, cv::Vec3f(0, 0, 0));
        std::array<cv::Vec3f, 2> sums{cv::Vec3f(0, 0, 0), cv::Vec3f(0, 0, 0)};
        std::array<int, 2> counts{0, 0};

        for (int r = 0; r < bgr.rows; ++r) {
            const uint8_t* bw_row    = bw.ptr<uint8_t>(r);
            const cv::Vec3f* lab_row = lab.ptr<cv::Vec3f>(r);
            int* out_row             = out.labels.ptr<int>(r);
            for (int c = 0; c < bgr.cols; ++c) {
                int lid    = (bw_row[c] > 0) ? 1 : 0;
                out_row[c] = lid;
                sums[lid] += lab_row[c];
                counts[lid]++;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (counts[i] > 0)
                out.centers_lab[i] = sums[i] * (1.0f / static_cast<float>(counts[i]));
        }
        return out;
    }

    cv::Mat km_src = lab;
    double scale   = 1.0;
    int total_px   = bgr.rows * bgr.cols;
    if (total_px > kMaxKMeansPixels) {
        scale = std::sqrt(static_cast<double>(kMaxKMeansPixels) / static_cast<double>(total_px));
        cv::resize(lab, km_src, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    cv::Mat samples = km_src.reshape(1, km_src.rows * km_src.cols);
    samples.convertTo(samples, CV_32F);
    int K = std::clamp(num_colors, 2, std::max(2, samples.rows));

    cv::Mat km_labels, centers;
    cv::kmeans(samples, K, km_labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.2), 5,
               cv::KMEANS_PP_CENTERS, centers);

    out.centers_lab.resize(K);
    for (int k = 0; k < K; ++k) {
        out.centers_lab[k] = {centers.at<float>(k, 0), centers.at<float>(k, 1),
                              centers.at<float>(k, 2)};
    }

    if (scale >= 1.0) {
        out.labels = km_labels.reshape(1, bgr.rows).clone();
        return out;
    }

    // Assign full-resolution pixels to nearest center.
    for (int r = 0; r < bgr.rows; ++r) {
        const cv::Vec3f* lab_row = lab.ptr<cv::Vec3f>(r);
        int* lbl_row             = out.labels.ptr<int>(r);
        for (int c = 0; c < bgr.cols; ++c) {
            float best_dist = std::numeric_limits<float>::max();
            int best_k      = 0;
            for (int k = 0; k < K; ++k) {
                float dl = lab_row[c][0] - out.centers_lab[k][0];
                float da = lab_row[c][1] - out.centers_lab[k][1];
                float db = lab_row[c][2] - out.centers_lab[k][2];
                float d2 = dl * dl + da * da + db * db;
                if (d2 < best_dist) {
                    best_dist = d2;
                    best_k    = k;
                }
            }
            lbl_row[c] = best_k;
        }
    }

    return out;
}

void MergeSmallComponents(cv::Mat& labels, const cv::Mat& lab,
                          const std::vector<cv::Vec3f>& centers_lab, int min_region_area) {
    if (min_region_area <= 1 || labels.empty()) return;

    const int h = labels.rows;
    const int w = labels.cols;
    cv::Mat visited(h, w, CV_8UC1, cv::Scalar(0));
    std::queue<cv::Point> q;
    std::vector<cv::Point> component;
    component.reserve(1024);

    constexpr int dr[4] = {1, -1, 0, 0};
    constexpr int dc[4] = {0, 0, 1, -1};

    for (int sr = 0; sr < h; ++sr) {
        for (int sc = 0; sc < w; ++sc) {
            if (visited.at<uint8_t>(sr, sc) != 0) continue;

            const int label0            = labels.at<int>(sr, sc);
            visited.at<uint8_t>(sr, sc) = 1;
            if (label0 < 0) continue;
            q.push({sc, sr});
            component.clear();
            component.push_back({sc, sr});

            std::unordered_map<int, int> border_hist;
            cv::Vec3f mean_lab(0, 0, 0);

            while (!q.empty()) {
                cv::Point p = q.front();
                q.pop();
                mean_lab += lab.at<cv::Vec3f>(p.y, p.x);

                for (int k = 0; k < 4; ++k) {
                    int nr = p.y + dr[k];
                    int nc = p.x + dc[k];
                    if (nr < 0 || nr >= h || nc < 0 || nc >= w) continue;
                    int nl = labels.at<int>(nr, nc);
                    if (nl == label0) {
                        if (visited.at<uint8_t>(nr, nc) == 0) {
                            visited.at<uint8_t>(nr, nc) = 1;
                            q.push({nc, nr});
                            component.push_back({nc, nr});
                        }
                    } else if (nl >= 0) {
                        border_hist[nl]++;
                    }
                }
            }

            if (static_cast<int>(component.size()) >= min_region_area || border_hist.empty())
                continue;
            mean_lab *= (1.0f / static_cast<float>(component.size()));

            int best_label       = label0;
            int best_border_vote = -1;
            float best_dist      = std::numeric_limits<float>::max();
            for (const auto& [candidate, vote] : border_hist) {
                if (candidate < 0 || candidate >= static_cast<int>(centers_lab.size())) continue;
                float dl = mean_lab[0] - centers_lab[candidate][0];
                float da = mean_lab[1] - centers_lab[candidate][1];
                float db = mean_lab[2] - centers_lab[candidate][2];
                float d2 = dl * dl + da * da + db * db;
                if (vote > best_border_vote || (vote == best_border_vote && d2 < best_dist)) {
                    best_border_vote = vote;
                    best_dist        = d2;
                    best_label       = candidate;
                }
            }

            if (best_label == label0) continue;
            for (const auto& p : component) labels.at<int>(p.y, p.x) = best_label;
        }
    }
}

int CompactLabels(cv::Mat& labels, std::vector<cv::Vec3f>& centers_lab) {
    int max_label = static_cast<int>(centers_lab.size());
    if (labels.empty() || max_label <= 0) return 0;

    std::vector<int> remap(max_label, -1);
    int next = 0;
    for (int r = 0; r < labels.rows; ++r) {
        int* row = labels.ptr<int>(r);
        for (int c = 0; c < labels.cols; ++c) {
            int lid = row[c];
            if (lid < 0 || lid >= max_label) continue;
            if (remap[lid] < 0) remap[lid] = next++;
            row[c] = remap[lid];
        }
    }

    std::vector<cv::Vec3f> compact(next, cv::Vec3f(0, 0, 0));
    for (int i = 0; i < max_label; ++i) {
        if (remap[i] >= 0) compact[remap[i]] = centers_lab[i];
    }
    centers_lab.swap(compact);
    return next;
}

std::vector<Rgb> ComputePalette(const cv::Mat& bgr, const cv::Mat& labels, int num_labels) {
    std::vector<Rgb> palette(std::max(0, num_labels), Rgb(0, 0, 0));
    if (num_labels <= 0) return palette;

    std::vector<std::array<double, 3>> sums(num_labels, {0.0, 0.0, 0.0});
    std::vector<int> counts(num_labels, 0);

    for (int r = 0; r < bgr.rows; ++r) {
        const cv::Vec3b* brow = bgr.ptr<cv::Vec3b>(r);
        const int* lrow       = labels.ptr<int>(r);
        for (int c = 0; c < bgr.cols; ++c) {
            int lid = lrow[c];
            if (lid < 0 || lid >= num_labels) continue;
            sums[lid][0] += brow[c][2] / 255.0; // R
            sums[lid][1] += brow[c][1] / 255.0; // G
            sums[lid][2] += brow[c][0] / 255.0; // B
            counts[lid]++;
        }
    }

    for (int i = 0; i < num_labels; ++i) {
        if (counts[i] <= 0) continue;
        float r    = static_cast<float>(sums[i][0] / counts[i]);
        float g    = static_cast<float>(sums[i][1] / counts[i]);
        float b    = static_cast<float>(sums[i][2] / counts[i]);
        palette[i] = Rgb(SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b));
    }
    return palette;
}

} // namespace

VectorizerResult VectorizePotracePipeline(const cv::Mat& bgr, const VectorizerConfig& cfg,
                                          const cv::Mat& opaque_mask) {
    // Keep edge pixels untouched; denoising here can erase one-pixel strokes.
    cv::Mat denoised = bgr.clone();

    auto seg = SegmentByKMeans(denoised, cfg.num_colors);
    if (seg.labels.empty())
        throw std::runtime_error("VectorizePotracePipeline: segmentation failed");

    if (!opaque_mask.empty()) {
        if (opaque_mask.type() != CV_8UC1 || opaque_mask.size() != seg.labels.size()) {
            throw std::runtime_error("VectorizePotracePipeline: invalid opaque mask");
        }
        cv::Mat transparent;
        cv::compare(opaque_mask, 0, transparent, cv::CMP_EQ);
        seg.labels.setTo(cv::Scalar(-1), transparent);
    }

    cv::Mat lab = BgrToLab(denoised);
    MergeSmallComponents(seg.labels, lab, seg.centers_lab, std::max(2, cfg.min_region_area));
    int num_labels = CompactLabels(seg.labels, seg.centers_lab);
    auto palette   = ComputePalette(bgr, seg.labels, num_labels);

    const float trace_eps =
        std::max(0.2f, std::clamp(cfg.contour_simplify * 0.45f + 0.2f, 0.2f, 2.0f));
    const float ring_eps_base = std::clamp(cfg.contour_simplify * 0.22f + 0.06f, 0.08f, 0.55f);

    std::vector<VectorizedShape> shapes;
    for (int rid = 0; rid < num_labels; ++rid) {
        cv::Mat mask = (seg.labels == rid);
        mask.convertTo(mask, CV_8UC1, 255);
        int px = cv::countNonZero(mask);
        if (px <= 0) continue;

        auto traced = TraceMaskWithPotrace(mask, trace_eps);
        auto fixed =
            RepairTopology(traced, cfg.topology_cleanup, std::max(1.0f, cfg.min_contour_area),
                           std::max(0.5f, cfg.min_hole_area));
        if (fixed.empty()) {
            // Potrace can drop ultra-thin strokes; retry with a 1px dilation.
            cv::Mat dilated;
            cv::dilate(mask, dilated, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
            auto traced_retry = TraceMaskWithPotrace(dilated, std::max(0.2f, trace_eps * 0.8f));
            fixed = RepairTopology(traced_retry, std::max(0.08f, cfg.topology_cleanup * 0.8f),
                                   std::max(0.5f, cfg.min_contour_area * 0.5f),
                                   std::max(0.25f, cfg.min_hole_area * 0.8f));
        }

        for (auto& g : fixed) {
            float local_cap = std::clamp(
                static_cast<float>(std::sqrt(std::max(1.0, g.area)) * 0.04), 0.08f, 0.5f);
            float ring_eps = std::min(ring_eps_base, local_cap);
            auto outer     = SimplifyRing(g.outer, ring_eps);
            if (outer.size() < 3) continue;
            if (std::abs(SignedArea(outer)) < static_cast<double>(cfg.min_contour_area)) continue;

            VectorizedShape shape;
            shape.color = palette[rid];
            shape.area  = std::abs(SignedArea(outer));
            shape.contours.push_back(RingToBezier(outer));

            for (const auto& hole_raw : g.holes) {
                auto hole = SimplifyRing(hole_raw, ring_eps * 0.8f);
                if (hole.size() < 3) continue;
                if (std::abs(SignedArea(hole)) < static_cast<double>(cfg.min_hole_area)) continue;
                shape.contours.push_back(RingToBezier(hole));
            }
            if (!shape.contours.empty()) shapes.push_back(std::move(shape));
        }
    }

    std::sort(shapes.begin(), shapes.end(),
              [](const auto& a, const auto& b) { return a.area > b.area; });

    if (cfg.enable_coverage_fix) {
        ApplyCoverageGuard(shapes, seg.labels, palette, cfg.min_coverage_ratio, trace_eps,
                           std::max(1.0f, cfg.min_contour_area * 0.5f));
    }

    VectorizerResult result;
    result.width      = bgr.cols;
    result.height     = bgr.rows;
    result.num_shapes = static_cast<int>(shapes.size());
    result.palette    = std::move(palette);
    result.svg_content =
        WriteSvg(shapes, bgr.cols, bgr.rows, cfg.svg_enable_stroke, cfg.svg_stroke_width);
    return result;
}

} // namespace ChromaPrint3D::detail
