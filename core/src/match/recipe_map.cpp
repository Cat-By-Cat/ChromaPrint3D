#include "chromaprint3d/recipe_map.h"
#include "chromaprint3d/color.h"
#include "chromaprint3d/color_db.h"
#include "chromaprint3d/error.h"
#include "detail/layer_preview_color.h"

#include <opencv2/imgproc.hpp>

#include <cstddef>
#include <cstdint>

namespace ChromaPrint3D {

const uint8_t* RecipeMap::RecipeAt(int r, int c) const {
    if (r < 0 || c < 0 || r >= height || c >= width || color_layers <= 0) { return nullptr; }
    const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
    const size_t offset = idx * static_cast<size_t>(color_layers);
    if (offset + static_cast<size_t>(color_layers) > recipes.size()) { return nullptr; }
    return &recipes[offset];
}

const uint8_t* RecipeMap::MaskAt(int r, int c) const {
    if (r < 0 || c < 0 || r >= height || c >= width) { return nullptr; }
    const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
    if (idx >= mask.size()) { return nullptr; }
    return &mask[idx];
}

const Lab RecipeMap::ColorAt(int r, int c) const {
    if (r < 0 || c < 0 || r >= height || c >= width) { return Lab(); }
    const size_t idx = static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
    if (idx >= mapped_color.size()) { return Lab(); }
    return mapped_color[idx];
}

cv::Mat RecipeMap::ToSourceMaskImage() const {
    if (width <= 0 || height <= 0) { return cv::Mat(); }
    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (source_mask.size() < pixel_count) { return cv::Mat(); }

    cv::Mat out(height, width, CV_8UC1, cv::Scalar(0));
    for (int r = 0; r < height; ++r) {
        uint8_t* row = out.ptr<uint8_t>(r);
        for (int c = 0; c < width; ++c) {
            const size_t idx =
                static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
            row[c] = (source_mask[idx] != 0) ? static_cast<uint8_t>(255) : static_cast<uint8_t>(0);
        }
    }
    return out;
}

cv::Mat RecipeMap::ToBgrImage(uint8_t background_b, uint8_t background_g,
                              uint8_t background_r) const {
    if (width <= 0 || height <= 0) { return cv::Mat(); }

    cv::Mat bgra = ToBgraImage();
    if (bgra.empty()) { return cv::Mat(); }

    cv::Mat bgr(height, width, CV_8UC3, cv::Scalar(background_b, background_g, background_r));
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            const cv::Vec4b px = bgra.at<cv::Vec4b>(r, c);
            if (px[3] == 0) { continue; }
            bgr.at<cv::Vec3b>(r, c) = cv::Vec3b(px[0], px[1], px[2]);
        }
    }
    return bgr;
}

cv::Mat RecipeMap::ToBgraImage() const {
    if (width <= 0 || height <= 0) { return cv::Mat(); }

    const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (mapped_color.size() < pixel_count) { throw InputError("mapped_color size mismatch"); }
    if (!mask.empty() && mask.size() < pixel_count) { throw InputError("mask size mismatch"); }

    cv::Mat bgra(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            const size_t idx =
                static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
            if (!mask.empty() && mask[idx] == 0) { continue; }

            Rgb rgb    = mapped_color[idx].ToRgb();
            uint8_t r8 = 0, g8 = 0, b8 = 0;
            rgb.ToRgb255(r8, g8, b8);
            bgra.at<cv::Vec4b>(r, c) = cv::Vec4b(b8, g8, r8, 255);
        }
    }
    return bgra;
}

cv::Mat RecipeMap::ToLayerBgrImage(int layer_idx, const std::vector<Channel>& palette,
                                   uint8_t background_b, uint8_t background_g,
                                   uint8_t background_r) const {
    cv::Mat bgra = ToLayerBgraImage(layer_idx, palette);
    if (bgra.empty()) { return cv::Mat(); }

    cv::Mat bgr(height, width, CV_8UC3, cv::Scalar(background_b, background_g, background_r));
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            const size_t idx =
                static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
            if (!mask.empty() && mask[idx] == 0) { continue; }

            const cv::Vec4b px = bgra.at<cv::Vec4b>(r, c);
            if (px[3] == 0) { continue; }
            bgr.at<cv::Vec3b>(r, c) = cv::Vec3b(px[0], px[1], px[2]);
        }
    }
    return bgr;
}

cv::Mat RecipeMap::ToLayerBgraImage(int layer_idx, const std::vector<Channel>& palette) const {
    if (width <= 0 || height <= 0 || color_layers <= 0) return cv::Mat();
    if (layer_idx < 0 || layer_idx >= color_layers) return cv::Mat();

    const size_t pixel_count          = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t expected_recipe_size = pixel_count * static_cast<size_t>(color_layers);
    if (recipes.size() < expected_recipe_size) { throw InputError("recipes size mismatch"); }
    if (!mask.empty() && mask.size() < pixel_count) { throw InputError("mask size mismatch"); }

    cv::Mat bgra(height, width, CV_8UC4, cv::Scalar(0, 0, 0, 0));
    const size_t layer_offset = static_cast<size_t>(layer_idx);
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            const size_t idx =
                static_cast<size_t>(r) * static_cast<size_t>(width) + static_cast<size_t>(c);
            if (!mask.empty() && mask[idx] == 0) { continue; }

            const size_t recipe_offset = idx * static_cast<size_t>(color_layers) + layer_offset;
            if (recipe_offset >= recipes.size()) { continue; }
            const uint8_t channel_idx = recipes[recipe_offset];

            cv::Vec3b color(127, 127, 127);
            if (channel_idx < palette.size()) {
                color = detail::PaletteColorLiteralToBgr(palette[channel_idx].color);
            }
            bgra.at<cv::Vec4b>(r, c) = cv::Vec4b(color[0], color[1], color[2], 255);
        }
    }
    return bgra;
}

} // namespace ChromaPrint3D
