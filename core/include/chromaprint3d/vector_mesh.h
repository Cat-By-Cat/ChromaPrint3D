#pragma once

/// \file vector_mesh.h
/// \brief Build 3D meshes from vector shapes via triangulation and extrusion.

#include "mesh.h"
#include "vector_image.h"

#include <vector>

namespace ChromaPrint3D {

struct VectorRecipeMap;

/// Configuration for vector mesh building.
struct VectorMeshConfig {
    float layer_height_mm = 0.08f;
    int base_layers       = 0;
    bool double_sided     = false;
};

/// Build per-channel 3D meshes from a vector recipe map and its shapes.
/// Returns meshes in channel order [ch0, ch1, ..., chN-1, base(optional)].
std::vector<Mesh> BuildVectorMeshes(const std::vector<VectorShape>& shapes,
                                    const VectorRecipeMap& recipe_map,
                                    const VectorMeshConfig& cfg = {});

} // namespace ChromaPrint3D
