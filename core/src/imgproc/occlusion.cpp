#include "occlusion.h"

#include <clipper2/clipper.h>

#include <spdlog/spdlog.h>

#include <algorithm>

namespace ChromaPrint3D::detail {

namespace {

constexpr double kScale = 1000.0;

Clipper2Lib::Path64 ContourToPath(const Contour& c) {
    Clipper2Lib::Path64 path;
    path.reserve(c.size());
    for (const Vec2f& p : c) {
        path.emplace_back(static_cast<int64_t>(std::round(p.x * kScale)),
                          static_cast<int64_t>(std::round(p.y * kScale)));
    }
    return path;
}

Contour PathToContour(const Clipper2Lib::Path64& path) {
    Contour c;
    c.reserve(path.size());
    for (const auto& pt : path) {
        c.emplace_back(static_cast<float>(pt.x) / static_cast<float>(kScale),
                       static_cast<float>(pt.y) / static_cast<float>(kScale));
    }
    return c;
}

Clipper2Lib::Paths64 ShapeToPaths(const VectorShape& shape) {
    Clipper2Lib::Paths64 paths;
    paths.reserve(shape.contours.size());
    for (const Contour& c : shape.contours) {
        if (c.size() >= 3) { paths.push_back(ContourToPath(c)); }
    }
    return paths;
}

VectorShape PathsToShape(const Clipper2Lib::Paths64& paths, const VectorShape& original) {
    VectorShape result;
    result.fill_type  = original.fill_type;
    result.fill_color = original.fill_color;
    result.gradient   = original.gradient;
    result.opacity    = original.opacity;
    result.contours.reserve(paths.size());
    for (const auto& path : paths) {
        if (path.size() >= 3) { result.contours.push_back(PathToContour(path)); }
    }
    return result;
}

} // namespace

std::vector<VectorShape> ClipOcclusion(const std::vector<VectorShape>& shapes) {
    if (shapes.empty()) { return {}; }

    const int n = static_cast<int>(shapes.size());
    std::vector<VectorShape> result;
    result.reserve(static_cast<size_t>(n));

    Clipper2Lib::Paths64 accumulated;

    for (int i = n - 1; i >= 0; --i) {
        Clipper2Lib::Paths64 shape_paths = ShapeToPaths(shapes[static_cast<size_t>(i)]);
        if (shape_paths.empty()) { continue; }

        Clipper2Lib::Paths64 clipped;
        if (accumulated.empty()) {
            clipped = shape_paths;
        } else {
            clipped =
                Clipper2Lib::Difference(shape_paths, accumulated, Clipper2Lib::FillRule::NonZero);
        }

        if (!clipped.empty()) {
            result.push_back(PathsToShape(clipped, shapes[static_cast<size_t>(i)]));
        }

        accumulated = Clipper2Lib::Union(accumulated, shape_paths, Clipper2Lib::FillRule::NonZero);
    }

    std::reverse(result.begin(), result.end());
    spdlog::debug("ClipOcclusion: {} input shapes -> {} clipped shapes", n, result.size());
    return result;
}

} // namespace ChromaPrint3D::detail
