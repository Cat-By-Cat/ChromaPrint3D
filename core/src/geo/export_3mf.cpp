#include "chromaprint3d/voxel.h"
#include "chromaprint3d/mesh.h"
#include "chromaprint3d/export_3mf.h"
#include "chromaprint3d/error.h"

#include "lib3mf_implicit.hpp"

#include <spdlog/spdlog.h>

#include <limits>
#include <algorithm>
#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ChromaPrint3D {
namespace {

struct FaceKey {
    Lib3MF_uint32 a = 0;
    Lib3MF_uint32 b = 0;
    Lib3MF_uint32 c = 0;

    bool operator==(const FaceKey& o) const { return a == o.a && b == o.b && c == o.c; }
};

struct FaceKeyHash {
    size_t operator()(const FaceKey& f) const {
        size_t h = std::hash<Lib3MF_uint32>{}(f.a);
        h ^= std::hash<Lib3MF_uint32>{}(f.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<Lib3MF_uint32>{}(f.c) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct EdgeKey {
    Lib3MF_uint32 a = 0;
    Lib3MF_uint32 b = 0;

    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& e) const {
        size_t h = std::hash<Lib3MF_uint32>{}(e.a);
        h ^= std::hash<Lib3MF_uint32>{}(e.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct MeshQualityMetrics {
    size_t input_triangles      = 0;
    size_t output_triangles     = 0;
    size_t degenerate_triangles = 0;
    size_t duplicate_faces      = 0;
    size_t non_manifold_edges   = 0;
    bool is_watertight          = false;
};

static FaceKey MakeFaceKey(Lib3MF_uint32 i0, Lib3MF_uint32 i1, Lib3MF_uint32 i2) {
    std::array<Lib3MF_uint32, 3> ids{i0, i1, i2};
    std::sort(ids.begin(), ids.end());
    return {ids[0], ids[1], ids[2]};
}

static EdgeKey MakeEdgeKey(Lib3MF_uint32 a, Lib3MF_uint32 b) {
    if (b < a) std::swap(a, b);
    return {a, b};
}

static bool IsDegenerateTriangleByArea(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    Vec3f ab = b - a;
    Vec3f ac = c - a;
    float cx = ab.y * ac.z - ab.z * ac.y;
    float cy = ab.z * ac.x - ab.x * ac.z;
    float cz = ab.x * ac.y - ab.y * ac.x;
    return (cx * cx + cy * cy + cz * cz) <= 1e-12f;
}

static Lib3MF_uint32 ToIndex(int idx, std::size_t vertex_count) {
    if (idx < 0) { throw InputError("Mesh index is negative"); }
    const std::size_t uidx = static_cast<std::size_t>(idx);
    if (uidx >= vertex_count) { throw InputError("Mesh index out of range"); }
    if (uidx > static_cast<std::size_t>(std::numeric_limits<Lib3MF_uint32>::max())) {
        throw InputError("Mesh index exceeds lib3mf limit");
    }
    return static_cast<Lib3MF_uint32>(uidx);
}

static std::vector<sLib3MFTriangle>
BuildValidatedTriangles(const Mesh& mesh, std::size_t vertex_count, MeshQualityMetrics& metrics) {
    std::vector<sLib3MFTriangle> triangles;
    triangles.reserve(mesh.indices.size());

    std::unordered_set<FaceKey, FaceKeyHash> seen_faces;
    seen_faces.reserve(mesh.indices.size() * 2);

    for (const Vec3i& tri : mesh.indices) {
        ++metrics.input_triangles;
        if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) {
            ++metrics.degenerate_triangles;
            continue;
        }

        Lib3MF_uint32 i0 = ToIndex(tri.x, vertex_count);
        Lib3MF_uint32 i1 = ToIndex(tri.y, vertex_count);
        Lib3MF_uint32 i2 = ToIndex(tri.z, vertex_count);

        if (IsDegenerateTriangleByArea(mesh.vertices[static_cast<size_t>(i0)],
                                       mesh.vertices[static_cast<size_t>(i1)],
                                       mesh.vertices[static_cast<size_t>(i2)])) {
            ++metrics.degenerate_triangles;
            continue;
        }

        FaceKey face = MakeFaceKey(i0, i1, i2);
        if (!seen_faces.insert(face).second) {
            ++metrics.duplicate_faces;
            continue;
        }

        sLib3MFTriangle t{};
        t.m_Indices[0] = i0;
        t.m_Indices[1] = i1;
        t.m_Indices[2] = i2;
        triangles.push_back(t);
    }

    std::unordered_map<EdgeKey, size_t, EdgeKeyHash> edge_use_count;
    edge_use_count.reserve(triangles.size() * 3);
    for (const auto& tri : triangles) {
        Lib3MF_uint32 i0 = tri.m_Indices[0];
        Lib3MF_uint32 i1 = tri.m_Indices[1];
        Lib3MF_uint32 i2 = tri.m_Indices[2];
        ++edge_use_count[MakeEdgeKey(i0, i1)];
        ++edge_use_count[MakeEdgeKey(i1, i2)];
        ++edge_use_count[MakeEdgeKey(i2, i0)];
    }

    for (const auto& [_, use_count] : edge_use_count) {
        if (use_count != 2) ++metrics.non_manifold_edges;
    }
    metrics.is_watertight    = metrics.non_manifold_edges == 0;
    metrics.output_triangles = triangles.size();
    return triangles;
}

static std::vector<sLib3MFTriangle> BuildTrianglesFast(const Mesh& mesh, std::size_t vertex_count,
                                                       MeshQualityMetrics& metrics) {
    std::vector<sLib3MFTriangle> triangles;
    triangles.reserve(mesh.indices.size());

    for (const Vec3i& tri : mesh.indices) {
        ++metrics.input_triangles;
        if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) {
            ++metrics.degenerate_triangles;
            continue;
        }

        Lib3MF_uint32 i0 = ToIndex(tri.x, vertex_count);
        Lib3MF_uint32 i1 = ToIndex(tri.y, vertex_count);
        Lib3MF_uint32 i2 = ToIndex(tri.z, vertex_count);

        if (IsDegenerateTriangleByArea(mesh.vertices[static_cast<size_t>(i0)],
                                       mesh.vertices[static_cast<size_t>(i1)],
                                       mesh.vertices[static_cast<size_t>(i2)])) {
            ++metrics.degenerate_triangles;
            continue;
        }

        sLib3MFTriangle t{};
        t.m_Indices[0] = i0;
        t.m_Indices[1] = i1;
        t.m_Indices[2] = i2;
        triangles.push_back(t);
    }

    metrics.output_triangles = triangles.size();
    return triangles;
}

static sLib3MFPosition ToPosition(const Vec3f& v) {
    sLib3MFPosition pos{};
    pos.m_Coordinates[0] = v.x;
    pos.m_Coordinates[1] = v.y;
    pos.m_Coordinates[2] = v.z;
    return pos;
}

static std::string BuildObjectNameFromPalette(std::size_t idx, const std::vector<Channel>& palette,
                                              int base_channel_idx, int base_layers) {
    if (idx == palette.size() && base_layers > 0) {
        std::string name = "Base";
        if (base_channel_idx >= 0 && base_channel_idx < static_cast<int>(palette.size())) {
            const Channel& base_ch = palette[static_cast<size_t>(base_channel_idx)];
            if (!base_ch.material.empty() && base_ch.material != "Default Material") {
                name += " - " + base_ch.material;
            }
        }
        return name;
    }
    if (idx < palette.size()) {
        const Channel& ch = palette[idx];
        std::string name  = ch.color.empty() ? ("Channel " + std::to_string(idx)) : ch.color;
        if (!ch.material.empty() && ch.material != "Default Material") {
            name += " - " + ch.material;
        }
        return name;
    }
    return "Channel " + std::to_string(idx);
}

static std::string BuildObjectName(const ModelIR& model_ir, std::size_t idx) {
    return BuildObjectNameFromPalette(idx, model_ir.palette, model_ir.base_channel_idx,
                                      model_ir.base_layers);
}

static void AddMeshToModel(Lib3MF::PModel& model, Lib3MF::PWrapper& wrapper, const Mesh& mesh,
                           const std::string& name) {
    const std::size_t vertex_count = mesh.vertices.size();
    if (vertex_count > static_cast<std::size_t>(std::numeric_limits<Lib3MF_uint32>::max())) {
        throw InputError("Mesh vertex count exceeds lib3mf limit");
    }

    std::vector<sLib3MFPosition> vertices;
    vertices.reserve(vertex_count);
    for (const Vec3f& v : mesh.vertices) {
        if (!v.IsFinite()) { throw InputError("Mesh vertex is not finite"); }
        vertices.push_back(ToPosition(v));
    }

    MeshQualityMetrics metrics;
    std::vector<sLib3MFTriangle> triangles;
    const bool full_validation = spdlog::should_log(spdlog::level::debug);
    if (full_validation) {
        triangles = BuildValidatedTriangles(mesh, vertex_count, metrics);
        if (metrics.non_manifold_edges > 0 || metrics.duplicate_faces > 0 ||
            metrics.degenerate_triangles > 0) {
            spdlog::warn("Mesh quality '{}' : input_tris={}, output_tris={}, degenerate={}, "
                         "duplicate_faces={}, non_manifold_edges={}, watertight={}",
                         name, metrics.input_triangles, metrics.output_triangles,
                         metrics.degenerate_triangles, metrics.duplicate_faces,
                         metrics.non_manifold_edges, metrics.is_watertight);
        } else {
            spdlog::debug("Mesh quality '{}' : tris={}, watertight=true", name,
                          metrics.output_triangles);
        }
    } else {
        triangles = BuildTrianglesFast(mesh, vertex_count, metrics);
        if (metrics.degenerate_triangles > 0) {
            spdlog::warn("Mesh quality '{}' : dropped {} degenerate triangle(s)", name,
                         metrics.degenerate_triangles);
        }
    }

    Lib3MF::PMeshObject mesh_object = model->AddMeshObject();
    mesh_object->SetName(name);
    mesh_object->SetGeometry(vertices, triangles);
    model->AddBuildItem(mesh_object.get(), wrapper->GetIdentityTransform());
}

static void Export3mfInternal(const std::string& path, const ModelIR& model_ir,
                              const BuildMeshConfig& cfg) {
    if (path.empty()) { throw InputError("Export3mf path is empty"); }
    if (model_ir.voxel_grids.empty()) { throw InputError("ModelIR voxel_grids is empty"); }
    spdlog::info("Export3mf: exporting to file {}, {} grid(s)", path, model_ir.voxel_grids.size());

    const auto n = static_cast<int>(model_ir.voxel_grids.size());
    std::vector<Mesh> meshes(static_cast<std::size_t>(n));

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < n; ++i) {
        const VoxelGrid& grid = model_ir.voxel_grids[static_cast<std::size_t>(i)];
        if (grid.width <= 0 || grid.height <= 0 || grid.num_layers <= 0) { continue; }
        if (grid.ooc.empty()) { continue; }
        meshes[static_cast<std::size_t>(i)] = Mesh::Build(grid, cfg);
    }

    std::size_t total_verts = 0, total_tris = 0;
    for (const auto& m : meshes) {
        total_verts += m.vertices.size();
        total_tris += m.indices.size();
    }
    spdlog::info("Mesh::Build: {} grids, total vertices={}, triangles={}", n, total_verts,
                 total_tris);

    try {
        Lib3MF::PWrapper wrapper = Lib3MF::CWrapper::loadLibrary();
        Lib3MF::PModel model     = wrapper->CreateModel();

        std::size_t exported = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
            if (meshes[i].vertices.empty() || meshes[i].indices.empty()) { continue; }
            AddMeshToModel(model, wrapper, meshes[i], BuildObjectName(model_ir, i));
            ++exported;
        }

        if (exported == 0) { throw InputError("No geometry to export"); }

        Lib3MF::PWriter writer = model->QueryWriter("3mf");
        writer->WriteToFile(path);
        spdlog::info("Export3mf: written {} object(s) to {}", exported, path);
    } catch (const Lib3MF::ELib3MFException& e) { throw IOError(e.what()); }
}

} // namespace

void Export3mf(const std::string& path, const ModelIR& model_ir) {
    Export3mfInternal(path, model_ir, BuildMeshConfig{});
}

void Export3mf(const std::string& path, const ModelIR& model_ir, const BuildMeshConfig& cfg) {
    Export3mfInternal(path, model_ir, cfg);
}

std::vector<uint8_t> Export3mfToBuffer(const ModelIR& model_ir, const BuildMeshConfig& cfg) {
    if (model_ir.voxel_grids.empty()) { throw InputError("ModelIR voxel_grids is empty"); }
    spdlog::info("Export3mfToBuffer: exporting {} grid(s) to memory", model_ir.voxel_grids.size());

    const auto n = static_cast<int>(model_ir.voxel_grids.size());
    std::vector<Mesh> meshes(static_cast<std::size_t>(n));

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < n; ++i) {
        const VoxelGrid& grid = model_ir.voxel_grids[static_cast<std::size_t>(i)];
        if (grid.width <= 0 || grid.height <= 0 || grid.num_layers <= 0) { continue; }
        if (grid.ooc.empty()) { continue; }
        meshes[static_cast<std::size_t>(i)] = Mesh::Build(grid, cfg);
    }

    std::size_t total_verts = 0, total_tris = 0;
    for (const auto& m : meshes) {
        total_verts += m.vertices.size();
        total_tris += m.indices.size();
    }
    spdlog::info("Mesh::Build: {} grids, total vertices={}, triangles={}", n, total_verts,
                 total_tris);

    try {
        Lib3MF::PWrapper wrapper = Lib3MF::CWrapper::loadLibrary();
        Lib3MF::PModel model     = wrapper->CreateModel();

        std::size_t exported = 0;
        for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
            if (meshes[i].vertices.empty() || meshes[i].indices.empty()) { continue; }
            AddMeshToModel(model, wrapper, meshes[i], BuildObjectName(model_ir, i));
            ++exported;
        }

        if (exported == 0) { throw InputError("No geometry to export"); }

        Lib3MF::PWriter writer = model->QueryWriter("3mf");
        std::vector<uint8_t> buffer;
        writer->WriteToBuffer(buffer);
        spdlog::info("Export3mfToBuffer: written {} object(s), {} bytes", exported, buffer.size());
        return buffer;
    } catch (const Lib3MF::ELib3MFException& e) { throw IOError(e.what()); }
}

std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette, int base_channel_idx,
                                         int base_layers) {
    if (meshes.empty()) { throw InputError("meshes vector is empty"); }
    spdlog::info("Export3mfFromMeshes: exporting {} mesh(es) to memory", meshes.size());

    try {
        Lib3MF::PWrapper wrapper = Lib3MF::CWrapper::loadLibrary();
        Lib3MF::PModel model     = wrapper->CreateModel();

        std::size_t exported = 0;
        for (std::size_t i = 0; i < meshes.size(); ++i) {
            if (meshes[i].vertices.empty() || meshes[i].indices.empty()) { continue; }
            std::string name =
                BuildObjectNameFromPalette(i, palette, base_channel_idx, base_layers);
            AddMeshToModel(model, wrapper, meshes[i], name);
            ++exported;
        }

        if (exported == 0) { throw InputError("No geometry to export"); }

        Lib3MF::PWriter writer = model->QueryWriter("3mf");
        std::vector<uint8_t> buffer;
        writer->WriteToBuffer(buffer);
        spdlog::info("Export3mfFromMeshes: written {} object(s), {} bytes", exported,
                     buffer.size());
        return buffer;
    } catch (const Lib3MF::ELib3MFException& e) { throw IOError(e.what()); }
}

} // namespace ChromaPrint3D
