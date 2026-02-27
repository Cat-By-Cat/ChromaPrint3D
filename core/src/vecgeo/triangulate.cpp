#include "triangulate.h"

#include <clipper2/clipper.h>
#include <earcut/earcut.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace mapbox::util {

template <>
struct nth<0, ChromaPrint3D::Vec2f> {
    static auto get(const ChromaPrint3D::Vec2f& p) { return p.x; }
};

template <>
struct nth<1, ChromaPrint3D::Vec2f> {
    static auto get(const ChromaPrint3D::Vec2f& p) { return p.y; }
};

} // namespace mapbox::util

namespace ChromaPrint3D::detail {

namespace {

constexpr double kClipperScale = 100000.0;

Clipper2Lib::Path64 ContourToPath(const Contour& contour) {
    Clipper2Lib::Path64 path;
    path.reserve(contour.size());
    for (const Vec2f& p : contour) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        path.emplace_back(static_cast<int64_t>(std::llround(p.x * kClipperScale)),
                          static_cast<int64_t>(std::llround(p.y * kClipperScale)));
    }
    return path;
}

Contour PathToContour(const Clipper2Lib::Path64& path) {
    Contour contour;
    contour.reserve(path.size());
    for (const auto& pt : path) {
        contour.push_back(Vec2f(static_cast<float>(pt.x / kClipperScale),
                                static_cast<float>(pt.y / kClipperScale)));
    }
    return contour;
}

Clipper2Lib::Paths64 BuildCleanPaths(const VectorShape& shape) {
    Clipper2Lib::Paths64 raw_paths;
    raw_paths.reserve(shape.contours.size());
    for (const Contour& c : shape.contours) {
        if (c.size() < 3) continue;
        auto path = ContourToPath(c);
        if (path.size() >= 3) raw_paths.push_back(std::move(path));
    }
    if (raw_paths.empty()) return {};

    auto cleaned = Clipper2Lib::SimplifyPaths(raw_paths, 1.0, true);
    Clipper2Lib::Paths64 filtered;
    filtered.reserve(cleaned.size());
    for (auto& path : cleaned) {
        if (path.size() < 3) continue;
        if (std::abs(Clipper2Lib::Area(path)) < 4.0) continue;
        filtered.push_back(std::move(path));
    }
    return filtered;
}

struct RingInfo {
    Clipper2Lib::Path64 path;
    double abs_area = 0.0;
    bool is_outer   = true;
    int owner_outer = -1;
};

} // namespace

void TriangulateShape(const VectorShape& shape, std::vector<Vec2f>& out_vertices,
                      std::vector<Vec3i>& out_indices) {
    if (shape.contours.empty()) return;

    Clipper2Lib::Paths64 cleaned_paths = BuildCleanPaths(shape);
    if (cleaned_paths.empty()) return;

    std::vector<RingInfo> rings;
    rings.reserve(cleaned_paths.size());
    for (auto& path : cleaned_paths) {
        RingInfo info;
        info.path     = std::move(path);
        info.abs_area = std::abs(Clipper2Lib::Area(info.path));
        rings.push_back(std::move(info));
    }
    if (rings.empty()) return;

    // Classify outer/holes by containment depth, independent of original orientation.
    for (size_t i = 0; i < rings.size(); ++i) {
        int depth = 0;
        for (size_t j = 0; j < rings.size(); ++j) {
            if (i == j) continue;
            if (Clipper2Lib::Path2ContainsPath1(rings[i].path, rings[j].path)) { ++depth; }
        }
        rings[i].is_outer = (depth % 2 == 0);
    }

    // Normalize winding: outer -> positive area, hole -> negative area.
    for (auto& ring : rings) {
        bool is_positive = Clipper2Lib::Area(ring.path) > 0;
        if ((ring.is_outer && !is_positive) || (!ring.is_outer && is_positive)) {
            std::reverse(ring.path.begin(), ring.path.end());
        }
        ring.abs_area = std::abs(Clipper2Lib::Area(ring.path));
    }

    std::vector<int> outer_ids;
    outer_ids.reserve(rings.size());
    for (size_t i = 0; i < rings.size(); ++i) {
        if (rings[i].is_outer) outer_ids.push_back(static_cast<int>(i));
    }

    // Assign each hole to the smallest containing outer ring.
    for (size_t i = 0; i < rings.size(); ++i) {
        if (rings[i].is_outer) continue;
        double best_area = std::numeric_limits<double>::infinity();
        int best_outer   = -1;
        for (int oid : outer_ids) {
            if (!Clipper2Lib::Path2ContainsPath1(rings[i].path,
                                                 rings[static_cast<size_t>(oid)].path)) {
                continue;
            }
            double outer_area = rings[static_cast<size_t>(oid)].abs_area;
            if (outer_area < best_area) {
                best_area  = outer_area;
                best_outer = oid;
            }
        }
        if (best_outer < 0) {
            // Orphan hole: treat as a standalone outer ring to avoid invalid polygons.
            if (Clipper2Lib::Area(rings[i].path) < 0) {
                std::reverse(rings[i].path.begin(), rings[i].path.end());
            }
            rings[i].is_outer = true;
            outer_ids.push_back(static_cast<int>(i));
        } else {
            rings[i].owner_outer = best_outer;
        }
    }

    std::unordered_map<int, int> outer_to_group;
    std::vector<Clipper2Lib::Paths64> grouped;
    grouped.reserve(outer_ids.size());
    for (int oid : outer_ids) {
        outer_to_group[oid] = static_cast<int>(grouped.size());
        grouped.push_back(Clipper2Lib::Paths64{rings[static_cast<size_t>(oid)].path});
    }
    for (const auto& ring : rings) {
        if (ring.is_outer || ring.owner_outer < 0) continue;
        auto it = outer_to_group.find(ring.owner_outer);
        if (it == outer_to_group.end()) continue;
        grouped[static_cast<size_t>(it->second)].push_back(ring.path);
    }

    for (const auto& group : grouped) {
        if (group.empty()) continue;
        std::vector<std::vector<Vec2f>> polygon;
        polygon.reserve(group.size());
        size_t total_pts = 0;
        for (const auto& ring_path : group) {
            Contour ring = PathToContour(ring_path);
            if (ring.size() < 3) continue;
            total_pts += ring.size();
            polygon.push_back(std::move(ring));
        }
        if (polygon.empty()) continue;

        std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);
        if (indices.empty()) continue;

        size_t base = out_vertices.size();
        out_vertices.reserve(base + total_pts);
        for (const auto& ring : polygon) {
            for (const Vec2f& p : ring) out_vertices.push_back(p);
        }

        size_t tri_count = indices.size() / 3;
        out_indices.reserve(out_indices.size() + tri_count);
        for (size_t i = 0; i < indices.size(); i += 3) {
            out_indices.emplace_back(static_cast<int>(base + indices[i]),
                                     static_cast<int>(base + indices[i + 1]),
                                     static_cast<int>(base + indices[i + 2]));
        }
    }
}

} // namespace ChromaPrint3D::detail
