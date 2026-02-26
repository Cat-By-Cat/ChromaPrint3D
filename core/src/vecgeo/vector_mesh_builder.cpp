#include "chromaprint3d/vector_mesh.h"
#include "chromaprint3d/vector_recipe_map.h"
#include "chromaprint3d/error.h"
#include "triangulate.h"

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <cstddef>
#include <omp.h>

namespace ChromaPrint3D {

namespace {

struct Vec3fHash {
    size_t operator()(const Vec3f& v) const {
        size_t h = std::hash<float>{}(v.x);
        h ^= std::hash<float>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct Vec3fEq {
    bool operator()(const Vec3f& a, const Vec3f& b) const {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

struct MeshAccumulator {
    std::vector<Vec3f> vertices;
    std::vector<Vec3i> indices;
    std::unordered_map<Vec3f, int, Vec3fHash, Vec3fEq> vertex_map;

    int AddVertex(const Vec3f& v) {
        auto it = vertex_map.find(v);
        if (it != vertex_map.end()) { return it->second; }
        int idx = static_cast<int>(vertices.size());
        vertices.push_back(v);
        vertex_map.emplace(v, idx);
        return idx;
    }

    void AddTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
        int i0 = AddVertex(a);
        int i1 = AddVertex(b);
        int i2 = AddVertex(c);
        indices.emplace_back(i0, i1, i2);
    }

    void ExtrudeShape(const VectorShape& shape, float z_bottom, float z_top) {
        // Triangulate the 2D shape
        std::vector<Vec2f> pts_2d;
        std::vector<Vec3i> tri_2d;
        detail::TriangulateShape(shape, pts_2d, tri_2d);

        if (tri_2d.empty()) { return; }

        // Top face
        for (const Vec3i& tri : tri_2d) {
            Vec3f a{pts_2d[static_cast<size_t>(tri.x)].x, pts_2d[static_cast<size_t>(tri.x)].y,
                    z_top};
            Vec3f b{pts_2d[static_cast<size_t>(tri.y)].x, pts_2d[static_cast<size_t>(tri.y)].y,
                    z_top};
            Vec3f c{pts_2d[static_cast<size_t>(tri.z)].x, pts_2d[static_cast<size_t>(tri.z)].y,
                    z_top};
            AddTriangle(a, b, c);
        }

        // Bottom face (reversed winding)
        for (const Vec3i& tri : tri_2d) {
            Vec3f a{pts_2d[static_cast<size_t>(tri.x)].x, pts_2d[static_cast<size_t>(tri.x)].y,
                    z_bottom};
            Vec3f b{pts_2d[static_cast<size_t>(tri.y)].x, pts_2d[static_cast<size_t>(tri.y)].y,
                    z_bottom};
            Vec3f c{pts_2d[static_cast<size_t>(tri.z)].x, pts_2d[static_cast<size_t>(tri.z)].y,
                    z_bottom};
            AddTriangle(a, c, b);
        }

        // Side walls for each contour edge
        for (const Contour& contour : shape.contours) {
            size_t n = contour.size();
            if (n < 3) { continue; }
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                Vec3f bl{contour[i].x, contour[i].y, z_bottom};
                Vec3f br{contour[j].x, contour[j].y, z_bottom};
                Vec3f tl{contour[i].x, contour[i].y, z_top};
                Vec3f tr{contour[j].x, contour[j].y, z_top};
                AddTriangle(bl, br, tr);
                AddTriangle(bl, tr, tl);
            }
        }
    }

    Mesh ToMesh() const {
        Mesh m;
        m.vertices = vertices;
        m.indices  = indices;
        return m;
    }
};

} // namespace

std::vector<Mesh> BuildVectorMeshes(const std::vector<VectorShape>& shapes,
                                    const VectorRecipeMap& recipe_map,
                                    const VectorMeshConfig& cfg) {
    if (recipe_map.entries.empty() || recipe_map.num_channels <= 0) { return {}; }

    const int num_channels = recipe_map.num_channels;
    const int color_layers = recipe_map.color_layers;
    const float lh         = cfg.layer_height_mm;

    if (lh <= 0.0f) { throw InputError("VectorMeshConfig layer_height_mm must be positive"); }

    const int base_layers  = cfg.base_layers;
    const int base_start   = cfg.double_sided ? color_layers : 0;
    const int total_layers = base_start + base_layers + color_layers;

    const bool has_base  = base_layers > 0;
    const int mesh_count = num_channels + (has_base ? 1 : 0);

    const int num_entries = static_cast<int>(recipe_map.entries.size());

    const int num_threads = omp_get_max_threads();
    std::vector<std::vector<MeshAccumulator>> thread_accumulators(
        static_cast<size_t>(num_threads),
        std::vector<MeshAccumulator>(static_cast<size_t>(mesh_count)));

#pragma omp parallel for schedule(dynamic)
    for (int ei = 0; ei < num_entries; ++ei) {
        const auto& entry = recipe_map.entries[static_cast<size_t>(ei)];
        if (entry.shape_idx < 0 || entry.shape_idx >= static_cast<int>(shapes.size())) { continue; }
        const VectorShape& shape = shapes[static_cast<size_t>(entry.shape_idx)];
        if (shape.contours.empty()) { continue; }

        auto& local_acc = thread_accumulators[static_cast<size_t>(omp_get_thread_num())];

        for (int ch = 0; ch < num_channels; ++ch) {
            int run_start = -1;
            for (int layer = 0; layer < color_layers; ++layer) {
                bool in_ch = (layer < static_cast<int>(entry.recipe.size()) &&
                              static_cast<int>(entry.recipe[static_cast<size_t>(layer)]) == ch);
                if (in_ch && run_start < 0) { run_start = layer; }
                if ((!in_ch || layer == color_layers - 1) && run_start >= 0) {
                    int run_end = in_ch ? layer : layer - 1;

                    int mapped_start = (recipe_map.layer_order == LayerOrder::Top2Bottom)
                                           ? (color_layers - 1 - run_end)
                                           : run_start;
                    int mapped_end   = (recipe_map.layer_order == LayerOrder::Top2Bottom)
                                           ? (color_layers - 1 - run_start)
                                           : run_end;

                    int stored_start = base_start + base_layers + mapped_start;
                    int stored_end   = base_start + base_layers + mapped_end;

                    float z_bot = static_cast<float>(stored_start) * lh;
                    float z_top = static_cast<float>(stored_end + 1) * lh;

                    local_acc[static_cast<size_t>(ch)].ExtrudeShape(shape, z_bot, z_top);
                    run_start = -1;
                }
            }
        }

        if (has_base) {
            float z_bot = static_cast<float>(base_start) * lh;
            float z_top = static_cast<float>(base_start + base_layers) * lh;
            local_acc[static_cast<size_t>(num_channels)].ExtrudeShape(shape, z_bot, z_top);
        }
    }

    // Merge thread-local accumulators into final meshes
    std::vector<Mesh> meshes(static_cast<size_t>(mesh_count));
    size_t total_verts = 0, total_tris = 0;

    for (int mi = 0; mi < mesh_count; ++mi) {
        MeshAccumulator merged;
        for (int ti = 0; ti < num_threads; ++ti) {
            const auto& src = thread_accumulators[static_cast<size_t>(ti)][static_cast<size_t>(mi)];
            int base_idx    = static_cast<int>(merged.vertices.size());
            merged.vertices.insert(merged.vertices.end(), src.vertices.begin(), src.vertices.end());
            for (const Vec3i& tri : src.indices) {
                merged.indices.emplace_back(tri.x + base_idx, tri.y + base_idx, tri.z + base_idx);
            }
        }
        Mesh m;
        m.vertices = std::move(merged.vertices);
        m.indices  = std::move(merged.indices);
        total_verts += m.vertices.size();
        total_tris += m.indices.size();
        meshes[static_cast<size_t>(mi)] = std::move(m);
    }

    spdlog::info("BuildVectorMeshes: {} meshes, total vertices={}, triangles={}", meshes.size(),
                 total_verts, total_tris);
    return meshes;
}

} // namespace ChromaPrint3D
