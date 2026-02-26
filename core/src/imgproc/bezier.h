#pragma once

#include "chromaprint3d/vec2.h"

#include <vector>

namespace ChromaPrint3D::detail {

/// Flatten a cubic bezier segment [p0, p1, p2, p3] into a polyline.
/// Appends points to `out` (does NOT add p0 — caller is responsible for the start point).
void FlattenCubicBezier(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3, float tolerance,
                        std::vector<Vec2f>& out);

} // namespace ChromaPrint3D::detail
