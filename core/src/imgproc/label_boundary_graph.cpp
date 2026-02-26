#include "imgproc/label_boundary_graph.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D::detail {

double SignedLoopArea(const std::vector<Vec2f>& loop);

namespace {

enum class Dir : uint8_t { East = 0, South = 1, West = 2, North = 3 };

struct GridPoint {
    int x = 0;
    int y = 0;
};

struct DirectedEdge {
    int region = -1;
    int start  = -1; // vertex id
    int end    = -1; // vertex id
    Dir dir    = Dir::East;
    bool used  = false;
};

inline int64_t PackPoint(int x, int y) {
    return (static_cast<int64_t>(y) << 32) ^ static_cast<uint32_t>(x);
}

class VertexPool {
public:
    int GetOrCreate(int x, int y) {
        int64_t key = PackPoint(x, y);
        auto it     = index_.find(key);
        if (it != index_.end()) return it->second;
        int id      = static_cast<int>(points_.size());
        index_[key] = id;
        points_.push_back({x, y});
        return id;
    }

    const std::vector<GridPoint>& Points() const { return points_; }

private:
    std::unordered_map<int64_t, int> index_;
    std::vector<GridPoint> points_;
};

Dir DirFromDelta(int dx, int dy) {
    if (dx == 1 && dy == 0) return Dir::East;
    if (dx == 0 && dy == 1) return Dir::South;
    if (dx == -1 && dy == 0) return Dir::West;
    return Dir::North;
}

int TurnPriority(Dir in_dir, Dir out_dir) {
    int in_v  = static_cast<int>(in_dir);
    int out_v = static_cast<int>(out_dir);
    int delta = (out_v - in_v + 4) % 4;
    // Keep region on the left: prefer right turn, then straight, then left, then back.
    switch (delta) {
    case 1:
        return 0;
    case 0:
        return 1;
    case 3:
        return 2;
    default:
        return 3;
    }
}

void AddEdge(std::vector<DirectedEdge>& edges, std::vector<std::vector<int>>& region_edge_ids,
             VertexPool& vertices, int region, int x0, int y0, int x1, int y1) {
    if (region < 0 || region >= static_cast<int>(region_edge_ids.size())) return;
    int dx = x1 - x0;
    int dy = y1 - y0;
    if ((dx == 0 && dy == 0) || (dx != 0 && dy != 0)) return;

    DirectedEdge e;
    e.region = region;
    e.start  = vertices.GetOrCreate(x0, y0);
    e.end    = vertices.GetOrCreate(x1, y1);
    e.dir    = DirFromDelta(dx, dy);

    int eid = static_cast<int>(edges.size());
    edges.push_back(e);
    region_edge_ids[region].push_back(eid);
}

bool TraceLoop(int start_edge_id, std::vector<DirectedEdge>& edges,
               const std::unordered_map<int, std::vector<int>>& outgoing,
               const std::vector<GridPoint>& vertices, std::vector<Vec2f>& out_loop) {
    if (start_edge_id < 0 || start_edge_id >= static_cast<int>(edges.size())) return false;
    if (edges[start_edge_id].used) return false;

    const int start_vertex = edges[start_edge_id].start;
    const int max_steps    = static_cast<int>(edges.size()) + 8;

    std::vector<int> trace;
    trace.reserve(64);
    out_loop.clear();

    int current_edge = start_edge_id;
    for (int step = 0; step < max_steps; ++step) {
        const DirectedEdge& cur = edges[current_edge];
        if (cur.used) return false;
        if (!trace.empty() && current_edge == start_edge_id) break;

        trace.push_back(current_edge);
        const GridPoint& p = vertices[cur.start];
        out_loop.push_back({static_cast<float>(p.x), static_cast<float>(p.y)});

        if (cur.end == start_vertex) {
            if (out_loop.size() < 3) return false;
            if (SignedLoopArea(out_loop) < 0.0) { std::reverse(out_loop.begin(), out_loop.end()); }
            for (int eid : trace) edges[eid].used = true;
            return true;
        }

        auto out_it = outgoing.find(cur.end);
        if (out_it == outgoing.end()) return false;

        int best_next     = -1;
        int best_priority = std::numeric_limits<int>::max();
        for (int cand_id : out_it->second) {
            if (edges[cand_id].used) continue;
            bool already_in_trace = false;
            for (int e : trace) {
                if (e == cand_id) {
                    already_in_trace = true;
                    break;
                }
            }
            if (already_in_trace) continue;

            int prio = TurnPriority(cur.dir, edges[cand_id].dir);
            if (prio < best_priority) {
                best_priority = prio;
                best_next     = cand_id;
            }
        }
        if (best_next < 0) return false;
        current_edge = best_next;
    }

    return false;
}

} // namespace

double SignedLoopArea(const std::vector<Vec2f>& loop) {
    if (loop.size() < 3) return 0.0;
    double acc = 0.0;
    for (size_t i = 0, n = loop.size(); i < n; ++i) {
        const Vec2f& a = loop[i];
        const Vec2f& b = loop[(i + 1) % n];
        acc += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return 0.5 * acc;
}

double LoopPerimeter(const std::vector<Vec2f>& loop) {
    if (loop.size() < 2) return 0.0;
    double acc = 0.0;
    for (size_t i = 0, n = loop.size(); i < n; ++i) {
        const Vec2f& a = loop[i];
        const Vec2f& b = loop[(i + 1) % n];
        acc += std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
    }
    return acc;
}

std::vector<RegionBoundaryLoops> BuildRegionBoundaryLoops(const cv::Mat& labels, int num_regions) {
    std::vector<RegionBoundaryLoops> result(std::max(0, num_regions));
    if (num_regions <= 0) return result;
    if (labels.empty() || labels.type() != CV_32SC1) return result;

    const int rows = labels.rows;
    const int cols = labels.cols;

    VertexPool vertices;
    std::vector<DirectedEdge> edges;
    std::vector<std::vector<int>> region_edge_ids(num_regions);
    edges.reserve(static_cast<size_t>(rows * cols));

    for (int r = 0; r < rows; ++r) {
        const int* row = labels.ptr<int>(r);
        const int* up  = (r > 0) ? labels.ptr<int>(r - 1) : nullptr;
        const int* dn  = (r + 1 < rows) ? labels.ptr<int>(r + 1) : nullptr;
        for (int c = 0; c < cols; ++c) {
            int lid = row[c];
            if (lid < 0 || lid >= num_regions) continue;

            if (!up || up[c] != lid) AddEdge(edges, region_edge_ids, vertices, lid, c, r, c + 1, r);
            if (c + 1 >= cols || row[c + 1] != lid)
                AddEdge(edges, region_edge_ids, vertices, lid, c + 1, r, c + 1, r + 1);
            if (!dn || dn[c] != lid)
                AddEdge(edges, region_edge_ids, vertices, lid, c + 1, r + 1, c, r + 1);
            if (c == 0 || row[c - 1] != lid)
                AddEdge(edges, region_edge_ids, vertices, lid, c, r + 1, c, r);
        }
    }

    std::vector<std::unordered_map<int, std::vector<int>>> outgoing(num_regions);
    for (int rid = 0; rid < num_regions; ++rid) {
        auto& out_map = outgoing[rid];
        for (int eid : region_edge_ids[rid]) {
            const auto& e = edges[eid];
            out_map[e.start].push_back(eid);
        }
    }

    const auto& pts = vertices.Points();
    for (int rid = 0; rid < num_regions; ++rid) {
        auto& loops = result[rid].loops;
        for (int eid : region_edge_ids[rid]) {
            if (edges[eid].used) continue;
            std::vector<Vec2f> loop;
            if (!TraceLoop(eid, edges, outgoing[rid], pts, loop)) continue;
            if (loop.size() < 3) continue;
            if (std::abs(SignedLoopArea(loop)) < 1e-6) continue;
            loops.push_back(std::move(loop));
        }

        std::sort(loops.begin(), loops.end(), [](const auto& a, const auto& b) {
            return std::abs(SignedLoopArea(a)) > std::abs(SignedLoopArea(b));
        });
    }

    return result;
}

} // namespace ChromaPrint3D::detail
