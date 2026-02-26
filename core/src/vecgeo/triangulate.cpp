#include "triangulate.h"

#include <earcut/earcut.hpp>

#include <array>

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

void TriangulateShape(const VectorShape& shape, std::vector<Vec2f>& out_vertices,
                      std::vector<Vec3i>& out_indices) {
    if (shape.contours.empty()) { return; }

    std::vector<std::vector<Vec2f>> polygon;
    polygon.reserve(shape.contours.size());
    size_t total_pts = 0;
    for (const Contour& c : shape.contours) {
        if (c.size() < 3) { continue; }
        polygon.push_back(c);
        total_pts += c.size();
    }
    if (polygon.empty()) { return; }

    std::vector<uint32_t> indices = mapbox::earcut<uint32_t>(polygon);

    size_t base = out_vertices.size();
    out_vertices.reserve(base + total_pts);
    for (const auto& ring : polygon) {
        for (const Vec2f& p : ring) { out_vertices.push_back(p); }
    }

    size_t tri_count = indices.size() / 3;
    out_indices.reserve(out_indices.size() + tri_count);
    for (size_t i = 0; i < indices.size(); i += 3) {
        out_indices.emplace_back(static_cast<int>(base + indices[i]),
                                 static_cast<int>(base + indices[i + 1]),
                                 static_cast<int>(base + indices[i + 2]));
    }
}

} // namespace ChromaPrint3D::detail
