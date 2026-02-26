#include "bezier.h"

#include <cmath>

namespace ChromaPrint3D::detail {

void FlattenCubicBezier(Vec2f p0, Vec2f p1, Vec2f p2, Vec2f p3, float tolerance,
                        std::vector<Vec2f>& out) {
    // Flatness test: max deviation of control points from the baseline p0→p3.
    float dx = p3.x - p0.x;
    float dy = p3.y - p0.y;
    float d2 = std::abs((p1.x - p3.x) * dy - (p1.y - p3.y) * dx);
    float d3 = std::abs((p2.x - p3.x) * dy - (p2.y - p3.y) * dx);

    float len_sq = dx * dx + dy * dy;
    float tol_sq = tolerance * tolerance * len_sq;

    if ((d2 + d3) * (d2 + d3) <= tol_sq) {
        out.push_back(p3);
        return;
    }

    // De Casteljau subdivision at t = 0.5
    Vec2f p01  = (p0 + p1) * 0.5f;
    Vec2f p12  = (p1 + p2) * 0.5f;
    Vec2f p23  = (p2 + p3) * 0.5f;
    Vec2f p012 = (p01 + p12) * 0.5f;
    Vec2f p123 = (p12 + p23) * 0.5f;
    Vec2f mid  = (p012 + p123) * 0.5f;

    FlattenCubicBezier(p0, p01, p012, mid, tolerance, out);
    FlattenCubicBezier(mid, p123, p23, p3, tolerance, out);
}

} // namespace ChromaPrint3D::detail
