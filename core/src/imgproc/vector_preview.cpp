#include "chromaprint3d/vector_preview.h"
#include "chromaprint3d/color.h"
#include "chromaprint3d/encoding.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ChromaPrint3D {

namespace {

void FillShapeOnImage(cv::Mat& img, const VectorShape& shape, const cv::Scalar& color, float ox,
                      float oy, float scale) {
    for (const Contour& contour : shape.contours) {
        if (contour.size() < 3) { continue; }

        std::vector<cv::Point> pts;
        pts.reserve(contour.size());
        for (const Vec2f& p : contour) {
            pts.emplace_back(static_cast<int>(std::round((p.x - ox) * scale)),
                             static_cast<int>(std::round((p.y - oy) * scale)));
        }

        const cv::Point* pt_arrays[] = {pts.data()};
        int npts[]                   = {static_cast<int>(pts.size())};
        cv::fillPoly(img, pt_arrays, npts, 1, color);
    }
}

} // namespace

cv::Mat RenderVectorPreview(const VectorProcResult& result, const VectorRecipeMap& recipe_map,
                            const std::vector<Channel>& /*palette*/, float pixels_per_mm) {
    if (result.shapes.empty() || pixels_per_mm <= 0.0f) { return {}; }

    int w = std::max(1, static_cast<int>(std::ceil(result.width_mm * pixels_per_mm)));
    int h = std::max(1, static_cast<int>(std::ceil(result.height_mm * pixels_per_mm)));

    cv::Mat img(h, w, CV_8UC3, cv::Scalar(255, 255, 255));

    std::unordered_map<int, const VectorRecipeMap::ShapeEntry*> entry_map;
    for (const auto& entry : recipe_map.entries) { entry_map[entry.shape_idx] = &entry; }

    for (size_t i = 0; i < result.shapes.size(); ++i) {
        const VectorShape& shape = result.shapes[i];

        cv::Scalar color(200, 200, 200);

        auto it = entry_map.find(static_cast<int>(i));
        if (it != entry_map.end()) {
            Lab lab = it->second->matched_color;
            Rgb rgb = lab.ToRgb();
            int br  = static_cast<int>(std::clamp(LinearToSrgb(rgb.r()) * 255.0f, 0.0f, 255.0f));
            int bg  = static_cast<int>(std::clamp(LinearToSrgb(rgb.g()) * 255.0f, 0.0f, 255.0f));
            int bb  = static_cast<int>(std::clamp(LinearToSrgb(rgb.b()) * 255.0f, 0.0f, 255.0f));
            color   = cv::Scalar(bb, bg, br);
        }

        FillShapeOnImage(img, shape, color, 0.0f, 0.0f, pixels_per_mm);
    }

    if (result.y_flipped) { cv::flip(img, img, 0); }

    return img;
}

std::vector<uint8_t> RenderVectorPreviewPng(const VectorProcResult& result,
                                            const VectorRecipeMap& recipe_map,
                                            const std::vector<Channel>& palette,
                                            float pixels_per_mm) {
    cv::Mat img = RenderVectorPreview(result, recipe_map, palette, pixels_per_mm);
    if (img.empty()) { return {}; }
    return EncodePng(img);
}

} // namespace ChromaPrint3D
