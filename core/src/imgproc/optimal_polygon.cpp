#include "optimal_polygon.h"
#include "math_utils.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace ChromaPrint3D::detail {

namespace {

// Direction enumeration for straightness test
enum Dir { DIR_RIGHT = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_DOWN = 3 };

Dir GetDir(const Vec2f& from, const Vec2f& to) {
    float dx = to.x - from.x;
    float dy = to.y - from.y;
    if (std::abs(dx) > std::abs(dy))
        return dx > 0 ? DIR_RIGHT : DIR_LEFT;
    else
        return dy > 0 ? DIR_UP : DIR_DOWN;
}

// Prefix sums for O(1) penalty calculation.
// For a path {v0, ..., v_{n-1}}, precompute prefix sums of x, y, x^2, y^2, xy.
struct PrefixSums {
    std::vector<double> sx, sy, sxx, syy, sxy;
    int n = 0;

    void Build(const std::vector<Vec2f>& path) {
        n = static_cast<int>(path.size());
        sx.resize(n + 1, 0);
        sy.resize(n + 1, 0);
        sxx.resize(n + 1, 0);
        syy.resize(n + 1, 0);
        sxy.resize(n + 1, 0);
        for (int k = 0; k < n; ++k) {
            sx[k + 1]  = sx[k] + path[k].x;
            sy[k + 1]  = sy[k] + path[k].y;
            sxx[k + 1] = sxx[k] + path[k].x * path[k].x;
            syy[k + 1] = syy[k] + path[k].y * path[k].y;
            sxy[k + 1] = sxy[k] + path[k].x * path[k].y;
        }
    }

    // Sum over cyclic range [i, j] inclusive (mod n).
    // cnt = number of points = CyclicDiff(i, j, n) + 1
    void RangeSum(int i, int j, int& cnt, double& rx, double& ry, double& rxx, double& ryy,
                  double& rxy) const {
        if (i <= j) {
            cnt = j - i + 1;
            rx  = sx[j + 1] - sx[i];
            ry  = sy[j + 1] - sy[i];
            rxx = sxx[j + 1] - sxx[i];
            ryy = syy[j + 1] - syy[i];
            rxy = sxy[j + 1] - sxy[i];
        } else {
            cnt = (n - i) + (j + 1);
            rx  = (sx[n] - sx[i]) + sx[j + 1];
            ry  = (sy[n] - sy[i]) + sy[j + 1];
            rxx = (sxx[n] - sxx[i]) + sxx[j + 1];
            ryy = (syy[n] - syy[i]) + syy[j + 1];
            rxy = (sxy[n] - sxy[i]) + sxy[j + 1];
        }
    }
};

// Compute penalty for a possible segment from vertex i to vertex j.
// P(i,j) = |vj - vi| * stddev(dist(vk, line(vi,vj))) for k in [i..j].
double ComputePenalty(const std::vector<Vec2f>& path, const PrefixSums& ps, int i, int j) {
    int n      = static_cast<int>(path.size());
    float dx   = path[Mod(j, n)].x - path[Mod(i, n)].x;
    float dy   = path[Mod(j, n)].y - path[Mod(i, n)].y;
    double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-9) return 0.0;

    int mi = Mod(i, n);
    int mj = Mod(j, n);
    int cnt;
    double rx, ry, rxx, ryy, rxy;
    ps.RangeSum(mi, mj, cnt, rx, ry, rxx, ryy, rxy);
    if (cnt <= 1) return 0.0;

    double mx = rx / cnt;
    double my = ry / cnt;

    // Variance of signed distance from line through (vi, vj).
    // dist(vk, line) = ((vk.x - vi.x) * dy - (vk.y - vi.y) * dx) / len
    // var = E[dist^2] - E[dist]^2
    double vix = path[mi].x, viy = path[mi].y;

    double a = rxx / cnt - 2.0 * vix * mx + vix * vix;
    double b = rxy / cnt - vix * my - viy * mx + vix * viy;
    double c = ryy / cnt - 2.0 * viy * my + viy * viy;

    // P = sqrt(c * xhat^2 + 2b * xhat * yhat + a * yhat^2) where (xhat, yhat) = (dy, -dx)
    double xhat = dy, yhat = -dx;
    double val = c * xhat * xhat + 2.0 * b * xhat * yhat + a * yhat * yhat;
    if (val < 0) val = 0;
    return std::sqrt(val);
}

// Straightness test: can subpath [i..j] (cyclic) be approximated by a line segment
// within half-pixel tolerance? Uses the constraint-propagation approach from Potrace.
// We precompute for each i the maximum j such that [i..j] is straight.
std::vector<int> ComputeLongestStraight(const std::vector<Vec2f>& path) {
    int n = static_cast<int>(path.size());
    std::vector<int> lon(n);

    // For each starting index, find how far we can extend while remaining straight.
    // A subpath is straight if: (1) max-distance from all points to approximating
    // line <= 0.5, and (2) not all 4 directions appear.
    // We use a simplified O(n^2) approach.
    for (int i = 0; i < n; ++i) {
        bool dirs[4]  = {};
        int dir_count = 0;

        // constraint variables: track min/max of cross products
        double cmin_x = 1e18, cmax_x = -1e18;
        double cmin_y = 1e18, cmax_y = -1e18;

        int max_j = i + 1;
        for (int jj = 1; jj < n; ++jj) {
            int j    = Mod(i + jj, n);
            int prev = Mod(i + jj - 1, n);

            Dir d = GetDir(path[prev], path[j]);
            if (!dirs[d]) {
                dirs[d] = true;
                dir_count++;
            }
            if (dir_count >= 4) break;

            // Check that all intermediate points are within 0.5 max-distance
            // from the line (path[i], path[j]).
            double dx = path[j].x - path[i].x;
            double dy = path[j].y - path[i].y;

            bool ok = true;
            if (std::abs(dx) + std::abs(dy) < 1e-9) {
                // degenerate: i and j are the same point
                // check that all intermediate points are within 0.5
                for (int kk = 1; kk < jj; ++kk) {
                    int k = Mod(i + kk, n);
                    if (std::abs(path[k].x - path[i].x) > 0.5 + 1e-9 ||
                        std::abs(path[k].y - path[i].y) > 0.5 + 1e-9) {
                        ok = false;
                        break;
                    }
                }
            } else {
                double inv_len = 1.0 / std::sqrt(dx * dx + dy * dy);
                for (int kk = 1; kk < jj; ++kk) {
                    int k     = Mod(i + kk, n);
                    double ex = path[k].x - path[i].x;
                    double ey = path[k].y - path[i].y;
                    // max-distance check: |ex - t*dx| <= 0.5 and |ey - t*dy| <= 0.5
                    // for some t. We use the cross product for perpendicular distance
                    // and bound with max-norm.
                    double cross = ex * dy - ey * dx;
                    double perp  = std::abs(cross) * inv_len;
                    if (perp > 0.5 + 1e-9) {
                        ok = false;
                        break;
                    }
                    // Also check that projection is within segment + 0.5
                    double dot    = ex * dx + ey * dy;
                    double len_sq = dx * dx + dy * dy;
                    double t      = dot / len_sq;
                    double rx     = ex - t * dx;
                    double ry     = ey - t * dy;
                    if (std::max(std::abs(rx), std::abs(ry)) > 0.5 + 1e-9) {
                        ok = false;
                        break;
                    }
                }
            }

            if (!ok) break;
            max_j = i + jj + 1;
        }

        lon[i] = std::min(max_j, i + n - 2);
    }
    return lon;
}

} // namespace

OptimalPolygon FindOptimalPolygon(const std::vector<Vec2f>& path) {
    int n = static_cast<int>(path.size());
    if (n < 3) {
        OptimalPolygon result;
        result.vertices.resize(n);
        std::iota(result.vertices.begin(), result.vertices.end(), 0);
        result.segment_count = n;
        return result;
    }

    // Precompute longest straight subpath from each index
    auto lon = ComputeLongestStraight(path);

    // Precompute prefix sums for penalty calculation
    PrefixSums ps;
    ps.Build(path);

    // For each starting vertex s, find optimal polygon using DP.
    // dp[j] = {min_segments, min_penalty} to reach j from s.
    // We try all starting vertices and pick the best cycle.

    // Simplified approach: fix start at vertex 0, find optimal polygon.
    // For closed paths, we try multiple starting points and pick the best.

    struct DPEntry {
        int segments   = std::numeric_limits<int>::max();
        double penalty = std::numeric_limits<double>::infinity();
        int prev       = -1;
    };

    OptimalPolygon best;
    best.segment_count = std::numeric_limits<int>::max();
    best.total_penalty = std::numeric_limits<double>::infinity();

    // Try each vertex as potential start of the polygon
    // For efficiency, we only try a subset for large paths
    int step = std::max(1, n / std::min(n, 50));
    for (int s = 0; s < n; s += step) {
        std::vector<DPEntry> dp(n);
        dp[s].segments = 0;
        dp[s].penalty  = 0.0;
        dp[s].prev     = s;

        // Process vertices in cyclic order from s
        for (int ii = 0; ii < n; ++ii) {
            int i = Mod(s + ii, n);
            if (dp[i].segments == std::numeric_limits<int>::max()) continue;

            int max_reach = lon[i];
            for (int jj = ii + 1; jj <= std::min(ii + (max_reach - (s + ii)) + 1, n); ++jj) {
                int j = Mod(s + jj, n);

                double pen     = ComputePenalty(path, ps, i, j);
                int new_seg    = dp[i].segments + 1;
                double new_pen = dp[i].penalty + pen;

                if (j == s) {
                    // Completed the cycle
                    if (new_seg < best.segment_count ||
                        (new_seg == best.segment_count && new_pen < best.total_penalty)) {
                        best.segment_count = new_seg;
                        best.total_penalty = new_pen;
                        // Reconstruct path
                        best.vertices.clear();
                        best.vertices.push_back(i);
                        int cur = i;
                        while (dp[cur].prev != s && dp[cur].prev != cur) {
                            cur = dp[cur].prev;
                            best.vertices.push_back(cur);
                        }
                        std::reverse(best.vertices.begin(), best.vertices.end());
                    }
                } else if (new_seg < dp[j].segments ||
                           (new_seg == dp[j].segments && new_pen < dp[j].penalty)) {
                    dp[j].segments = new_seg;
                    dp[j].penalty  = new_pen;
                    dp[j].prev     = i;
                }
            }
        }
    }

    if (best.vertices.empty()) {
        // Fallback: use every k-th vertex
        int k = std::max(1, n / 20);
        for (int i = 0; i < n; i += k) best.vertices.push_back(i);
        best.segment_count = static_cast<int>(best.vertices.size());
    }

    return best;
}

std::vector<Vec2f> AdjustVertices(const std::vector<Vec2f>& path, const OptimalPolygon& polygon) {
    int n = static_cast<int>(path.size());
    int m = static_cast<int>(polygon.vertices.size());
    std::vector<Vec2f> adjusted(m);

    if (m == 0) return adjusted;

    PrefixSums ps;
    ps.Build(path);

    for (int k = 0; k < m; ++k) {
        int ik      = polygon.vertices[k];
        int ik_prev = polygon.vertices[Mod(k - 1, m)];
        int ik_next = polygon.vertices[Mod(k + 1, m)];

        // Compute best-fit line for segment [ik_prev, ik]
        // and segment [ik, ik_next], then find their intersection.
        auto best_fit_line = [&](int from, int to) -> std::pair<Vec2f, Vec2f> {
            int cnt;
            double rx, ry, rxx, ryy, rxy;
            ps.RangeSum(from, to, cnt, rx, ry, rxx, ryy, rxy);
            if (cnt < 2) return {{0, 0}, {1, 0}};

            double mx = rx / cnt, my = ry / cnt;
            double a = rxx / cnt - mx * mx;
            double b = rxy / cnt - mx * my;
            double c = ryy / cnt - my * my;

            // Eigenvector of larger eigenvalue of [[a, b], [b, c]]
            double disc = std::sqrt(std::max(0.0, (a - c) * (a - c) + 4 * b * b));
            double lam  = ((a + c) + disc) * 0.5;
            Vec2f dir;
            if (std::abs(b) > 1e-12) {
                dir = {static_cast<float>(lam - c), static_cast<float>(b)};
            } else if (a >= c) {
                dir = {1.0f, 0.0f};
            } else {
                dir = {0.0f, 1.0f};
            }
            dir = dir.Normalized();

            return {Vec2f(static_cast<float>(mx), static_cast<float>(my)), dir};
        };

        auto [p1, d1] = best_fit_line(ik_prev, ik);
        auto [p2, d2] = best_fit_line(ik, ik_next);

        float cross  = d1.Cross(d2);
        Vec2f target = path[ik];

        if (std::abs(cross) > 1e-6f) {
            float t     = (p2 - p1).Cross(d2) / cross;
            Vec2f inter = p1 + d1 * t;

            // Clamp to half-pixel box around original vertex
            target.x = std::clamp(inter.x, path[ik].x - 0.5f, path[ik].x + 0.5f);
            target.y = std::clamp(inter.y, path[ik].y - 0.5f, path[ik].y + 0.5f);
        }

        adjusted[k] = target;
    }

    return adjusted;
}

} // namespace ChromaPrint3D::detail
