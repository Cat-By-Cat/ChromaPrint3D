#pragma once

/// \file vector_preview.h
/// \brief Generate preview images from vector processing results.

#include "vector_proc.h"
#include "vector_recipe_map.h"
#include "color_db.h"

#include <opencv2/core.hpp>

#include <cstdint>
#include <vector>

namespace ChromaPrint3D {

/// Render a vector preview image showing matched colors.
/// Returns a BGR image (CV_8UC3).
cv::Mat RenderVectorPreview(const VectorProcResult& result, const VectorRecipeMap& recipe_map,
                            const std::vector<Channel>& palette, float pixels_per_mm = 5.0f);

/// Encode the preview to PNG bytes.
std::vector<uint8_t> RenderVectorPreviewPng(const VectorProcResult& result,
                                            const VectorRecipeMap& recipe_map,
                                            const std::vector<Channel>& palette,
                                            float pixels_per_mm = 5.0f);

} // namespace ChromaPrint3D
