#pragma once

/// \file contour_assembly.h
/// \brief Assemble closed contours per label from a BoundaryGraph, with hole hierarchy.

#include "imgproc/bezier.h"
#include "imgproc/boundary_graph.h"
#include "imgproc/curve_fitting.h"
#include "imgproc/svg_writer.h"

#include <vector>

namespace ChromaPrint3D::detail {

/// Assemble VectorizedShapes from a BoundaryGraph using polyline (degenerate Bezier) segments.
///
/// For each label, collects all boundary edges, chains them into closed contours,
/// determines outer/hole hierarchy by signed area, and packages them as VectorizedShape.
///
/// \param graph         The shared boundary graph.
/// \param num_labels    Total number of labels (0-based).
/// \param palette       Color palette indexed by label.
/// \param min_contour_area  Minimum absolute area to keep a contour.
/// \param min_hole_area     Minimum absolute area to keep a hole.
/// \return  One VectorizedShape per label that has valid contours.
std::vector<VectorizedShape> AssembleContoursFromGraph(const BoundaryGraph& graph, int num_labels,
                                                       const std::vector<Rgb>& palette,
                                                       float min_contour_area, float min_hole_area,
                                                       const CurveFitConfig* fit_cfg = nullptr);

} // namespace ChromaPrint3D::detail
