#pragma once

/// \file curve_fit.h
/// \brief Curve fitting: Potrace-style alpha corner detection + Bezier smoothing,
///        curve optimization, and Schneider recursive fallback.

#include "bezier.h"

#include <vector>

namespace ChromaPrint3D::detail {

// ── Potrace-style fitting ───────────────────────────────────────────────────

/// Fit a smooth curve to adjusted polygon vertices using Potrace's method.
///
/// For each vertex, computes the alpha parameter to decide whether to produce
/// a smooth Bezier curve or a sharp corner. Adjacent mid-points are connected
/// by Bezier segments with control points determined by alpha.
///
/// \param vertices   Adjusted polygon vertex positions (from AdjustVertices).
/// \param alpha_max  Corner threshold: alpha > alpha_max => corner.
///                   Default 1.0. Set to 0 for all corners, >4/3 for all smooth.
/// \return           Vector of CurveSegments forming the closed contour.
std::vector<CurveSegment> FitPotraceCurve(const std::vector<Vec2f>& vertices,
                                          float alpha_max = 1.0f);

/// Optimize a curve by merging adjacent same-direction Bezier segments.
/// Based on Potrace paper Section 2.4. Can reduce segment count by ~40%.
///
/// \param segments       Input segments from FitPotraceCurve.
/// \param opt_tolerance  Maximum deviation for merging (default 0.2 pixels).
/// \return               Optimized segment list.
std::vector<CurveSegment> OptimizeCurve(const std::vector<CurveSegment>& segments,
                                        float opt_tolerance = 0.2f);

// ── Schneider recursive fitting ─────────────────────────────────────────────

/// Detect corners in a point sequence based on angle threshold.
///
/// \param points           Ordered polyline points.
/// \param angle_threshold  Angle in degrees; sharper turns become corners.
/// \param window           Number of neighbors used for tangent estimation.
/// \return                 Sorted indices of detected corners.
std::vector<int> DetectCorners(const std::vector<Vec2f>& points, float angle_threshold = 135.0f,
                               int window = 3);

/// Fit cubic Bezier curves to a polyline using Schneider's recursive algorithm.
///
/// \param points     Polyline points.
/// \param corners    Corner indices (from DetectCorners). First and last are
///                   always treated as endpoints.
/// \param tolerance  Maximum allowed fitting error in pixels.
/// \return           Fitted Bezier contour.
BezierContour FitSchneider(const std::vector<Vec2f>& points, const std::vector<int>& corners,
                           float tolerance = 2.0f);

// ── Conversion helpers ──────────────────────────────────────────────────────

/// Convert CurveSegments to a BezierContour (expanding corners to two segments).
BezierContour SegmentsToBezierContour(const std::vector<CurveSegment>& segments);

} // namespace ChromaPrint3D::detail
