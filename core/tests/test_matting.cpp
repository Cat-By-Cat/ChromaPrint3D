#include <gtest/gtest.h>
#include "chromaprint3d/matting.h"
#include "chromaprint3d/matting_postprocess.h"
#include "chromaprint3d/error.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <filesystem>
#include <fstream>

using namespace ChromaPrint3D;

// ── MattingModelConfig ──────────────────────────────────────────────────────

class MattingConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        tmp_dir_ = std::filesystem::temp_directory_path() / "chromaprint3d_test_matting";
        std::filesystem::create_directories(tmp_dir_);
    }

    void TearDown() override { std::filesystem::remove_all(tmp_dir_); }

    void WriteJson(const std::string& filename, const std::string& content) {
        std::ofstream out(tmp_dir_ / filename);
        out << content;
    }

    std::filesystem::path tmp_dir_;
};

TEST_F(MattingConfigTest, LoadFromJson) {
    WriteJson("test_model.json", R"({
        "name": "Test Model",
        "description": "A test model",
        "preprocess": {
            "input_size": 512,
            "channel_order": "bgr",
            "layout": "nhwc",
            "normalize_scale": 0.5,
            "normalize_mean": [0.1, 0.2, 0.3],
            "normalize_std": [0.4, 0.5, 0.6]
        },
        "postprocess": {
            "output_index": 1,
            "threshold": 0.7
        }
    })");

    auto cfg = MattingModelConfig::LoadFromJson((tmp_dir_ / "test_model.json").string());
    EXPECT_EQ(cfg.name, "Test Model");
    EXPECT_EQ(cfg.description, "A test model");
    EXPECT_EQ(cfg.input_width, 512);
    EXPECT_EQ(cfg.input_height, 512);
    EXPECT_EQ(cfg.channel_order, "bgr");
    EXPECT_EQ(cfg.layout, "nhwc");
    EXPECT_FLOAT_EQ(cfg.normalize_scale, 0.5f);
    EXPECT_FLOAT_EQ(cfg.normalize_mean[0], 0.1f);
    EXPECT_FLOAT_EQ(cfg.normalize_mean[1], 0.2f);
    EXPECT_FLOAT_EQ(cfg.normalize_mean[2], 0.3f);
    EXPECT_FLOAT_EQ(cfg.normalize_std[0], 0.4f);
    EXPECT_FLOAT_EQ(cfg.normalize_std[1], 0.5f);
    EXPECT_FLOAT_EQ(cfg.normalize_std[2], 0.6f);
    EXPECT_EQ(cfg.output_index, 1);
    EXPECT_FLOAT_EQ(cfg.threshold, 0.7f);
}

TEST_F(MattingConfigTest, LoadFromJsonNonSquare) {
    WriteJson("rect.json", R"({
        "preprocess": {
            "input_width": 640,
            "input_height": 480
        }
    })");

    auto cfg = MattingModelConfig::LoadFromJson((tmp_dir_ / "rect.json").string());
    EXPECT_EQ(cfg.input_width, 640);
    EXPECT_EQ(cfg.input_height, 480);
}

TEST_F(MattingConfigTest, LoadFromJsonWidthHeightOverrideSize) {
    WriteJson("override.json", R"({
        "preprocess": {
            "input_size": 256,
            "input_width": 640,
            "input_height": 480
        }
    })");

    auto cfg = MattingModelConfig::LoadFromJson((tmp_dir_ / "override.json").string());
    EXPECT_EQ(cfg.input_width, 640);
    EXPECT_EQ(cfg.input_height, 480);
}

TEST_F(MattingConfigTest, LoadFromJsonDefaults) {
    WriteJson("minimal.json", R"({})");

    auto cfg = MattingModelConfig::LoadFromJson((tmp_dir_ / "minimal.json").string());
    EXPECT_EQ(cfg.input_width, 320);
    EXPECT_EQ(cfg.input_height, 320);
    EXPECT_EQ(cfg.channel_order, "rgb");
    EXPECT_EQ(cfg.layout, "nchw");
    EXPECT_EQ(cfg.output_index, 0);
    EXPECT_FLOAT_EQ(cfg.threshold, 0.5f);
}

TEST_F(MattingConfigTest, LoadFromJsonMissingFile) {
    EXPECT_THROW(MattingModelConfig::LoadFromJson("/nonexistent/path.json"), IOError);
}

// ── ThresholdMattingProvider ────────────────────────────────────────────────

TEST(ThresholdMattingTest, Name) {
    ThresholdMattingProvider provider;
    EXPECT_EQ(provider.Name(), "opencv-threshold");
}

TEST(ThresholdMattingTest, EmptyInput) {
    ThresholdMattingProvider provider;
    auto output = provider.Run(cv::Mat());
    EXPECT_TRUE(output.mask.empty());
    EXPECT_TRUE(output.alpha.empty());
}

TEST(ThresholdMattingTest, BasicMask) {
    ThresholdMattingProvider provider(15.0f);

    cv::Mat bgr(100, 100, CV_8UC3, cv::Scalar(200, 200, 200));
    cv::rectangle(bgr, cv::Rect(30, 30, 40, 40), cv::Scalar(0, 0, 255), cv::FILLED);

    auto output = provider.Run(bgr);
    ASSERT_FALSE(output.mask.empty());
    EXPECT_TRUE(output.alpha.empty());
    EXPECT_EQ(output.mask.rows, 100);
    EXPECT_EQ(output.mask.cols, 100);
    EXPECT_EQ(output.mask.type(), CV_8UC1);

    EXPECT_EQ(output.mask.at<uint8_t>(0, 0), 0);
    EXPECT_EQ(output.mask.at<uint8_t>(50, 50), 255);
}

// ── MattingRegistry ─────────────────────────────────────────────────────────

TEST(MattingRegistryTest, RegisterAndGet) {
    MattingRegistry reg;
    auto provider = std::make_shared<ThresholdMattingProvider>();
    reg.Register("opencv", provider);

    EXPECT_TRUE(reg.Has("opencv"));
    EXPECT_FALSE(reg.Has("nonexistent"));
    EXPECT_NE(reg.Get("opencv"), nullptr);
    EXPECT_EQ(reg.Get("nonexistent"), nullptr);
}

TEST(MattingRegistryTest, GetShared) {
    MattingRegistry reg;
    auto provider = std::make_shared<ThresholdMattingProvider>();
    reg.Register("opencv", provider);

    auto shared = reg.GetShared("opencv");
    ASSERT_NE(shared, nullptr);
    EXPECT_EQ(shared.get(), provider.get());
    EXPECT_EQ(reg.GetShared("nonexistent"), nullptr);
}

TEST(MattingRegistryTest, Available) {
    MattingRegistry reg;
    reg.Register("b_method", std::make_shared<ThresholdMattingProvider>());
    reg.Register("a_method", std::make_shared<ThresholdMattingProvider>());

    auto keys = reg.Available();
    ASSERT_EQ(keys.size(), 2);
    EXPECT_EQ(keys[0], "a_method");
    EXPECT_EQ(keys[1], "b_method");
}

TEST(MattingRegistryTest, OverwriteExisting) {
    MattingRegistry reg;
    auto p1 = std::make_shared<ThresholdMattingProvider>(10.0f);
    auto p2 = std::make_shared<ThresholdMattingProvider>(20.0f);
    reg.Register("test", p1);
    reg.Register("test", p2);

    EXPECT_EQ(reg.Get("test"), p2.get());
    EXPECT_EQ(reg.Available().size(), 1);
}

// ── DiscoverMattingModels ───────────────────────────────────────────────────

class DiscoverTest : public MattingConfigTest {};

TEST_F(DiscoverTest, EmptyDirectory) {
    auto results = DiscoverMattingModels(tmp_dir_.string());
    EXPECT_TRUE(results.empty());
}

TEST_F(DiscoverTest, ModelWithConfig) {
    std::ofstream(tmp_dir_ / "model_a.onnx") << "fake_model_data";
    WriteJson("model_a.json", R"({"name": "Model A"})");

    auto results = DiscoverMattingModels(tmp_dir_.string());
    ASSERT_EQ(results.size(), 1);
    EXPECT_TRUE(results[0].first.find("model_a.onnx") != std::string::npos);
    EXPECT_EQ(results[0].second.name, "Model A");
}

TEST_F(DiscoverTest, ModelWithoutConfigIsSkipped) {
    std::ofstream(tmp_dir_ / "orphan.onnx") << "fake_model_data";

    auto results = DiscoverMattingModels(tmp_dir_.string());
    EXPECT_TRUE(results.empty());
}

TEST_F(DiscoverTest, MultipleModels) {
    std::ofstream(tmp_dir_ / "beta.onnx") << "fake";
    WriteJson("beta.json", R"({"name": "Beta"})");
    std::ofstream(tmp_dir_ / "alpha.onnx") << "fake";
    WriteJson("alpha.json", R"({"name": "Alpha"})");

    auto results = DiscoverMattingModels(tmp_dir_.string());
    ASSERT_EQ(results.size(), 2);
    EXPECT_TRUE(results[0].first.find("alpha.onnx") != std::string::npos);
    EXPECT_TRUE(results[1].first.find("beta.onnx") != std::string::npos);
}

TEST_F(DiscoverTest, NonexistentDirectory) {
    auto results = DiscoverMattingModels("/nonexistent/directory");
    EXPECT_TRUE(results.empty());
}

// ── MattingPostprocess ──────────────────────────────────────────────────────

TEST(MattingPostprocessTest, ThresholdAlpha) {
    cv::Mat alpha(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(alpha, cv::Rect(20, 20, 60, 60), cv::Scalar(200), cv::FILLED);
    cv::rectangle(alpha, cv::Rect(30, 30, 40, 40), cv::Scalar(100), cv::FILLED);

    cv::Mat original(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));

    MattingPostprocessParams params;
    params.threshold = 0.5f;

    auto result = ApplyMattingPostprocess(alpha, {}, original, params);
    ASSERT_FALSE(result.mask.empty());
    EXPECT_EQ(result.mask.type(), CV_8UC1);
    EXPECT_EQ(result.mask.at<uint8_t>(25, 25), 255);
    EXPECT_EQ(result.mask.at<uint8_t>(50, 50), 0);
    EXPECT_EQ(result.mask.at<uint8_t>(0, 0), 0);
}

TEST(MattingPostprocessTest, MorphClose) {
    cv::Mat alpha(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(alpha, cv::Rect(20, 20, 25, 60), cv::Scalar(200), cv::FILLED);
    cv::rectangle(alpha, cv::Rect(50, 20, 25, 60), cv::Scalar(200), cv::FILLED);

    cv::Mat original(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));

    MattingPostprocessParams params;
    params.threshold        = 0.3f;
    params.morph_close_size = 15;

    auto result = ApplyMattingPostprocess(alpha, {}, original, params);
    ASSERT_FALSE(result.mask.empty());
    EXPECT_EQ(result.mask.at<uint8_t>(50, 48), 255);
}

TEST(MattingPostprocessTest, OutlineCenterMode) {
    cv::Mat alpha(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(alpha, cv::Rect(20, 20, 60, 60), cv::Scalar(200), cv::FILLED);

    cv::Mat original(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));

    MattingPostprocessParams params;
    params.threshold       = 0.3f;
    params.outline_enabled = true;
    params.outline_width   = 2;
    params.outline_color   = {0, 255, 0};
    params.outline_mode    = OutlineMode::Center;

    auto result = ApplyMattingPostprocess(alpha, {}, original, params);
    ASSERT_FALSE(result.outline.empty());
    EXPECT_EQ(result.outline.type(), CV_8UC4);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(0, 0)[3], 0);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(50, 50)[3], 0);

    auto border_px = result.outline.at<cv::Vec4b>(20, 50);
    EXPECT_EQ(border_px[3], 255);
    EXPECT_EQ(border_px[1], 255);
}

TEST(MattingPostprocessTest, OutlineInnerMode) {
    cv::Mat mask(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(20, 20, 60, 60), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.outline_enabled = true;
    params.outline_width   = 3;
    params.outline_color   = {255, 0, 0};
    params.outline_mode    = OutlineMode::Inner;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    ASSERT_FALSE(result.outline.empty());

    EXPECT_EQ(result.outline.at<cv::Vec4b>(18, 50)[3], 0);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(22, 50)[3], 255);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(50, 50)[3], 0);
}

TEST(MattingPostprocessTest, OutlineOuterMode) {
    cv::Mat mask(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(20, 20, 60, 60), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.outline_enabled = true;
    params.outline_width   = 3;
    params.outline_color   = {0, 0, 255};
    params.outline_mode    = OutlineMode::Outer;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    ASSERT_FALSE(result.outline.empty());

    EXPECT_EQ(result.outline.at<cv::Vec4b>(17, 50)[3], 255);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(21, 50)[3], 0);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(50, 50)[3], 0);
}

TEST(MattingPostprocessTest, OutlineWidthAdaptsOnLargeMask) {
    cv::Mat mask(2048, 2048, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(512, 512, 1024, 1024), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.outline_enabled = true;
    params.outline_width   = 2;
    params.outline_color   = {0, 255, 0};
    params.outline_mode    = OutlineMode::Outer;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    ASSERT_FALSE(result.outline.empty());

    // 2048px short-side triggers adaptive scaling from 2 -> 4px.
    EXPECT_EQ(result.outline.at<cv::Vec4b>(509, 1024)[3], 255);
    EXPECT_EQ(result.outline.at<cv::Vec4b>(504, 1024)[3], 0);
}

TEST(MattingPostprocessTest, FallbackMask) {
    cv::Mat mask(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(20, 20, 60, 60), cv::Scalar(255), cv::FILLED);

    cv::Mat original(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));

    MattingPostprocessParams params;
    params.threshold = 0.5f;

    auto result = ApplyMattingPostprocess({}, mask, original, params);
    ASSERT_FALSE(result.mask.empty());
    ASSERT_FALSE(result.foreground.empty());
    EXPECT_EQ(result.foreground.type(), CV_8UC4);
    EXPECT_EQ(result.mask.at<uint8_t>(50, 50), 255);
    EXPECT_EQ(result.mask.at<uint8_t>(0, 0), 0);
}

TEST(MattingPostprocessTest, EmptyInputs) {
    MattingPostprocessParams params;
    auto result = ApplyMattingPostprocess({}, {}, {}, params);
    EXPECT_TRUE(result.mask.empty());
    EXPECT_TRUE(result.foreground.empty());
    EXPECT_TRUE(result.outline.empty());
}

TEST(MattingPostprocessTest, MorphCloseIterations) {
    cv::Mat alpha(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(alpha, cv::Rect(20, 20, 25, 60), cv::Scalar(200), cv::FILLED);
    cv::rectangle(alpha, cv::Rect(55, 20, 25, 60), cv::Scalar(200), cv::FILLED);

    cv::Mat original(100, 100, CV_8UC3, cv::Scalar(128, 128, 128));

    MattingPostprocessParams params_single;
    params_single.threshold        = 0.3f;
    params_single.morph_close_size = 7;

    auto r1 = ApplyMattingPostprocess(alpha, {}, original, params_single);
    EXPECT_EQ(r1.mask.at<uint8_t>(50, 50), 0);

    MattingPostprocessParams params_iter;
    params_iter.threshold              = 0.3f;
    params_iter.morph_close_size       = 7;
    params_iter.morph_close_iterations = 3;

    auto r2 = ApplyMattingPostprocess(alpha, {}, original, params_iter);
    EXPECT_EQ(r2.mask.at<uint8_t>(50, 50), 255);
}

TEST(MattingPostprocessTest, FilterSmallRegions) {
    cv::Mat mask(200, 200, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(10, 10, 100, 100), cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(150, 150, 5, 5), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.min_region_area = 100;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    ASSERT_FALSE(result.mask.empty());
    EXPECT_EQ(result.mask.at<uint8_t>(50, 50), 255);
    EXPECT_EQ(result.mask.at<uint8_t>(152, 152), 0);
}

TEST(MattingPostprocessTest, FilterSmallRegionsZeroSkips) {
    cv::Mat mask(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(10, 10, 5, 5), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.min_region_area = 0;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    EXPECT_EQ(result.mask.at<uint8_t>(12, 12), 255);
}

TEST(MattingPostprocessTest, FilterRemovesAllSmallRegions) {
    cv::Mat mask(100, 100, CV_8UC1, cv::Scalar(0));
    cv::rectangle(mask, cv::Rect(10, 10, 3, 3), cv::Scalar(255), cv::FILLED);
    cv::rectangle(mask, cv::Rect(50, 50, 4, 4), cv::Scalar(255), cv::FILLED);

    MattingPostprocessParams params;
    params.min_region_area = 100;

    auto result = ApplyMattingPostprocess({}, mask, {}, params);
    EXPECT_EQ(cv::countNonZero(result.mask), 0);
}
