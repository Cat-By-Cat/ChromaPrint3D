#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/color.h"
#include "chromaprint3d/error.h"
#include "bezier.h"
#include "occlusion.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <omp.h>

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>

namespace ChromaPrint3D {

namespace {

static Rgb NsvgColorToRgb(unsigned int color) {
    float r = static_cast<float>((color >> 0) & 0xFF) / 255.0f;
    float g = static_cast<float>((color >> 8) & 0xFF) / 255.0f;
    float b = static_cast<float>((color >> 16) & 0xFF) / 255.0f;
    return Rgb{SrgbToLinear(r), SrgbToLinear(g), SrgbToLinear(b)};
}

static GradientInfo ExtractGradient(const NSVGgradient* grad, const NSVGshape* shape) {
    GradientInfo info;
    if (!grad) { return info; }
    info.stops.reserve(static_cast<size_t>(grad->nstops));
    for (int i = 0; i < grad->nstops; ++i) {
        info.stops.push_back({grad->stops[i].offset, NsvgColorToRgb(grad->stops[i].color)});
    }

    float bounds_cx = (shape->bounds[0] + shape->bounds[2]) * 0.5f;
    float bounds_cy = (shape->bounds[1] + shape->bounds[3]) * 0.5f;
    float bounds_w  = shape->bounds[2] - shape->bounds[0];
    float bounds_h  = shape->bounds[3] - shape->bounds[1];

    info.x1 = bounds_cx;
    info.y1 = bounds_cy;
    info.x2 = bounds_cx + bounds_w * 0.5f;
    info.y2 = bounds_cy;
    info.r  = std::max(bounds_w, bounds_h) * 0.5f;
    return info;
}

static VectorShape ConvertShape(const NSVGshape* nshape, float tolerance) {
    VectorShape vs;

    if (nshape->fill.type == NSVG_PAINT_COLOR) {
        vs.fill_type  = FillType::Solid;
        vs.fill_color = NsvgColorToRgb(nshape->fill.color);
    } else if (nshape->fill.type == NSVG_PAINT_LINEAR_GRADIENT) {
        vs.fill_type = FillType::LinearGradient;
        vs.gradient  = ExtractGradient(nshape->fill.gradient, nshape);
    } else if (nshape->fill.type == NSVG_PAINT_RADIAL_GRADIENT) {
        vs.fill_type = FillType::RadialGradient;
        vs.gradient  = ExtractGradient(nshape->fill.gradient, nshape);
    } else {
        return vs;
    }

    vs.opacity = nshape->opacity;

    for (const NSVGpath* path = nshape->paths; path != nullptr; path = path->next) {
        if (path->npts < 4) { continue; }

        Contour contour;
        contour.push_back({path->pts[0], path->pts[1]});

        for (int i = 0; i < path->npts - 1; i += 3) {
            const float* p = &path->pts[i * 2];
            Vec2f p0{p[0], p[1]};
            Vec2f p1{p[2], p[3]};
            Vec2f p2{p[4], p[5]};
            Vec2f p3{p[6], p[7]};
            detail::FlattenCubicBezier(p0, p1, p2, p3, tolerance, contour);
        }

        if (path->closed && contour.size() >= 3) {
            if (contour.front() == contour.back()) { contour.pop_back(); }
        }

        if (contour.size() >= 3) { vs.contours.push_back(std::move(contour)); }
    }

    return vs;
}

static std::string PathStem(const std::string& path) {
    if (path.empty()) { return {}; }
    return std::filesystem::path(path).stem().string();
}

} // namespace

VectorProc::VectorProc(const VectorProcConfig& config) : config_(config) {}

VectorProcResult VectorProc::Run(const std::string& svg_path) const {
    spdlog::info("VectorProc: loading SVG from file: {}", svg_path);

    NSVGimage* image = nsvgParseFromFile(svg_path.c_str(), "mm", 96.0f);
    if (!image) { throw IOError("Failed to parse SVG: " + svg_path); }

    float svg_w = image->width;
    float svg_h = image->height;
    spdlog::info("VectorProc: SVG size {:.2f} x {:.2f} mm", svg_w, svg_h);

    float scale = 1.0f;
    if (config_.target_width_mm > 0.0f && svg_w > 0.0f) {
        scale = std::min(scale, config_.target_width_mm / svg_w);
    }
    if (config_.target_height_mm > 0.0f && svg_h > 0.0f) {
        scale = std::min(scale, config_.target_height_mm / svg_h);
    }

    float tol = config_.tessellation_tolerance_mm / scale;

    VectorImage vimg;
    vimg.width_mm  = svg_w * scale;
    vimg.height_mm = svg_h * scale;

    // Collect visible NSVGshapes into a vector for parallel processing
    std::vector<const NSVGshape*> nshapes;
    for (const NSVGshape* nshape = image->shapes; nshape != nullptr; nshape = nshape->next) {
        if (!(nshape->flags & NSVG_FLAGS_VISIBLE)) { continue; }
        if (nshape->fill.type == NSVG_PAINT_NONE) { continue; }
        nshapes.push_back(nshape);
    }

    std::vector<VectorShape> converted(nshapes.size());
    const float height_mm = vimg.height_mm;
    const bool do_flip    = config_.flip_y;

#pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < nshapes.size(); ++i) {
        VectorShape vs = ConvertShape(nshapes[i], tol);

        if (scale != 1.0f) {
            for (Contour& c : vs.contours) {
                for (Vec2f& p : c) {
                    p.x *= scale;
                    p.y *= scale;
                }
            }
        }
        if (do_flip) {
            for (Contour& c : vs.contours) {
                for (Vec2f& p : c) { p.y = height_mm - p.y; }
            }
        }
        converted[i] = std::move(vs);
    }

    nsvgDelete(image);

    for (auto& vs : converted) {
        if (!vs.contours.empty()) { vimg.shapes.push_back(std::move(vs)); }
    }

    spdlog::info("VectorProc: extracted {} shapes", vimg.shapes.size());

    std::vector<VectorShape> clipped = detail::ClipOcclusion(vimg.shapes);

    VectorProcResult result;
    result.name      = PathStem(svg_path);
    result.width_mm  = vimg.width_mm;
    result.height_mm = vimg.height_mm;
    result.shapes    = std::move(clipped);
    spdlog::info("VectorProc: output {} clipped shapes, {:.2f} x {:.2f} mm", result.shapes.size(),
                 result.width_mm, result.height_mm);
    return result;
}

VectorProcResult VectorProc::RunFromBuffer(const std::vector<uint8_t>& buffer,
                                           const std::string& name) const {
    if (buffer.empty()) { throw InputError("VectorProc::RunFromBuffer: buffer is empty"); }

    std::string data(buffer.begin(), buffer.end());
    NSVGimage* image = nsvgParse(data.data(), "mm", 96.0f);
    if (!image) { throw IOError("VectorProc: failed to parse SVG from buffer"); }

    float svg_w = image->width;
    float svg_h = image->height;

    float scale = 1.0f;
    if (config_.target_width_mm > 0.0f && svg_w > 0.0f) {
        scale = std::min(scale, config_.target_width_mm / svg_w);
    }
    if (config_.target_height_mm > 0.0f && svg_h > 0.0f) {
        scale = std::min(scale, config_.target_height_mm / svg_h);
    }

    float tol = config_.tessellation_tolerance_mm / scale;

    VectorImage vimg;
    vimg.width_mm  = svg_w * scale;
    vimg.height_mm = svg_h * scale;

    std::vector<const NSVGshape*> nshapes;
    for (const NSVGshape* nshape = image->shapes; nshape != nullptr; nshape = nshape->next) {
        if (!(nshape->flags & NSVG_FLAGS_VISIBLE)) { continue; }
        if (nshape->fill.type == NSVG_PAINT_NONE) { continue; }
        nshapes.push_back(nshape);
    }

    std::vector<VectorShape> converted(nshapes.size());
    const float height_mm = vimg.height_mm;
    const bool do_flip    = config_.flip_y;

#pragma omp parallel for schedule(dynamic)
    for (size_t i = 0; i < nshapes.size(); ++i) {
        VectorShape vs = ConvertShape(nshapes[i], tol);

        if (scale != 1.0f) {
            for (Contour& c : vs.contours) {
                for (Vec2f& p : c) {
                    p.x *= scale;
                    p.y *= scale;
                }
            }
        }
        if (do_flip) {
            for (Contour& c : vs.contours) {
                for (Vec2f& p : c) { p.y = height_mm - p.y; }
            }
        }
        converted[i] = std::move(vs);
    }

    nsvgDelete(image);

    for (auto& vs : converted) {
        if (!vs.contours.empty()) { vimg.shapes.push_back(std::move(vs)); }
    }

    std::vector<VectorShape> clipped = detail::ClipOcclusion(vimg.shapes);

    VectorProcResult result;
    result.name      = name;
    result.width_mm  = vimg.width_mm;
    result.height_mm = vimg.height_mm;
    result.shapes    = std::move(clipped);
    return result;
}

} // namespace ChromaPrint3D
