#pragma once

/// \file topology_repair.h
/// \brief Topology cleanup for traced polygon groups.

#include "imgproc/potrace_adapter.h"

#include <vector>

namespace ChromaPrint3D::detail {

/// Clean traced polygons with boolean operations and rebuild outer/hole hierarchy.
std::vector<TracedPolygonGroup> RepairTopology(const std::vector<TracedPolygonGroup>& groups,
                                               float simplify_epsilon, float min_outer_area,
                                               float min_hole_area);

} // namespace ChromaPrint3D::detail
