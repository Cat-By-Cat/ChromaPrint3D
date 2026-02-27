#include "curve_fit.h"
#include "math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ChromaPrint3D::detail {

namespace {

// ── Schneider helpers ───────────────────────────────────────────────────────

Vec2f EstimateTangent(const std::vector<Vec2f>& pts, int idx, int window, bool left) {
    int n = static_cast<int>(pts.size());
    Vec2f sum{0, 0};
    int count = 0;
    for (int i = 1; i <= window && i < n; ++i) {
        int other = left ? idx - i : idx + i;
        if (other < 0 || other >= n) break;
        Vec2f d = pts[other] - pts[idx];
        if (left) d = d * -1.0f;
        float len = d.Length();
        if (len > 1e-9f) {
            sum = sum + d * (1.0f / len);
            count++;
        }
    }
    return count > 0 ? sum.Normalized() : Vec2f{1, 0};
}

std::vector<float> ChordLengthParameterize(const std::vector<Vec2f>& pts, int first, int last) {
    int n = last - first + 1;
    std::vector<float> u(n, 0.0f);
    for (int i = 1; i < n; ++i) {
        u[i] = u[i - 1] + (pts[first + i] - pts[first + i - 1]).Length();
    }
    if (u.back() > 1e-9f) {
        float inv = 1.0f / u.back();
        for (auto& v : u) v *= inv;
    }
    return u;
}

CubicBezier FitSingleBezier(const std::vector<Vec2f>& pts, int first, int last,
                            const std::vector<float>& u, Vec2f tHat1, Vec2f tHat2) {
    int n = last - first + 1;
    CubicBezier bez;
    bez.p0 = pts[first];
    bez.p3 = pts[last];

    if (n == 2) {
        float dist = (pts[last] - pts[first]).Length() / 3.0f;
        bez.p1     = pts[first] + tHat1 * dist;
        bez.p2     = pts[last] + tHat2 * dist;
        return bez;
    }

    float c11 = 0, c12 = 0, c22 = 0;
    float x1 = 0, x2 = 0;

    for (int i = 0; i < n; ++i) {
        float t  = u[i];
        float s  = 1.0f - t;
        float b1 = 3.0f * s * s * t;
        float b2 = 3.0f * s * t * t;

        Vec2f a1 = tHat1 * b1;
        Vec2f a2 = tHat2 * b2;

        c11 += a1.Dot(a1);
        c12 += a1.Dot(a2);
        c22 += a2.Dot(a2);

        Vec2f baseP = bez.p0 * (s * s * s) + bez.p3 * (t * t * t);
        Vec2f diff  = pts[first + i] - baseP;

        x1 += a1.Dot(diff);
        x2 += a2.Dot(diff);
    }

    float det = c11 * c22 - c12 * c12;
    float alpha1, alpha2;
    if (std::abs(det) < 1e-12f) {
        float dist = (pts[last] - pts[first]).Length() / 3.0f;
        alpha1     = dist;
        alpha2     = dist;
    } else {
        alpha1 = (c22 * x1 - c12 * x2) / det;
        alpha2 = (c11 * x2 - c12 * x1) / det;
    }

    float segLen  = (pts[last] - pts[first]).Length();
    float epsilon = 1e-6f * segLen;

    if (alpha1 < epsilon || alpha2 > -epsilon) {
        if (alpha1 < epsilon) alpha1 = segLen / 3.0f;
        if (alpha2 > -epsilon) alpha2 = -segLen / 3.0f;
    }

    bez.p1 = bez.p0 + tHat1 * alpha1;
    bez.p2 = bez.p3 + tHat2 * alpha2;

    return bez;
}

float MaxError(const std::vector<Vec2f>& pts, int first, int last, const CubicBezier& bez,
               const std::vector<float>& u, int& split_point) {
    float max_dist = 0.0f;
    split_point    = (first + last) / 2;
    int n          = last - first + 1;
    for (int i = 1; i < n - 1; ++i) {
        Vec2f p    = EvalBezier(bez, u[i]);
        float dist = (pts[first + i] - p).LengthSquared();
        if (dist > max_dist) {
            max_dist    = dist;
            split_point = first + i;
        }
    }
    return std::sqrt(max_dist);
}

std::vector<float> Reparameterize(const std::vector<Vec2f>& pts, int first, int last,
                                  const CubicBezier& bez, const std::vector<float>& u) {
    int n = last - first + 1;
    std::vector<float> u_new(n);
    for (int i = 0; i < n; ++i) {
        float t    = u[i];
        Vec2f p    = EvalBezier(bez, t);
        Vec2f d    = EvalBezierDeriv(bez, t);
        Vec2f dd   = EvalBezierSecondDeriv(bez, t);
        Vec2f diff = p - pts[first + i];
        float num  = diff.Dot(d);
        float den  = d.Dot(d) + diff.Dot(dd);
        u_new[i]   = (std::abs(den) > 1e-12f) ? t - num / den : t;
        u_new[i]   = std::clamp(u_new[i], 0.0f, 1.0f);
    }
    return u_new;
}

void FitRecursive(const std::vector<Vec2f>& pts, int first, int last, Vec2f tHat1, Vec2f tHat2,
                  float tolerance, std::vector<CubicBezier>& out, int depth = 0) {
    if (last - first < 1) return;

    if (last - first == 1) {
        float dist = (pts[last] - pts[first]).Length() / 3.0f;
        CubicBezier bez;
        bez.p0 = pts[first];
        bez.p1 = pts[first] + tHat1 * dist;
        bez.p2 = pts[last] + tHat2 * dist;
        bez.p3 = pts[last];
        out.push_back(bez);
        return;
    }

    auto u   = ChordLengthParameterize(pts, first, last);
    auto bez = FitSingleBezier(pts, first, last, u, tHat1, tHat2);

    int split;
    float err = MaxError(pts, first, last, bez, u, split);

    if (err <= tolerance) {
        out.push_back(bez);
        return;
    }

    if (err <= tolerance * 6.0f && depth < 6) {
        for (int iter = 0; iter < 6; ++iter) {
            auto u_new = Reparameterize(pts, first, last, bez, u);
            bez        = FitSingleBezier(pts, first, last, u_new, tHat1, tHat2);
            err        = MaxError(pts, first, last, bez, u_new, split);
            if (err <= tolerance) {
                out.push_back(bez);
                return;
            }
            u = u_new;
        }
    }

    Vec2f tCenter = (pts[split + 1] - pts[split - 1]).Normalized();
    FitRecursive(pts, first, split, tHat1, tCenter * -1.0f, tolerance, out, depth + 1);
    FitRecursive(pts, split, last, tCenter, tHat2, tolerance, out, depth + 1);
}

// ── Potrace alpha computation ───────────────────────────────────────────────

float ComputeAlpha(Vec2f b_prev, Vec2f a, Vec2f b_next) {
    Vec2f d     = b_next - b_prev;
    float len_d = d.Length();
    if (len_d < 1e-9f) return 2.0f;

    Vec2f dir = d.Normalized();
    Vec2f ba  = a - b_prev;

    Vec2f ba_dir = ba.Normalized();
    float ba_len = ba.Length();
    if (ba_len < 1e-9f) return 2.0f;

    Vec2f perp{-dir.y, dir.x};

    float sign    = ((a - b_prev).Cross(d) > 0) ? 1.0f : -1.0f;
    Vec2f l_point = a + perp * (sign * 0.5f);

    float denom = ba_dir.Cross(dir);
    if (std::abs(denom) < 1e-9f) return 0.55f;

    Vec2f diff        = l_point - b_prev;
    float t_intersect = diff.Cross(dir) / denom;
    float gamma       = t_intersect / ba_len;

    return 4.0f * gamma / 3.0f;
}

} // namespace

// ── Potrace curve fitting ───────────────────────────────────────────────────

std::vector<CurveSegment> FitPotraceCurve(const std::vector<Vec2f>& vertices, float alpha_max) {
    int m = static_cast<int>(vertices.size());
    if (m < 2) return {};

    std::vector<CurveSegment> segments;

    std::vector<Vec2f> midpts(m);
    for (int i = 0; i < m; ++i) {
        int next  = (i + 1) % m;
        midpts[i] = Vec2f::Lerp(vertices[i], vertices[next], 0.5f);
    }

    for (int i = 0; i < m; ++i) {
        int prev = Mod(i - 1, m);
        Vec2f bp = midpts[prev];
        Vec2f bn = midpts[i];
        Vec2f ai = vertices[i];

        float alpha = ComputeAlpha(bp, ai, bn);

        CurveSegment seg;
        if (alpha > alpha_max) {
            seg.type = CurveSegment::CORNER;
            seg.p0   = bp;
            seg.p1   = ai;
            seg.p2   = ai;
            seg.p3   = bn;
        } else {
            alpha    = std::clamp(alpha, 0.55f, 1.0f);
            seg.type = CurveSegment::BEZIER;
            seg.p0   = bp;
            seg.p3   = bn;
            seg.p1   = Vec2f::Lerp(bp, ai, alpha);
            seg.p2   = Vec2f::Lerp(bn, ai, alpha);
        }
        segments.push_back(seg);
    }

    return segments;
}

// ── Curve optimization ──────────────────────────────────────────────────────

std::vector<CurveSegment> OptimizeCurve(const std::vector<CurveSegment>& segments,
                                        float opt_tolerance) {
    int n = static_cast<int>(segments.size());
    if (n <= 2) return segments;

    std::vector<CurveSegment> result;

    int i = 0;
    while (i < n) {
        if (segments[i].type == CurveSegment::CORNER) {
            result.push_back(segments[i]);
            ++i;
            continue;
        }

        int run_start          = i;
        float first_cross_sign = 0;
        float total_cross      = 0;
        bool same_dir          = true;

        int j = i;
        while (j < n && segments[j].type == CurveSegment::BEZIER) {
            Vec2f d_in  = segments[j].p1 - segments[j].p0;
            Vec2f d_out = segments[j].p3 - segments[j].p2;
            float c     = d_in.Cross(d_out);

            if (j == i) {
                first_cross_sign = (c >= 0) ? 1.0f : -1.0f;
            } else if ((c >= 0) != (first_cross_sign >= 0) && std::abs(c) > 1e-6f) {
                same_dir = false;
                break;
            }

            Vec2f seg_d     = segments[j].p3 - segments[j].p0;
            float seg_angle = std::atan2(seg_d.y, seg_d.x);

            if (j > i) {
                Vec2f prev_d     = segments[j - 1].p3 - segments[j - 1].p0;
                float prev_angle = std::atan2(prev_d.y, prev_d.x);
                float da         = seg_angle - prev_angle;
                while (da > M_PI) da -= 2 * M_PI;
                while (da < -M_PI) da += 2 * M_PI;
                total_cross += da;
                if (std::abs(total_cross) > 179.0f * M_PI / 180.0f) {
                    same_dir = false;
                    break;
                }
            }
            ++j;
        }

        int run_len = j - run_start;
        if (run_len <= 1 || !same_dir) {
            result.push_back(segments[i]);
            ++i;
            continue;
        }

        Vec2f start   = segments[run_start].p0;
        Vec2f end     = segments[j - 1].p3;
        Vec2f d_start = (segments[run_start].p1 - segments[run_start].p0).Normalized();
        Vec2f d_end   = (segments[j - 1].p2 - segments[j - 1].p3).Normalized();

        float cross_tan = d_start.Cross(d_end);

        if (std::abs(cross_tan) < 1e-6f) {
            for (int k = run_start; k < j; ++k) result.push_back(segments[k]);
            i = j;
            continue;
        }

        Vec2f diff    = end - start;
        float t_param = diff.Cross(d_end) / cross_tan;
        float span    = (end - start).Length();

        float total_area = 0;
        for (int k = run_start; k < j; ++k) {
            auto& s = segments[k];
            total_area += 0.5f * (s.p1 - s.p0).Cross(s.p3 - s.p0);
            total_area += 0.5f * (s.p2 - s.p0).Cross(s.p3 - s.p0);
        }

        CurveSegment merged;
        merged.type = CurveSegment::BEZIER;
        merged.p0   = start;
        merged.p3   = end;

        float alpha = std::clamp(t_param / span * 3.0f, 0.3f, 3.0f);
        merged.p1   = start + d_start * (span * alpha / 3.0f);
        merged.p2   = end + d_end * (span * alpha / 3.0f);

        bool acceptable = true;
        for (int k = run_start; k < j && acceptable; ++k) {
            auto& s = segments[k];
            for (float t = 0.0f; t <= 1.0f; t += 0.2f) {
                Vec2f orig;
                if (s.type == CurveSegment::BEZIER) {
                    orig = EvalBezier({s.p0, s.p1, s.p2, s.p3}, t);
                } else {
                    orig = Vec2f::Lerp(s.p0, s.p3, t);
                }

                float min_dist = std::numeric_limits<float>::max();
                CubicBezier mcb{merged.p0, merged.p1, merged.p2, merged.p3};
                for (float u = 0.0f; u <= 1.0f; u += 0.02f) {
                    float d  = (EvalBezier(mcb, u) - orig).LengthSquared();
                    min_dist = std::min(min_dist, d);
                }
                if (std::sqrt(min_dist) > opt_tolerance) {
                    acceptable = false;
                    break;
                }
            }
        }

        if (acceptable) {
            result.push_back(merged);
        } else {
            for (int k = run_start; k < j; ++k) result.push_back(segments[k]);
        }
        i = j;
    }

    return result;
}

// ── Schneider corner detection ──────────────────────────────────────────────

std::vector<int> DetectCorners(const std::vector<Vec2f>& points, float angle_threshold,
                               int window) {
    int n = static_cast<int>(points.size());
    if (n < 3) return {0, std::max(0, n - 1)};

    float cos_thresh = std::cos(angle_threshold * static_cast<float>(M_PI) / 180.0f);
    std::vector<int> corners;
    corners.push_back(0);

    for (int i = 1; i < n - 1; ++i) {
        Vec2f d_back{0, 0}, d_fwd{0, 0};
        int cb = 0, cf = 0;
        for (int w = 1; w <= window && i - w >= 0; ++w) {
            d_back = d_back + (points[i] - points[i - w]);
            ++cb;
        }
        for (int w = 1; w <= window && i + w < n; ++w) {
            d_fwd = d_fwd + (points[i + w] - points[i]);
            ++cf;
        }
        if (cb == 0 || cf == 0) continue;

        d_back = d_back.Normalized();
        d_fwd  = d_fwd.Normalized();

        float cos_angle = d_back.Dot(d_fwd);
        if (cos_angle < cos_thresh) { corners.push_back(i); }
    }

    if (corners.back() != n - 1) corners.push_back(n - 1);
    return corners;
}

// ── Schneider fitting ───────────────────────────────────────────────────────

BezierContour FitSchneider(const std::vector<Vec2f>& points, const std::vector<int>& corners,
                           float tolerance) {
    BezierContour contour;
    if (points.size() < 2 || corners.size() < 2) return contour;

    for (size_t ci = 0; ci + 1 < corners.size(); ++ci) {
        int first = corners[ci];
        int last  = corners[ci + 1];
        if (last <= first) continue;

        Vec2f tHat1 = EstimateTangent(points, first, 3, false);
        Vec2f tHat2 = EstimateTangent(points, last, 3, true);

        FitRecursive(points, first, last, tHat1, tHat2, tolerance, contour.segments);
    }

    return contour;
}

// ── Conversion ──────────────────────────────────────────────────────────────

BezierContour SegmentsToBezierContour(const std::vector<CurveSegment>& segments) {
    BezierContour contour;
    contour.closed = true;

    for (auto& seg : segments) {
        if (seg.type == CurveSegment::BEZIER) {
            contour.segments.push_back({seg.p0, seg.p1, seg.p2, seg.p3});
        } else {
            Vec2f mid = seg.p1;
            contour.segments.push_back({seg.p0, Vec2f::Lerp(seg.p0, mid, 1.0f / 3.0f),
                                        Vec2f::Lerp(seg.p0, mid, 2.0f / 3.0f), mid});
            contour.segments.push_back({mid, Vec2f::Lerp(mid, seg.p3, 1.0f / 3.0f),
                                        Vec2f::Lerp(mid, seg.p3, 2.0f / 3.0f), seg.p3});
        }
    }

    return contour;
}

} // namespace ChromaPrint3D::detail
