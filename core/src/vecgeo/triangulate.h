#pragma once

#include "chromaprint3d/vector_image.h"
#include "chromaprint3d/vec3.h"

#include <vector>

namespace ChromaPrint3D::detail {

/// Triangulate a 2D shape (outer contour + holes) into a list of triangle indices.
/// Returns indices into a flat point array built from all contours concatenated.
/// `out_vertices` receives the flat vertex list.
/// `out_indices` receives triplets of indices (i0, i1, i2).
void TriangulateShape(const VectorShape& shape, std::vector<Vec2f>& out_vertices,
                      std::vector<Vec3i>& out_indices);

} // namespace ChromaPrint3D::detail
