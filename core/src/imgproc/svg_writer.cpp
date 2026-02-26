#include "svg_writer.h"

#include <cstdio>
#include <sstream>

namespace ChromaPrint3D::detail {

namespace {

std::string FmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(v));
    // Strip trailing zeros after decimal point
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        size_t last = s.find_last_not_of('0');
        if (last != std::string::npos && s[last] == '.') last--;
        s.erase(last + 1);
    }
    return s;
}

std::string RgbToHex(const Rgb& c) {
    uint8_t r8, g8, b8;
    c.ToRgb255(r8, g8, b8);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", r8, g8, b8);
    return buf;
}

} // namespace

std::string BezierToSvgPath(const BezierContour& contour) {
    if (contour.segments.empty()) return "";

    std::ostringstream ss;
    ss << "M" << FmtFloat(contour.segments[0].p0.x) << "," << FmtFloat(contour.segments[0].p0.y);

    for (auto& seg : contour.segments) {
        ss << "C" << FmtFloat(seg.p1.x) << "," << FmtFloat(seg.p1.y) << " " << FmtFloat(seg.p2.x)
           << "," << FmtFloat(seg.p2.y) << " " << FmtFloat(seg.p3.x) << "," << FmtFloat(seg.p3.y);
    }

    if (contour.closed) ss << "Z";
    return ss.str();
}

std::string ContoursToSvgPath(const std::vector<BezierContour>& contours) {
    std::ostringstream ss;
    for (size_t ci = 0; ci < contours.size(); ++ci) {
        auto& contour = contours[ci];
        if (contour.segments.empty()) continue;

        if (ci == 0) {
            // Outer contour: normal winding
            ss << BezierToSvgPath(contour);
        } else {
            // Hole: reverse winding for evenodd fill-rule
            // Reverse by iterating segments backwards and swapping p0<->p3, p1<->p2
            auto& segs = contour.segments;
            int n      = static_cast<int>(segs.size());
            ss << "M" << FmtFloat(segs[n - 1].p3.x) << "," << FmtFloat(segs[n - 1].p3.y);
            for (int i = n - 1; i >= 0; --i) {
                ss << "C" << FmtFloat(segs[i].p2.x) << "," << FmtFloat(segs[i].p2.y) << " "
                   << FmtFloat(segs[i].p1.x) << "," << FmtFloat(segs[i].p1.y) << " "
                   << FmtFloat(segs[i].p0.x) << "," << FmtFloat(segs[i].p0.y);
            }
            ss << "Z";
        }
    }
    return ss.str();
}

std::string WriteSvg(const std::vector<VectorizedShape>& shapes, int width, int height) {
    std::ostringstream svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" " << "viewBox=\"0 0 " << width << " "
        << height << "\" " << "width=\"" << width << "\" height=\"" << height << "\">\n";

    for (auto& shape : shapes) {
        std::string d = ContoursToSvgPath(shape.contours);
        if (d.empty()) continue;

        svg << "  <path d=\"" << d << "\" " << "fill=\"" << RgbToHex(shape.color) << "\" "
            << "fill-rule=\"evenodd\" " << "stroke=\"none\"/>\n";
    }

    svg << "</svg>\n";
    return svg.str();
}

} // namespace ChromaPrint3D::detail
