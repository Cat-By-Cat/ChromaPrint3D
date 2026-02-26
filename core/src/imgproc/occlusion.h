#pragma once

#include "chromaprint3d/vector_image.h"

#include <vector>

namespace ChromaPrint3D::detail {

/// Clip shapes so that no two shapes overlap in XY space.
/// Input shapes are in draw order (last = topmost). The topmost shape has
/// priority: lower shapes are clipped where they overlap higher ones.
/// Returns the clipped shapes (same ordering, but geometries trimmed).
std::vector<VectorShape> ClipOcclusion(const std::vector<VectorShape>& shapes);

} // namespace ChromaPrint3D::detail
