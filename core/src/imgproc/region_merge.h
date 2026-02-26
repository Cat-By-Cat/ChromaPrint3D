#pragma once

/// \file region_merge.h
/// \brief Region merging for post-quantization simplification.
/// Based on the Area criterion from He, Kang, Morel (arXiv:2409.15940).

#include <opencv2/core.hpp>

#include <vector>

namespace ChromaPrint3D::detail {

/// Merge small / similar adjacent connected components after K-Means quantization.
///
/// \param labels  Input/output CV_32SC1 label map (modified in-place).
///                Each pixel holds its region id.
/// \param image   The original image in Lab float32 (CV_32FC3) for color comparison.
/// \param lambda  Merge threshold — larger values merge more aggressively.
///                Merge condition: min(area1, area2) * ||avg1 - avg2||^2 < lambda
/// \param min_area  Regions smaller than this are force-merged to their most similar neighbor.
/// \return        Number of regions remaining after merging.
int MergeRegions(cv::Mat& labels, const cv::Mat& image, double lambda, int min_area);

} // namespace ChromaPrint3D::detail
