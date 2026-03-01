#include <gtest/gtest.h>

#include "chromaprint3d/color_db.h"
#include "chromaprint3d/encoding.h"
#include "chromaprint3d/recipe_map.h"
#include "chromaprint3d/vector_preview.h"
#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/vector_recipe_map.h"

#include <opencv2/imgcodecs.hpp>

using namespace ChromaPrint3D;

TEST(LayerPreview, RasterLayerPreviewRendersExpectedChannelsAndPng) {
    RecipeMap map;
    map.width        = 2;
    map.height       = 1;
    map.color_layers = 2;
    map.num_channels = 2;
    map.recipes      = {0, 1, 1, 0};
    map.mask         = {255, 255};

    std::vector<Channel> palette = {
        Channel{"Red", "PLA"},
        Channel{"Blue", "PLA"},
    };

    cv::Mat layer0 = map.ToLayerBgrImage(0, palette, 255, 255, 255);
    ASSERT_FALSE(layer0.empty());
    ASSERT_EQ(layer0.rows, 1);
    ASSERT_EQ(layer0.cols, 2);
    EXPECT_EQ(layer0.at<cv::Vec3b>(0, 0), cv::Vec3b(0, 0, 255));
    EXPECT_EQ(layer0.at<cv::Vec3b>(0, 1), cv::Vec3b(255, 0, 0));

    cv::Mat layer1 = map.ToLayerBgrImage(1, palette, 255, 255, 255);
    ASSERT_FALSE(layer1.empty());
    EXPECT_EQ(layer1.at<cv::Vec3b>(0, 0), cv::Vec3b(255, 0, 0));
    EXPECT_EQ(layer1.at<cv::Vec3b>(0, 1), cv::Vec3b(0, 0, 255));

    std::vector<uint8_t> layer0_png = EncodePng(layer0);
    EXPECT_FALSE(layer0_png.empty());
}

TEST(LayerPreview, VectorLayerPreviewPngsContainAllLayers) {
    VectorProcResult vector_result;
    vector_result.width_mm  = 10.0f;
    vector_result.height_mm = 10.0f;

    VectorShape shape;
    shape.fill_type = FillType::Solid;
    shape.contours.push_back(
        {Vec2f(0.0f, 0.0f), Vec2f(10.0f, 0.0f), Vec2f(10.0f, 10.0f), Vec2f(0.0f, 10.0f)});
    vector_result.shapes.push_back(shape);

    VectorRecipeMap recipe_map;
    recipe_map.color_layers = 2;
    recipe_map.num_channels = 2;
    recipe_map.entries.push_back(VectorRecipeMap::ShapeEntry{
        0, std::vector<uint8_t>{0, 1}, Lab::FromRgb(Rgb::FromRgb255(255, 255, 255)), false});

    std::vector<Channel> palette = {
        Channel{"Green", "PLA"},
        Channel{"Magenta", "PLA"},
    };

    std::vector<std::vector<uint8_t>> pngs =
        RenderVectorLayerPreviewPngs(vector_result, recipe_map, palette, 1.0f);
    ASSERT_EQ(pngs.size(), 2u);
    EXPECT_FALSE(pngs[0].empty());
    EXPECT_FALSE(pngs[1].empty());

    cv::Mat layer0 = cv::imdecode(pngs[0], cv::IMREAD_COLOR);
    cv::Mat layer1 = cv::imdecode(pngs[1], cv::IMREAD_COLOR);
    ASSERT_FALSE(layer0.empty());
    ASSERT_FALSE(layer1.empty());
    EXPECT_EQ(layer0.rows, 10);
    EXPECT_EQ(layer0.cols, 10);
    EXPECT_EQ(layer1.rows, 10);
    EXPECT_EQ(layer1.cols, 10);

    EXPECT_EQ(layer0.at<cv::Vec3b>(5, 5), cv::Vec3b(0, 255, 0));
    EXPECT_EQ(layer1.at<cv::Vec3b>(5, 5), cv::Vec3b(255, 0, 255));
}
