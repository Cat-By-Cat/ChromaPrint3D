#include "chromaprint3d/vector_mesh.h"
#include "chromaprint3d/vector_recipe_map.h"
#include "chromaprint3d/error.h"
#include "triangulate.h"

#include <clipper2/clipper.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ChromaPrint3D {

namespace {

constexpr double kClipperScale = 100000.0;

// ---------------------------------------------------------------------------
// Coordinate conversion
// ---------------------------------------------------------------------------

Clipper2Lib::Path64 ContourToPath64(const Contour& c) {
    Clipper2Lib::Path64 path;
    path.reserve(c.size());
    for (const Vec2f& p : c) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        path.emplace_back(static_cast<int64_t>(std::llround(p.x * kClipperScale)),
                          static_cast<int64_t>(std::llround(p.y * kClipperScale)));
    }
    return path;
}

// ---------------------------------------------------------------------------
// Shape contour union
// ---------------------------------------------------------------------------

Clipper2Lib::Paths64 UnionShapeContours(const std::vector<VectorShape>& shapes,
                                        const std::vector<int>& indices) {
    Clipper2Lib::Paths64 all_paths;
    for (int idx : indices) {
        const auto& shape = shapes[static_cast<size_t>(idx)];
        for (const Contour& c : shape.contours) {
            if (c.size() < 3) continue;
            auto path = ContourToPath64(c);
            if (path.size() >= 3) all_paths.push_back(std::move(path));
        }
    }
    if (all_paths.empty()) return {};

    constexpr double kMicroClose = 50.0;
    auto inflated = Clipper2Lib::InflatePaths(all_paths, kMicroClose, Clipper2Lib::JoinType::Round,
                                              Clipper2Lib::EndType::Polygon);
    auto merged   = Clipper2Lib::Union(inflated, Clipper2Lib::FillRule::NonZero);
    auto closed   = Clipper2Lib::InflatePaths(merged, -kMicroClose, Clipper2Lib::JoinType::Miter,
                                              Clipper2Lib::EndType::Polygon);

    constexpr double kSimplifyTolerance = 50.0;
    closed = Clipper2Lib::SimplifyPaths(closed, kSimplifyTolerance, true);

    Clipper2Lib::Paths64 result;
    result.reserve(closed.size());
    for (auto& p : closed) {
        if (p.size() >= 3 && std::abs(Clipper2Lib::Area(p)) > 1.0) {
            result.push_back(std::move(p));
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Z coordinate computation
// ---------------------------------------------------------------------------

struct ZRange {
    float bot = 0.0f;
    float top = 0.0f;
};

ZRange ComputeLayerZ(int logical_layer, int color_layers, LayerOrder order, int base_layers,
                     float lh, float half_gap, bool double_sided) {
    int mapped =
        (order == LayerOrder::Top2Bottom) ? (color_layers - 1 - logical_layer) : logical_layer;
    int base_offset = (double_sided ? color_layers : 0) + base_layers;
    int stored      = base_offset + mapped;

    float z_bot = static_cast<float>(stored) * lh;
    float z_top = static_cast<float>(stored + 1) * lh;

    if (half_gap > 0.0f && stored == base_offset) { z_bot += half_gap; }
    return {z_bot, z_top};
}

ZRange ComputeMirrorZ(int logical_layer, int color_layers, LayerOrder order, int base_layers,
                      float lh, float half_gap) {
    int mapped =
        (order == LayerOrder::Top2Bottom) ? (color_layers - 1 - logical_layer) : logical_layer;
    int base_start = color_layers;
    int mirror     = (base_start - 1) - mapped;

    float z_bot = static_cast<float>(mirror) * lh;
    float z_top = static_cast<float>(mirror + 1) * lh;

    if (half_gap > 0.0f && mirror + 1 == base_start) { z_top -= half_gap; }
    return {z_bot, z_top};
}

ZRange ComputeBaseZ(int color_layers, int base_layers, float lh, float half_gap,
                    bool double_sided) {
    int base_start = double_sided ? color_layers : 0;
    float z_bot    = static_cast<float>(base_start) * lh;
    float z_top    = static_cast<float>(base_start + base_layers) * lh;

    if (half_gap > 0.0f) {
        if (double_sided) { z_bot += half_gap; }
        z_top -= half_gap;
    }
    return {z_bot, z_top};
}

// ---------------------------------------------------------------------------
// Slab extrusion (the core geometric primitive)
// ---------------------------------------------------------------------------

void ExtrudeSlab(const detail::TriangulatedRegion& region, float z_bot, float z_top,
                 std::vector<Vec3f>& out_verts, std::vector<Vec3i>& out_faces) {
    if (region.Empty()) return;

    const int N    = static_cast<int>(region.vertices.size());
    const int base = static_cast<int>(out_verts.size());

    out_verts.reserve(out_verts.size() + static_cast<size_t>(2 * N));
    for (const Vec2f& v : region.vertices) { out_verts.push_back({v.x, v.y, z_top}); }
    for (const Vec2f& v : region.vertices) { out_verts.push_back({v.x, v.y, z_bot}); }

    const size_t num_tris       = region.triangles.size();
    const size_t num_wall_edges = region.vertices.size();
    out_faces.reserve(out_faces.size() + 2 * num_tris + 2 * num_wall_edges);

    for (const Vec3i& t : region.triangles) {
        out_faces.push_back({base + t.x, base + t.y, base + t.z});
    }
    for (const Vec3i& t : region.triangles) {
        out_faces.push_back({base + N + t.x, base + N + t.z, base + N + t.y});
    }

    constexpr float kMinEdgeLenSq = 1e-12f;

    int offset = 0;
    for (const auto& group : region.polygon_groups) {
        for (const auto& ring : group) {
            const int n = static_cast<int>(ring.size());
            for (int i = 0; i < n; ++i) {
                int j    = (i + 1) % n;
                float dx = ring[static_cast<size_t>(j)].x - ring[static_cast<size_t>(i)].x;
                float dy = ring[static_cast<size_t>(j)].y - ring[static_cast<size_t>(i)].y;
                if (dx * dx + dy * dy < kMinEdgeLenSq) continue;

                int ti = base + offset + i;
                int tj = base + offset + j;
                int bi = base + N + offset + i;
                int bj = base + N + offset + j;
                out_faces.push_back({bi, bj, tj});
                out_faces.push_back({bi, tj, ti});
            }
            offset += n;
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry point
// ---------------------------------------------------------------------------

std::vector<Mesh> BuildVectorMeshes(const std::vector<VectorShape>& shapes,
                                    const VectorRecipeMap& recipe_map,
                                    const VectorMeshConfig& cfg) {
    if (recipe_map.entries.empty() || recipe_map.num_channels <= 0) { return {}; }

    const int num_channels = recipe_map.num_channels;
    const int color_layers = recipe_map.color_layers;
    const float lh         = cfg.layer_height_mm;

    if (lh <= 0.0f) { throw InputError("VectorMeshConfig layer_height_mm must be positive"); }

    const int base_layers = cfg.base_layers;
    const bool has_base   = base_layers > 0;
    const int mesh_count  = num_channels + (has_base ? 1 : 0);
    const float half_gap  = cfg.base_color_gap_mm * 0.5f;

    // === Phase 1: Group shapes by (layer, channel) ===

    const size_t total_groups =
        static_cast<size_t>(color_layers) * static_cast<size_t>(num_channels);
    std::vector<std::vector<int>> groups(total_groups);

    for (const auto& entry : recipe_map.entries) {
        if (entry.shape_idx < 0 || entry.shape_idx >= static_cast<int>(shapes.size())) continue;
        if (shapes[static_cast<size_t>(entry.shape_idx)].contours.empty()) continue;

        for (int L = 0; L < color_layers; ++L) {
            if (L >= static_cast<int>(entry.recipe.size())) continue;
            int C = static_cast<int>(entry.recipe[static_cast<size_t>(L)]);
            if (C < 0 || C >= num_channels) continue;
            groups[static_cast<size_t>(L * num_channels + C)].push_back(entry.shape_idx);
        }
    }

    for (auto& g : groups) {
        std::sort(g.begin(), g.end());
        g.erase(std::unique(g.begin(), g.end()), g.end());
    }

    // === Phase 1b: Detect cross-layer runs per channel ===
    //
    // Consecutive layers with identical shape index sets for a channel are merged
    // into a single "run". This avoids redundant Union+earcut and eliminates
    // internal faces between adjacent same-region slabs.

    struct ChannelRun {
        int first_layer;
        int last_layer;
        size_t group_idx;
    };

    std::vector<std::vector<ChannelRun>> runs_per_channel(static_cast<size_t>(num_channels));

    for (int C = 0; C < num_channels; ++C) {
        int run_start = 0;
        for (int L = 1; L <= color_layers; ++L) {
            bool same = false;
            if (L < color_layers) {
                size_t cur  = static_cast<size_t>(L * num_channels + C);
                size_t prev = static_cast<size_t>((L - 1) * num_channels + C);
                same        = (groups[cur] == groups[prev]);
            }
            if (!same) {
                size_t gidx = static_cast<size_t>(run_start * num_channels + C);
                if (!groups[gidx].empty()) {
                    runs_per_channel[static_cast<size_t>(C)].push_back({run_start, L - 1, gidx});
                }
                run_start = L;
            }
        }
    }

    // Collect unique group indices that need processing.
    std::vector<size_t> unique_group_indices;
    for (const auto& runs : runs_per_channel) {
        for (const auto& run : runs) { unique_group_indices.push_back(run.group_idx); }
    }
    std::sort(unique_group_indices.begin(), unique_group_indices.end());
    unique_group_indices.erase(
        std::unique(unique_group_indices.begin(), unique_group_indices.end()),
        unique_group_indices.end());

    {
        size_t skipped = total_groups - unique_group_indices.size();
        if (skipped > 0) {
            spdlog::debug("Cross-layer compression: {} total groups -> {} unique ({} skipped)",
                          total_groups, unique_group_indices.size(), skipped);
        }
    }

    // === Phase 2: Merge + triangulate only unique groups ===

    std::vector<detail::TriangulatedRegion> regions(total_groups);

    const int num_unique = static_cast<int>(unique_group_indices.size());
#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 0; i < num_unique; ++i) {
        size_t idx        = unique_group_indices[static_cast<size_t>(i)];
        const auto& group = groups[idx];
        if (group.empty()) continue;

        Clipper2Lib::Paths64 merged = UnionShapeContours(shapes, group);
        if (merged.empty()) continue;

        regions[idx] = detail::TriangulateMergedPaths(merged);
    }

    // === Phase 3: Extrude per channel using compressed runs ===

    std::vector<Mesh> meshes(static_cast<size_t>(mesh_count));

#ifdef _OPENMP
#    pragma omp parallel for schedule(static)
#endif
    for (int C = 0; C < num_channels; ++C) {
        std::vector<Vec3f> verts;
        std::vector<Vec3i> faces;

        for (const auto& run : runs_per_channel[static_cast<size_t>(C)]) {
            const auto& region = regions[run.group_idx];
            if (region.Empty()) continue;

            ZRange z_first = ComputeLayerZ(run.first_layer, color_layers, recipe_map.layer_order,
                                           base_layers, lh, half_gap, cfg.double_sided);
            ZRange z_last  = ComputeLayerZ(run.last_layer, color_layers, recipe_map.layer_order,
                                           base_layers, lh, half_gap, cfg.double_sided);
            float z_bot    = std::min(z_first.bot, z_last.bot);
            float z_top    = std::max(z_first.top, z_last.top);
            ExtrudeSlab(region, z_bot, z_top, verts, faces);

            if (cfg.double_sided && base_layers > 0) {
                ZRange mz_first = ComputeMirrorZ(run.first_layer, color_layers,
                                                 recipe_map.layer_order, base_layers, lh, half_gap);
                ZRange mz_last  = ComputeMirrorZ(run.last_layer, color_layers,
                                                 recipe_map.layer_order, base_layers, lh, half_gap);
                float mz_bot    = std::min(mz_first.bot, mz_last.bot);
                float mz_top    = std::max(mz_first.top, mz_last.top);
                ExtrudeSlab(region, mz_bot, mz_top, verts, faces);
            }
        }

        meshes[static_cast<size_t>(C)] = Mesh{std::move(verts), std::move(faces)};
    }

    // === Phase 4: Base mesh ===

    if (has_base) {
        std::vector<int> all_indices;
        all_indices.reserve(recipe_map.entries.size());
        for (const auto& entry : recipe_map.entries) {
            if (entry.shape_idx >= 0 && entry.shape_idx < static_cast<int>(shapes.size())) {
                all_indices.push_back(entry.shape_idx);
            }
        }
        std::sort(all_indices.begin(), all_indices.end());
        all_indices.erase(std::unique(all_indices.begin(), all_indices.end()), all_indices.end());

        Clipper2Lib::Paths64 merged            = UnionShapeContours(shapes, all_indices);
        detail::TriangulatedRegion base_region = detail::TriangulateMergedPaths(merged);

        std::vector<Vec3f> verts;
        std::vector<Vec3i> faces;
        ZRange bz = ComputeBaseZ(color_layers, base_layers, lh, half_gap, cfg.double_sided);
        ExtrudeSlab(base_region, bz.bot, bz.top, verts, faces);

        meshes[static_cast<size_t>(num_channels)] = Mesh{std::move(verts), std::move(faces)};
    }

    size_t total_verts = 0, total_tris = 0;
    for (const auto& m : meshes) {
        total_verts += m.vertices.size();
        total_tris += m.indices.size();
    }
    spdlog::info("BuildVectorMeshes: {} meshes, total vertices={}, triangles={}", meshes.size(),
                 total_verts, total_tris);

    return meshes;
}

} // namespace ChromaPrint3D
