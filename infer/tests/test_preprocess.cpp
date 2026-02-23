#include <chromaprint3d/infer/preprocess.h>
#include <chromaprint3d/infer/error.h>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <gtest/gtest.h>

#include <cmath>

using namespace ChromaPrint3D::infer;

TEST(PreprocessTest, BasicRGBNCHW) {
    cv::Mat bgr(100, 200, CV_8UC3, cv::Scalar(50, 100, 150));

    PreprocessConfig config;
    config.target_width  = 200;
    config.target_height = 100;
    config.channel_order = ChannelOrder::kRGB;
    config.layout = LayoutOrder::kNCHW;
    config.normalize = true;
    config.mean = {0.0f, 0.0f, 0.0f};
    config.std  = {1.0f, 1.0f, 1.0f};
    config.scale = 1.0f / 255.0f;

    PreprocessMeta meta;
    Tensor t = PreprocessImage(bgr, config, &meta);

    EXPECT_EQ(t.DType(), DataType::kFloat32);
    EXPECT_EQ(t.NDim(), 4);
    EXPECT_EQ(t.Shape(0), 1);  // N
    EXPECT_EQ(t.Shape(1), 3);  // C
    EXPECT_EQ(t.Shape(2), 100); // H
    EXPECT_EQ(t.Shape(3), 200); // W

    EXPECT_EQ(meta.original_width, 200);
    EXPECT_EQ(meta.original_height, 100);

    // Check first pixel of R channel (BGR 50,100,150 → RGB 150,100,50)
    const float* data = t.DataAs<float>();
    float r_val = data[0]; // first pixel of channel 0 (R)
    EXPECT_NEAR(r_val, 150.0f / 255.0f, 1e-4f);
}

TEST(PreprocessTest, NHWCLayout) {
    cv::Mat bgr(50, 60, CV_8UC3, cv::Scalar(255, 0, 0));

    PreprocessConfig config;
    config.channel_order = ChannelOrder::kBGR;
    config.layout = LayoutOrder::kNHWC;
    config.normalize = false;

    Tensor t = PreprocessImage(bgr, config);

    EXPECT_EQ(t.NDim(), 4);
    EXPECT_EQ(t.Shape(0), 1);
    EXPECT_EQ(t.Shape(1), 50);
    EXPECT_EQ(t.Shape(2), 60);
    EXPECT_EQ(t.Shape(3), 3);
}

TEST(PreprocessTest, ResizeWithPadding) {
    cv::Mat bgr(100, 200, CV_8UC3, cv::Scalar(128, 128, 128));

    PreprocessConfig config;
    config.target_width  = 256;
    config.target_height = 256;
    config.keep_aspect_ratio = true;
    config.pad_value = 0;
    config.normalize = false;

    PreprocessMeta meta;
    Tensor t = PreprocessImage(bgr, config, &meta);

    EXPECT_EQ(t.Shape(2), 256);
    EXPECT_EQ(t.Shape(3), 256);

    EXPECT_EQ(meta.original_width, 200);
    EXPECT_EQ(meta.original_height, 100);
    EXPECT_GT(meta.pad_top, 0); // vertical padding since image is wider
}

TEST(PreprocessTest, ResizeWithoutPadding) {
    cv::Mat bgr(100, 200, CV_8UC3, cv::Scalar(128, 128, 128));

    PreprocessConfig config;
    config.target_width  = 256;
    config.target_height = 256;
    config.keep_aspect_ratio = false;
    config.normalize = false;

    PreprocessMeta meta;
    Tensor t = PreprocessImage(bgr, config, &meta);

    EXPECT_EQ(t.Shape(2), 256);
    EXPECT_EQ(t.Shape(3), 256);
    EXPECT_EQ(meta.pad_top, 0);
    EXPECT_EQ(meta.pad_left, 0);
}

TEST(PreprocessTest, Normalization) {
    // Solid white image
    cv::Mat bgr(10, 10, CV_8UC3, cv::Scalar(255, 255, 255));

    PreprocessConfig config;
    config.channel_order = ChannelOrder::kRGB;
    config.layout = LayoutOrder::kNCHW;
    config.normalize = true;
    config.scale = 1.0f / 255.0f;
    config.mean = {0.485f, 0.456f, 0.406f};
    config.std  = {0.229f, 0.224f, 0.225f};

    Tensor t = PreprocessImage(bgr, config);
    const float* data = t.DataAs<float>();

    // R channel first pixel: (1.0 - 0.485) / 0.229 ≈ 2.2489
    float expected_r = (1.0f - 0.485f) / 0.229f;
    EXPECT_NEAR(data[0], expected_r, 0.01f);
}

TEST(PreprocessTest, GrayscaleInput) {
    cv::Mat gray(50, 50, CV_8UC1, cv::Scalar(128));

    PreprocessConfig config;
    config.normalize = false;

    Tensor t = PreprocessImage(gray, config);
    EXPECT_EQ(t.Shape(1), 3); // should be converted to 3 channels
}

TEST(PreprocessTest, RGBAInput) {
    cv::Mat rgba(30, 40, CV_8UC4, cv::Scalar(100, 150, 200, 255));

    PreprocessConfig config;
    config.normalize = false;

    Tensor t = PreprocessImage(rgba, config);
    EXPECT_EQ(t.Shape(1), 3); // alpha dropped
}

TEST(PreprocessTest, EmptyImageThrows) {
    cv::Mat empty;
    PreprocessConfig config;
    EXPECT_THROW(PreprocessImage(empty, config), InputError);
}

TEST(PreprocessTest, PostprocessMaskBasic) {
    // Simulate a model output: 1x1x10x20 mask
    Tensor mask = Tensor::Allocate(DataType::kFloat32, {1, 1, 10, 20});
    float* data = mask.DataAs<float>();
    for (int i = 0; i < 200; ++i) data[i] = 0.8f; // above threshold

    PreprocessMeta meta;
    meta.original_width  = 200;
    meta.original_height = 100;
    meta.padded_width    = 20;
    meta.padded_height   = 10;
    meta.pad_left = 0;
    meta.pad_top  = 0;
    meta.scale_x  = 20.0f / 200.0f;
    meta.scale_y  = 10.0f / 100.0f;

    cv::Mat result = PostprocessMask(mask, meta, 0.5f);

    EXPECT_EQ(result.cols, 200);
    EXPECT_EQ(result.rows, 100);
    EXPECT_EQ(result.type(), CV_8UC1);
    EXPECT_EQ(result.at<uint8_t>(0, 0), 255);
}

TEST(PreprocessTest, PostprocessMaskEmpty) {
    Tensor empty;
    PreprocessMeta meta;
    EXPECT_THROW(PostprocessMask(empty, meta), InputError);
}
