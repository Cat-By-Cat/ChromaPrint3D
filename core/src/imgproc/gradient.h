#pragma once

#include "chromaprint3d/vector_image.h"
#include "chromaprint3d/raster_proc.h"

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

/// Rasterize a gradient-filled shape into a local bitmap.
/// Returns an OpenCV Mat (CV_32FC3, linear RGB) covering the shape's bounding box.
/// `out_offset_x` and `out_offset_y` receive the bounding box origin in mm.
cv::Mat RasterizeGradientShape(const VectorShape& shape, float pixel_mm, float& out_offset_x,
                               float& out_offset_y);

} // namespace ChromaPrint3D::detail
