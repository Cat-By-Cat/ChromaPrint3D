#include "imgproc/vectorize_potrace_pipeline.h"

#include "detail/cv_utils.h"
#include "imgproc/bezier.h"
#include "imgproc/boundary_graph.h"
#include "imgproc/contour_assembly.h"
#include "imgproc/coverage_guard.h"
#include "imgproc/curve_fitting.h"
#include "imgproc/potrace_adapter.h"
#include "imgproc/thin_line_vectorizer.h"
#include "imgproc/svg_writer.h"
#include "imgproc/vectorize_preprocess.h"
#include "match/slic_segmenter.h"

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

struct SegmentationResult {
    cv::Mat labels;
    cv::Mat lab;
    std::vector<cv::Vec3f> centers_lab;
};

SegmentationResult SegmentBinary(const cv::Mat& bgr, const cv::Mat& lab) {
    SegmentationResult out;
    out.lab = lab;

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
        if (counts[i] > 0) out.centers_lab[i] = sums[i] * (1.0f / static_cast<float>(counts[i]));
    }
    return out;
}

SegmentationResult SegmentMultiColor(const cv::Mat& lab, int num_colors) {
    SegmentationResult out;
    out.lab    = lab;
    out.labels = cv::Mat(lab.rows, lab.cols, CV_32SC1, cv::Scalar(0));

    SlicConfig slic_cfg;
    slic_cfg.region_size      = 20;
    slic_cfg.compactness      = 10.0f;
    slic_cfg.iterations       = 10;
    slic_cfg.min_region_ratio = 0.25f;

    auto slic  = SegmentBySlic(lab, cv::Mat(), slic_cfg);
    int num_sp = static_cast<int>(slic.centers.size());

    if (num_sp < num_colors) {
        cv::Mat samples = lab.reshape(1, lab.rows * lab.cols);
        samples.convertTo(samples, CV_32F);
        int K = std::clamp(num_colors, 2, std::max(2, samples.rows));

        cv::Mat km_labels, km_centers;
        cv::kmeans(samples, K, km_labels,
                   cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.2), 5,
                   cv::KMEANS_PP_CENTERS, km_centers);

        out.centers_lab.resize(K);
        for (int k = 0; k < K; ++k) {
            out.centers_lab[k] = {km_centers.at<float>(k, 0), km_centers.at<float>(k, 1),
                                  km_centers.at<float>(k, 2)};
        }
        out.labels = km_labels.reshape(1, lab.rows).clone();
        return out;
    }

    cv::Mat sp_samples(num_sp, 3, CV_32FC1);
    for (int i = 0; i < num_sp; ++i) {
        sp_samples.at<float>(i, 0) = slic.centers[i][0];
        sp_samples.at<float>(i, 1) = slic.centers[i][1];
        sp_samples.at<float>(i, 2) = slic.centers[i][2];
    }
    int K = std::clamp(num_colors, 2, num_sp);

    cv::Mat km_labels, km_centers;
    cv::kmeans(sp_samples, K, km_labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 30, 0.2), 5,
               cv::KMEANS_PP_CENTERS, km_centers);

    for (int r = 0; r < lab.rows; ++r) {
        const int* slic_row = slic.labels.ptr<int>(r);
        int* out_row        = out.labels.ptr<int>(r);
        for (int c = 0; c < lab.cols; ++c) {
            int sp_id = slic_row[c];
            if (sp_id >= 0 && sp_id < num_sp) {
                out_row[c] = km_labels.at<int>(sp_id, 0);
            } else {
                out_row[c] = -1;
            }
        }
    }

    out.centers_lab.resize(K);
    for (int k = 0; k < K; ++k) {
        out.centers_lab[k] = {km_centers.at<float>(k, 0), km_centers.at<float>(k, 1),
                              km_centers.at<float>(k, 2)};
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
    const bool multicolor = cfg.num_colors > 2;
    auto preproc          = PreprocessForVectorize(bgr, multicolor);
    cv::Mat working       = preproc.bgr;
    const float scale     = preproc.scale;

    cv::Mat working_mask = opaque_mask;
    if (scale > 1.0f && !opaque_mask.empty()) {
        cv::resize(opaque_mask, working_mask, working.size(), 0, 0, cv::INTER_NEAREST);
    }

    cv::Mat lab = BgrToLab(working);
    SegmentationResult seg =
        multicolor ? SegmentMultiColor(lab, cfg.num_colors) : SegmentBinary(working, lab);
    if (seg.labels.empty())
        throw std::runtime_error("VectorizePotracePipeline: segmentation failed");

    if (!working_mask.empty()) {
        if (working_mask.type() != CV_8UC1 || working_mask.size() != seg.labels.size()) {
            throw std::runtime_error("VectorizePotracePipeline: invalid opaque mask");
        }
        cv::Mat transparent;
        cv::compare(working_mask, 0, transparent, cv::CMP_EQ);
        seg.labels.setTo(cv::Scalar(-1), transparent);
    }

    MergeSmallComponents(seg.labels, seg.lab, seg.centers_lab, std::max(2, cfg.min_region_area));
    int num_labels = CompactLabels(seg.labels, seg.centers_lab);
    auto palette   = ComputePalette(working, seg.labels, num_labels);

    const float trace_eps =
        std::max(0.2f, std::clamp(cfg.contour_simplify * 0.45f + 0.2f, 0.2f, 2.0f));
    const int turdsize        = std::max(0, static_cast<int>(std::lround(trace_eps * 0.5f)));
    const double opttolerance = std::clamp(static_cast<double>(trace_eps), 0.2, 2.0);

    std::vector<VectorizedShape> shapes;

    if (multicolor && num_labels > 2) {
        auto boundary_graph = BuildBoundaryGraph(seg.labels);
        CurveFitConfig fit_cfg;
        shapes = AssembleContoursFromGraph(boundary_graph, num_labels, palette,
                                           cfg.min_contour_area, cfg.min_hole_area, &fit_cfg);

        // Potrace fallback for labels that BoundaryGraph failed to produce shapes for.
        // Determine covered labels by checking pixel coverage against shapes.
        std::vector<double> label_pixel_count(num_labels, 0.0);
        std::vector<double> label_shape_area(num_labels, 0.0);
        for (int r = 0; r < seg.labels.rows; ++r) {
            const int* lrow = seg.labels.ptr<int>(r);
            for (int c = 0; c < seg.labels.cols; ++c) {
                int lid = lrow[c];
                if (lid >= 0 && lid < num_labels) label_pixel_count[lid] += 1.0;
            }
        }
        for (const auto& s : shapes) {
            if (s.is_stroke) continue;
            for (int rid = 0; rid < num_labels; ++rid) {
                if (rid < static_cast<int>(palette.size())) {
                    uint8_t sr, sg, sb, pr, pg, pb;
                    s.color.ToRgb255(sr, sg, sb);
                    palette[rid].ToRgb255(pr, pg, pb);
                    if (sr == pr && sg == pg && sb == pb) { label_shape_area[rid] += s.area; }
                }
            }
        }
        std::vector<bool> label_covered(num_labels, false);
        for (int rid = 0; rid < num_labels; ++rid) {
            if (label_pixel_count[rid] < 1.0) {
                label_covered[rid] = true;
            } else if (label_shape_area[rid] > label_pixel_count[rid] * 0.1) {
                label_covered[rid] = true;
            }
        }
        for (int rid = 0; rid < num_labels; ++rid) {
            if (label_covered[rid]) continue;
            cv::Mat mask = (seg.labels == rid);
            mask.convertTo(mask, CV_8UC1, 255);
            if (cv::countNonZero(mask) <= 0) continue;

            auto traced = TraceMaskWithPotraceBezier(mask, turdsize, opttolerance);
            for (auto& g : traced) {
                if (g.area < static_cast<double>(cfg.min_contour_area)) continue;
                VectorizedShape shape;
                shape.color = palette[rid];
                shape.area  = g.area;
                shape.contours.push_back(std::move(g.outer));
                for (auto& hole : g.holes) {
                    double hole_area = std::abs(BezierContourSignedArea(hole));
                    if (hole_area < static_cast<double>(cfg.min_hole_area)) continue;
                    shape.contours.push_back(std::move(hole));
                }
                if (!shape.contours.empty()) shapes.push_back(std::move(shape));
            }
        }
    } else {
        for (int rid = 0; rid < num_labels; ++rid) {
            cv::Mat mask = (seg.labels == rid);
            mask.convertTo(mask, CV_8UC1, 255);
            int px = cv::countNonZero(mask);
            if (px <= 0) continue;

            auto traced = TraceMaskWithPotraceBezier(mask, turdsize, opttolerance);
            if (traced.empty()) {
                cv::Mat dilated;
                cv::dilate(mask, dilated,
                           cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3)));
                traced = TraceMaskWithPotraceBezier(dilated, std::max(0, turdsize - 1),
                                                    std::max(0.2, opttolerance * 0.8));
            }

            for (auto& g : traced) {
                if (g.area < static_cast<double>(cfg.min_contour_area)) continue;
                VectorizedShape shape;
                shape.color = palette[rid];
                shape.area  = g.area;
                shape.contours.push_back(std::move(g.outer));
                for (auto& hole : g.holes) {
                    double hole_area = std::abs(BezierContourSignedArea(hole));
                    if (hole_area < static_cast<double>(cfg.min_hole_area)) continue;
                    shape.contours.push_back(std::move(hole));
                }
                if (!shape.contours.empty()) shapes.push_back(std::move(shape));
            }
        }
    }

    // Thin-line enhancement: detect narrow sub-regions and add stroke paths
    if (cfg.svg_enable_stroke && multicolor && num_labels > 1) {
        for (int rid = 0; rid < num_labels; ++rid) {
            cv::Mat mask = (seg.labels == rid);
            mask.convertTo(mask, CV_8UC1, 255);
            if (cv::countNonZero(mask) <= 0) continue;

            cv::Mat thin = DetectThinRegion(mask, 2.5f);
            if (cv::countNonZero(thin) < 3) continue;

            cv::Mat dist;
            cv::distanceTransform(mask, dist, cv::DIST_L2, cv::DIST_MASK_PRECISE);
            cv::Mat skel = ZhangSuenThinning(thin);
            if (cv::countNonZero(skel) < 3) continue;

            auto strokes = ExtractStrokePaths(skel, dist, palette[rid], 3.0f);
            for (auto& s : strokes) shapes.push_back(std::move(s));
        }
    }

    std::sort(shapes.begin(), shapes.end(), [](const auto& a, const auto& b) {
        if (a.is_stroke != b.is_stroke) return !a.is_stroke;
        return a.area > b.area;
    });

    if (cfg.enable_coverage_fix && !(multicolor && num_labels > 2)) {
        ApplyCoverageGuard(shapes, seg.labels, palette, cfg.min_coverage_ratio, trace_eps,
                           std::max(1.0f, cfg.min_contour_area * 0.5f));
    }

    if (scale > 1.0f) {
        const float inv = 1.0f / scale;
        for (auto& shape : shapes) {
            for (auto& contour : shape.contours) {
                for (auto& s : contour.segments) {
                    s.p0 = s.p0 * inv;
                    s.p1 = s.p1 * inv;
                    s.p2 = s.p2 * inv;
                    s.p3 = s.p3 * inv;
                }
            }
        }
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
