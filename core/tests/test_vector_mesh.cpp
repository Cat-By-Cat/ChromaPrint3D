#include <gtest/gtest.h>

#include "chromaprint3d/vector_mesh.h"
#include "chromaprint3d/vector_recipe_map.h"

#include <algorithm>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace ChromaPrint3D;

namespace {

Contour MakeRect(float x0, float y0, float x1, float y1) {
    return Contour{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

VectorRecipeMap BuildSingleChannelRecipeMap(int shape_count) {
    VectorRecipeMap map;
    map.color_layers = 1;
    map.num_channels = 1;
    map.layer_order  = LayerOrder::Top2Bottom;
    map.entries.reserve(static_cast<size_t>(shape_count));
    for (int i = 0; i < shape_count; ++i) {
        VectorRecipeMap::ShapeEntry entry;
        entry.shape_idx = i;
        entry.recipe    = {0};
        map.entries.push_back(std::move(entry));
    }
    return map;
}

struct EdgeKey {
    int a = 0;
    int b = 0;

    bool operator==(const EdgeKey& o) const { return a == o.a && b == o.b; }
};

struct EdgeKeyHash {
    size_t operator()(const EdgeKey& e) const {
        size_t h = std::hash<int>{}(e.a);
        h ^= std::hash<int>{}(e.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
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

EdgeKey MakeEdgeKey(int a, int b) {
    if (b < a) std::swap(a, b);
    return {a, b};
}

FaceKey MakeFaceKey(int i0, int i1, int i2) {
    std::array<int, 3> ids{i0, i1, i2};
    std::sort(ids.begin(), ids.end());
    return {ids[0], ids[1], ids[2]};
}

bool IsDegenerateByArea(const Mesh& mesh, const Vec3i& tri) {
    const Vec3f& a = mesh.vertices[static_cast<size_t>(tri.x)];
    const Vec3f& b = mesh.vertices[static_cast<size_t>(tri.y)];
    const Vec3f& c = mesh.vertices[static_cast<size_t>(tri.z)];
    Vec3f ab       = b - a;
    Vec3f ac       = c - a;
    float cx       = ab.y * ac.z - ab.z * ac.y;
    float cy       = ab.z * ac.x - ab.x * ac.z;
    float cz       = ab.x * ac.y - ab.y * ac.x;
    return (cx * cx + cy * cy + cz * cz) <= 1e-12f;
}

struct MeshTopologyMetrics {
    size_t non_manifold_edges   = 0;
    size_t duplicate_faces      = 0;
    size_t degenerate_triangles = 0;
};

MeshTopologyMetrics AnalyzeMesh(const Mesh& mesh) {
    MeshTopologyMetrics m;
    std::unordered_map<EdgeKey, int, EdgeKeyHash> edge_use;
    std::unordered_set<FaceKey, FaceKeyHash> faces;
    edge_use.reserve(mesh.indices.size() * 3);
    faces.reserve(mesh.indices.size() * 2);

    const int max_idx = static_cast<int>(mesh.vertices.size());
    for (const Vec3i& tri : mesh.indices) {
        if (tri.x < 0 || tri.y < 0 || tri.z < 0 || tri.x >= max_idx || tri.y >= max_idx ||
            tri.z >= max_idx || tri.x == tri.y || tri.y == tri.z || tri.x == tri.z ||
            IsDegenerateByArea(mesh, tri)) {
            ++m.degenerate_triangles;
            continue;
        }

        FaceKey fk = MakeFaceKey(tri.x, tri.y, tri.z);
        if (!faces.insert(fk).second) { ++m.duplicate_faces; }

        ++edge_use[MakeEdgeKey(tri.x, tri.y)];
        ++edge_use[MakeEdgeKey(tri.y, tri.z)];
        ++edge_use[MakeEdgeKey(tri.z, tri.x)];
    }

    for (const auto& [_, count] : edge_use) {
        if (count != 2) ++m.non_manifold_edges;
    }
    return m;
}

} // namespace

TEST(VectorMesh, SingleRectangleIsWatertight) {
    VectorShape shape;
    shape.contours.push_back(MakeRect(0.0f, 0.0f, 10.0f, 10.0f));

    std::vector<VectorShape> shapes{shape};
    VectorRecipeMap map = BuildSingleChannelRecipeMap(1);

    VectorMeshConfig cfg;
    cfg.layer_height_mm = 0.2f;

    std::vector<Mesh> meshes = BuildVectorMeshes(shapes, map, cfg);
    ASSERT_EQ(meshes.size(), 1u);
    ASSERT_FALSE(meshes[0].indices.empty());

    MeshTopologyMetrics metrics = AnalyzeMesh(meshes[0]);
    EXPECT_EQ(metrics.non_manifold_edges, 0u);
    EXPECT_EQ(metrics.duplicate_faces, 0u);
    EXPECT_EQ(metrics.degenerate_triangles, 0u);
}

TEST(VectorMesh, AdjacentRectanglesDoNotGenerateInternalWalls) {
    VectorShape left;
    left.contours.push_back(MakeRect(0.0f, 0.0f, 10.0f, 10.0f));
    VectorShape right;
    right.contours.push_back(MakeRect(10.0f, 0.0f, 20.0f, 10.0f));

    std::vector<VectorShape> shapes{left, right};
    VectorRecipeMap map = BuildSingleChannelRecipeMap(2);

    VectorMeshConfig cfg;
    cfg.layer_height_mm = 0.2f;

    std::vector<Mesh> meshes = BuildVectorMeshes(shapes, map, cfg);
    ASSERT_EQ(meshes.size(), 1u);
    ASSERT_FALSE(meshes[0].indices.empty());

    MeshTopologyMetrics metrics = AnalyzeMesh(meshes[0]);
    EXPECT_EQ(metrics.non_manifold_edges, 0u);
    EXPECT_EQ(metrics.duplicate_faces, 0u);
    EXPECT_EQ(metrics.degenerate_triangles, 0u);
    EXPECT_LT(meshes[0].indices.size(), 24u);
}
