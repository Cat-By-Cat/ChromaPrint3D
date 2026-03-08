#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/color.h"
#include "chromaprint3d/error.h"
#include "bezier.h"
#include "occlusion.h"

#include <clipper2/clipper.h>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

#if defined(_OPENMP)
#    if defined(__has_include)
#        if __has_include(<omp.h>)
#            include <omp.h>
#        endif
#    else
#        include <omp.h>
#    endif
#endif

#define NANOSVG_IMPLEMENTATION
#include <nanosvg/nanosvg.h>

namespace ChromaPrint3D {

namespace {

constexpr double kClipperScale = 100000.0;

static Clipper2Lib::Path64 ContourToPath(const Contour& contour) {
    Clipper2Lib::Path64 path;
    path.reserve(contour.size());
    for (const Vec2f& p : contour) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        path.emplace_back(static_cast<int64_t>(std::llround(p.x * kClipperScale)),
                          static_cast<int64_t>(std::llround(p.y * kClipperScale)));
    }
    return path;
}

static Contour PathToContour(const Clipper2Lib::Path64& path) {
    Contour contour;
    contour.reserve(path.size());
    for (const auto& p : path) {
        contour.emplace_back(static_cast<float>(p.x / kClipperScale),
                             static_cast<float>(p.y / kClipperScale));
    }
    return contour;
}

static bool IsDegenerateContour(const Contour& contour, float min_area_mm2) {
    if (contour.size() < 3) return true;
    Clipper2Lib::Path64 path = ContourToPath(contour);
    if (path.size() < 3) return true;
    double area_mm2 = std::abs(Clipper2Lib::Area(path)) / (kClipperScale * kClipperScale);
    return area_mm2 < min_area_mm2;
}

static Clipper2Lib::FillRule ToClipperFillRule(SvgFillRule rule) {
    return (rule == SvgFillRule::EvenOdd) ? Clipper2Lib::FillRule::EvenOdd
                                          : Clipper2Lib::FillRule::NonZero;
}

static std::vector<Contour> NormalizeContours(const std::vector<Contour>& contours,
                                              float tolerance_mm, Clipper2Lib::FillRule fill_rule) {
    std::vector<Contour> normalized;
    if (contours.empty()) return normalized;

    double simplify_eps = std::max(100.0, static_cast<double>(tolerance_mm) * kClipperScale * 0.4);
    float min_area_mm2  = std::max(1e-5f, tolerance_mm * tolerance_mm * 0.01f);

    Clipper2Lib::Paths64 all_paths;
    all_paths.reserve(contours.size());
    for (const Contour& contour : contours) {
        Clipper2Lib::Path64 path = ContourToPath(contour);
        if (path.size() >= 3) { all_paths.push_back(std::move(path)); }
    }
    if (all_paths.empty()) return normalized;

    Clipper2Lib::Paths64 cleaned = Clipper2Lib::Union(all_paths, fill_rule);
    if (cleaned.empty()) return normalized;
    cleaned = Clipper2Lib::SimplifyPaths(cleaned, simplify_eps, true);

    for (auto& cpath : cleaned) {
        if (cpath.size() < 3) continue;
        Contour out = PathToContour(cpath);
        if (IsDegenerateContour(out, min_area_mm2)) continue;
        normalized.push_back(std::move(out));
    }

    std::sort(normalized.begin(), normalized.end(), [](const Contour& a, const Contour& b) {
        Clipper2Lib::Path64 pa = ContourToPath(a);
        Clipper2Lib::Path64 pb = ContourToPath(b);
        return std::abs(Clipper2Lib::Area(pa)) > std::abs(Clipper2Lib::Area(pb));
    });

    return normalized;
}

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
    vs.svg_fill_rule =
        (nshape->fillRule == NSVG_FILLRULE_EVENODD) ? SvgFillRule::EvenOdd : SvgFillRule::NonZero;

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

    vs.contours = NormalizeContours(vs.contours, tolerance, ToClipperFillRule(vs.svg_fill_rule));
    return vs;
}

static std::string PathStem(const std::string& path) {
    if (path.empty()) { return {}; }
    return std::filesystem::path(path).stem().string();
}

/// Compute the bounding box of all visible shapes from NanoSVG.
/// After nsvg__scaleToViewbox, shape bounds are in the output unit (mm).
static void ComputeShapeBounds(const NSVGimage* image, float& out_w, float& out_h) {
    float max_x = 0.0f;
    float max_y = 0.0f;
    for (const NSVGshape* s = image->shapes; s != nullptr; s = s->next) {
        if (!(s->flags & NSVG_FLAGS_VISIBLE)) { continue; }
        if (s->fill.type == NSVG_PAINT_NONE) { continue; }
        max_x = std::max(max_x, s->bounds[2]);
        max_y = std::max(max_y, s->bounds[3]);
    }
    out_w = max_x;
    out_h = max_y;
}

/// Common processing logic shared by Run() and RunFromBuffer().
static VectorProcResult ProcessParsedSvg(NSVGimage* image, const VectorProcConfig& config,
                                         const std::string& result_name) {
    // NanoSVG's image->width/height are in pixels (pre-unit-conversion), but path
    // coordinates are already in the output unit (mm) after nsvg__scaleToViewbox.
    // Compute actual physical dimensions from the shape bounding boxes.
    float svg_w = 0.0f;
    float svg_h = 0.0f;
    ComputeShapeBounds(image, svg_w, svg_h);

    if (svg_w <= 0.0f || svg_h <= 0.0f) {
        nsvgDelete(image);
        throw InputError("SVG has no visible content");
    }

    spdlog::info("VectorProc: SVG content bounds {:.2f} x {:.2f} mm", svg_w, svg_h);

    // Compute scale to fit target dimensions (allows both up-scaling and down-scaling).
    float scale = 1.0f;
    if (config.target_width_mm > 0.0f || config.target_height_mm > 0.0f) {
        float sw = (config.target_width_mm > 0.0f) ? config.target_width_mm / svg_w
                                                   : std::numeric_limits<float>::max();
        float sh = (config.target_height_mm > 0.0f) ? config.target_height_mm / svg_h
                                                    : std::numeric_limits<float>::max();
        scale    = std::min(sw, sh);
    }

    float tol = config.tessellation_tolerance_mm / std::max(scale, 0.001f);

    float final_w = svg_w * scale;
    float final_h = svg_h * scale;

    spdlog::info("VectorProc: scale={:.4f}, output {:.2f} x {:.2f} mm", scale, final_w, final_h);

    // Collect visible shapes for parallel processing
    std::vector<const NSVGshape*> nshapes;
    for (const NSVGshape* nshape = image->shapes; nshape != nullptr; nshape = nshape->next) {
        if (!(nshape->flags & NSVG_FLAGS_VISIBLE)) { continue; }
        if (nshape->fill.type == NSVG_PAINT_NONE) { continue; }
        nshapes.push_back(nshape);
    }

    std::vector<VectorShape> converted(nshapes.size());
    const bool do_flip               = config.flip_y;
    const std::ptrdiff_t shape_count = static_cast<std::ptrdiff_t>(nshapes.size());

#if defined(_OPENMP)
#    pragma omp parallel for schedule(dynamic)
#endif
    for (std::ptrdiff_t i = 0; i < shape_count; ++i) {
        const size_t idx = static_cast<size_t>(i);
        VectorShape vs   = ConvertShape(nshapes[idx], tol);

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
                for (Vec2f& p : c) { p.y = final_h - p.y; }
            }
        }
        converted[idx] = std::move(vs);
    }

    nsvgDelete(image);

    VectorImage vimg;
    vimg.width_mm  = final_w;
    vimg.height_mm = final_h;
    for (auto& vs : converted) {
        if (!vs.contours.empty()) { vimg.shapes.push_back(std::move(vs)); }
    }

    spdlog::info("VectorProc: extracted {} shapes", vimg.shapes.size());

    std::vector<VectorShape> clipped = detail::ClipOcclusion(vimg.shapes);
    for (auto& vs : clipped) {
        vs.contours = NormalizeContours(vs.contours, tol, ToClipperFillRule(vs.svg_fill_rule));
    }
    clipped.erase(std::remove_if(clipped.begin(), clipped.end(),
                                 [](const VectorShape& vs) { return vs.contours.empty(); }),
                  clipped.end());

    VectorProcResult result;
    result.name      = result_name;
    result.width_mm  = final_w;
    result.height_mm = final_h;
    result.y_flipped = config.flip_y;
    result.shapes    = std::move(clipped);
    spdlog::info("VectorProc: output {} clipped shapes, {:.2f} x {:.2f} mm", result.shapes.size(),
                 result.width_mm, result.height_mm);
    return result;
}

} // namespace

VectorProc::VectorProc(const VectorProcConfig& config) : config_(config) {}

VectorProcResult VectorProc::Run(const std::string& svg_path) const {
    spdlog::info("VectorProc: loading SVG from file: {}", svg_path);

    NSVGimage* image = nsvgParseFromFile(svg_path.c_str(), "mm", 96.0f);
    if (!image) { throw IOError("Failed to parse SVG: " + svg_path); }

    return ProcessParsedSvg(image, config_, PathStem(svg_path));
}

VectorProcResult VectorProc::RunFromBuffer(const std::vector<uint8_t>& buffer,
                                           const std::string& name) const {
    if (buffer.empty()) { throw InputError("VectorProc::RunFromBuffer: buffer is empty"); }

    std::string data(buffer.begin(), buffer.end());
    NSVGimage* image = nsvgParse(data.data(), "mm", 96.0f);
    if (!image) { throw IOError("VectorProc: failed to parse SVG from buffer"); }

    return ProcessParsedSvg(image, config_, name);
}

} // namespace ChromaPrint3D
