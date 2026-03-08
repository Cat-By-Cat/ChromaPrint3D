#include "chromaprint3d/vector_mesh.h"
#include "chromaprint3d/vector_recipe_map.h"
#include "chromaprint3d/error.h"
#include "triangulate.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace ChromaPrint3D {

namespace {

constexpr float kVertexWeldEps = 1e-5f;
constexpr float kWallWeldEps   = 1e-5f;
constexpr float kTriArea2Eps   = 1e-12f;

inline int64_t QuantizeCoord(float v, float eps) {
    return static_cast<int64_t>(std::llround(static_cast<double>(v) / static_cast<double>(eps)));
}

struct QuantizedVec3Key {
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;

    bool operator==(const QuantizedVec3Key& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct QuantizedVec3KeyHash {
    size_t operator()(const QuantizedVec3Key& v) const {
        size_t h = std::hash<int64_t>{}(v.x);
        h ^= std::hash<int64_t>{}(v.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(v.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct FaceKey {
    int a = 0;
    int b = 0;
    int c = 0;

    bool operator==(const FaceKey& o) const { return a == o.a && b == o.b && c == o.c; }
};

struct FaceKeyHash {
    size_t operator()(const FaceKey& f) const {
        size_t h = std::hash<int>{}(f.a);
        h ^= std::hash<int>{}(f.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(f.c) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct WallKey {
    int64_t ax = 0;
    int64_t ay = 0;
    int64_t bx = 0;
    int64_t by = 0;
    int64_t z0 = 0;
    int64_t z1 = 0;

    bool operator==(const WallKey& o) const {
        return ax == o.ax && ay == o.ay && bx == o.bx && by == o.by && z0 == o.z0 && z1 == o.z1;
    }
};

struct WallKeyHash {
    size_t operator()(const WallKey& k) const {
        size_t h = std::hash<int64_t>{}(k.ax);
        h ^= std::hash<int64_t>{}(k.ay) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(k.bx) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(k.by) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(k.z0) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int64_t>{}(k.z1) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

WallKey MakeWallKey(const Vec2f& p0, const Vec2f& p1, float z_bottom, float z_top) {
    WallKey key;
    int64_t x0 = QuantizeCoord(p0.x, kWallWeldEps);
    int64_t y0 = QuantizeCoord(p0.y, kWallWeldEps);
    int64_t x1 = QuantizeCoord(p1.x, kWallWeldEps);
    int64_t y1 = QuantizeCoord(p1.y, kWallWeldEps);

    bool swap = (x1 < x0) || (x1 == x0 && y1 < y0);
    if (swap) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    key.ax = x0;
    key.ay = y0;
    key.bx = x1;
    key.by = y1;
    key.z0 = QuantizeCoord(z_bottom, kWallWeldEps);
    key.z1 = QuantizeCoord(z_top, kWallWeldEps);
    if (key.z1 < key.z0) std::swap(key.z0, key.z1);
    return key;
}

FaceKey MakeFaceKey(int i0, int i1, int i2) {
    std::array<int, 3> ids{i0, i1, i2};
    std::sort(ids.begin(), ids.end());
    return {ids[0], ids[1], ids[2]};
}

bool IsDegenerateTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    Vec3f ab = b - a;
    Vec3f ac = c - a;
    float cx = ab.y * ac.z - ab.z * ac.y;
    float cy = ab.z * ac.x - ab.x * ac.z;
    float cz = ab.x * ac.y - ab.y * ac.x;
    return (cx * cx + cy * cy + cz * cz) <= kTriArea2Eps;
}

struct WallRecord {
    Vec2f p0;
    Vec2f p1;
    float z_bottom  = 0.0f;
    float z_top     = 0.0f;
    int occurrences = 0;
};

struct TriangulationCacheEntry {
    std::vector<Vec2f> points_2d;
    std::vector<Vec3i> triangles;
};

struct MeshAccumulator {
    std::vector<Vec3f> vertices;
    std::vector<Vec3i> indices;
    std::unordered_map<QuantizedVec3Key, int, QuantizedVec3KeyHash> vertex_map;
    std::unordered_set<FaceKey, FaceKeyHash> face_set;
    std::unordered_map<WallKey, WallRecord, WallKeyHash> wall_records;
    bool walls_finalized = false;

    QuantizedVec3Key VertexKey(const Vec3f& v) const {
        return {QuantizeCoord(v.x, kVertexWeldEps), QuantizeCoord(v.y, kVertexWeldEps),
                QuantizeCoord(v.z, kVertexWeldEps)};
    }

    int AddVertex(const Vec3f& v) {
        auto key = VertexKey(v);
        auto it  = vertex_map.find(key);
        if (it != vertex_map.end()) { return it->second; }
        int idx = static_cast<int>(vertices.size());
        vertices.push_back(v);
        vertex_map.emplace(key, idx);
        return idx;
    }

    void AddTriangle(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
        if (IsDegenerateTriangle(a, b, c)) return;
        int i0 = AddVertex(a);
        int i1 = AddVertex(b);
        int i2 = AddVertex(c);
        if (i0 == i1 || i1 == i2 || i0 == i2) { return; }
        FaceKey key = MakeFaceKey(i0, i1, i2);
        if (!face_set.insert(key).second) return;
        indices.emplace_back(i0, i1, i2);
    }

    void RecordWallEdge(const Vec2f& p0, const Vec2f& p1, float z_bottom, float z_top) {
        if (std::fabs(p0.x - p1.x) <= kWallWeldEps && std::fabs(p0.y - p1.y) <= kWallWeldEps) {
            return;
        }
        WallKey key  = MakeWallKey(p0, p1, z_bottom, z_top);
        auto& record = wall_records[key];
        if (record.occurrences == 0) {
            record.p0       = p0;
            record.p1       = p1;
            record.z_bottom = z_bottom;
            record.z_top    = z_top;
        }
        ++record.occurrences;
    }

    void FinalizeWalls() {
        if (walls_finalized) return;
        walls_finalized = true;
        for (const auto& [_, record] : wall_records) {
            if (record.occurrences != 1) continue;
            Vec3f bl{record.p0.x, record.p0.y, record.z_bottom};
            Vec3f br{record.p1.x, record.p1.y, record.z_bottom};
            Vec3f tl{record.p0.x, record.p0.y, record.z_top};
            Vec3f tr{record.p1.x, record.p1.y, record.z_top};
            AddTriangle(bl, br, tr);
            AddTriangle(bl, tr, tl);
        }
        wall_records.clear();
    }

    void ExtrudeShape(const VectorShape& shape, const TriangulationCacheEntry& cache,
                      float z_bottom, float z_top) {
        walls_finalized = false;
        if (cache.triangles.empty()) { return; }

        // Top face
        for (const Vec3i& tri : cache.triangles) {
            Vec3f a{cache.points_2d[static_cast<size_t>(tri.x)].x,
                    cache.points_2d[static_cast<size_t>(tri.x)].y, z_top};
            Vec3f b{cache.points_2d[static_cast<size_t>(tri.y)].x,
                    cache.points_2d[static_cast<size_t>(tri.y)].y, z_top};
            Vec3f c{cache.points_2d[static_cast<size_t>(tri.z)].x,
                    cache.points_2d[static_cast<size_t>(tri.z)].y, z_top};
            AddTriangle(a, b, c);
        }

        // Bottom face (reversed winding)
        for (const Vec3i& tri : cache.triangles) {
            Vec3f a{cache.points_2d[static_cast<size_t>(tri.x)].x,
                    cache.points_2d[static_cast<size_t>(tri.x)].y, z_bottom};
            Vec3f b{cache.points_2d[static_cast<size_t>(tri.y)].x,
                    cache.points_2d[static_cast<size_t>(tri.y)].y, z_bottom};
            Vec3f c{cache.points_2d[static_cast<size_t>(tri.z)].x,
                    cache.points_2d[static_cast<size_t>(tri.z)].y, z_bottom};
            AddTriangle(a, c, b);
        }

        // Side walls for each contour edge
        for (const Contour& contour : shape.contours) {
            size_t n = contour.size();
            if (n < 3) { continue; }
            for (size_t i = 0; i < n; ++i) {
                size_t j = (i + 1) % n;
                RecordWallEdge(contour[i], contour[j], z_bottom, z_top);
            }
        }
    }

    Mesh ToMesh() {
        FinalizeWalls();
        Mesh m;
        m.vertices = std::move(vertices);
        m.indices  = std::move(indices);
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

    const int base_layers = cfg.base_layers;
    const int base_start  = cfg.double_sided ? color_layers : 0;
    const float half_gap  = cfg.base_color_gap_mm * 0.5f;

    const bool has_base  = base_layers > 0;
    const int mesh_count = num_channels + (has_base ? 1 : 0);

    std::vector<TriangulationCacheEntry> triangulation_cache(shapes.size());
    std::vector<int> tri_shape_indices;
    tri_shape_indices.reserve(recipe_map.entries.size());
    std::vector<uint8_t> tri_marker(shapes.size(), 0);
    for (const auto& entry : recipe_map.entries) {
        if (entry.shape_idx < 0 || entry.shape_idx >= static_cast<int>(shapes.size())) { continue; }
        const size_t idx = static_cast<size_t>(entry.shape_idx);
        if (shapes[idx].contours.empty() || tri_marker[idx] != 0) { continue; }
        tri_marker[idx] = 1;
        tri_shape_indices.push_back(entry.shape_idx);
    }

#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 0; i < static_cast<int>(tri_shape_indices.size()); ++i) {
        const size_t shape_idx = static_cast<size_t>(tri_shape_indices[static_cast<size_t>(i)]);
        detail::TriangulateShape(shapes[shape_idx], triangulation_cache[shape_idx].points_2d,
                                 triangulation_cache[shape_idx].triangles);
    }

    std::vector<MeshAccumulator> accumulators(static_cast<size_t>(mesh_count));
    std::atomic<bool> has_extrude_error{false};
    std::string extrude_error_message;
#ifdef _OPENMP
#    pragma omp parallel for schedule(static)
#endif
    for (int mi = 0; mi < mesh_count; ++mi) {
        if (has_extrude_error.load(std::memory_order_relaxed)) { continue; }

        try {
            MeshAccumulator& accumulator = accumulators[static_cast<size_t>(mi)];
            const bool is_base_mesh      = has_base && (mi == num_channels);
            const int channel_idx        = mi;

            for (const auto& entry : recipe_map.entries) {
                if (entry.shape_idx < 0 || entry.shape_idx >= static_cast<int>(shapes.size())) {
                    continue;
                }
                const size_t shape_idx   = static_cast<size_t>(entry.shape_idx);
                const VectorShape& shape = shapes[shape_idx];
                if (shape.contours.empty()) { continue; }
                const TriangulationCacheEntry& tri_cache = triangulation_cache[shape_idx];

                if (is_base_mesh) {
                    float z_bot = static_cast<float>(base_start) * lh;
                    float z_top = static_cast<float>(base_start + base_layers) * lh;
                    if (half_gap > 0.0f) {
                        if (cfg.double_sided) { z_bot += half_gap; }
                        z_top -= half_gap;
                    }
                    accumulator.ExtrudeShape(shape, tri_cache, z_bot, z_top);
                    continue;
                }

                int run_start = -1;
                for (int layer = 0; layer < color_layers; ++layer) {
                    bool in_ch =
                        (layer < static_cast<int>(entry.recipe.size()) &&
                         static_cast<int>(entry.recipe[static_cast<size_t>(layer)]) == channel_idx);
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
                        if (half_gap > 0.0f && stored_start == base_start + base_layers) {
                            z_bot += half_gap;
                        }
                        accumulator.ExtrudeShape(shape, tri_cache, z_bot, z_top);

                        if (cfg.double_sided) {
                            const int mirror_start = (base_start - 1) - mapped_end;
                            const int mirror_end   = (base_start - 1) - mapped_start;
                            float mirror_bot       = static_cast<float>(mirror_start) * lh;
                            float mirror_top       = static_cast<float>(mirror_end + 1) * lh;
                            if (half_gap > 0.0f && mirror_end + 1 == base_start) {
                                mirror_top -= half_gap;
                            }
                            accumulator.ExtrudeShape(shape, tri_cache, mirror_bot, mirror_top);
                        }
                        run_start = -1;
                    }
                }
            }
        } catch (const std::exception& e) {
#ifdef _OPENMP
#    pragma omp critical(vector_mesh_extrude_error)
#endif
            {
                if (!has_extrude_error.load(std::memory_order_relaxed)) {
                    extrude_error_message = e.what();
                    has_extrude_error.store(true, std::memory_order_relaxed);
                }
            }
        } catch (...) {
#ifdef _OPENMP
#    pragma omp critical(vector_mesh_extrude_error)
#endif
            {
                if (!has_extrude_error.load(std::memory_order_relaxed)) {
                    extrude_error_message = "Unknown error while building vector mesh";
                    has_extrude_error.store(true, std::memory_order_relaxed);
                }
            }
        }
    }

    if (has_extrude_error.load(std::memory_order_relaxed)) {
        throw InputError(extrude_error_message.empty() ? "BuildVectorMeshes failed"
                                                       : extrude_error_message);
    }

    std::vector<Mesh> meshes(static_cast<size_t>(mesh_count));
    size_t total_verts = 0, total_tris = 0;

    for (int mi = 0; mi < mesh_count; ++mi) {
        Mesh m = accumulators[static_cast<size_t>(mi)].ToMesh();
        total_verts += m.vertices.size();
        total_tris += m.indices.size();
        meshes[static_cast<size_t>(mi)] = std::move(m);
    }

    spdlog::info("BuildVectorMeshes: {} meshes, total vertices={}, triangles={}", meshes.size(),
                 total_verts, total_tris);
    return meshes;
}

} // namespace ChromaPrint3D
