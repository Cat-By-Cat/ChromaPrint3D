#include <gtest/gtest.h>
#include "chromaprint3d/matting.h"
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

    void TearDown() override {
        std::filesystem::remove_all(tmp_dir_);
    }

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
    cv::Mat result = provider.Run(cv::Mat());
    EXPECT_TRUE(result.empty());
}

TEST(ThresholdMattingTest, BasicMask) {
    ThresholdMattingProvider provider(15.0f);

    cv::Mat bgr(100, 100, CV_8UC3, cv::Scalar(200, 200, 200));
    cv::rectangle(bgr, cv::Rect(30, 30, 40, 40), cv::Scalar(0, 0, 255), cv::FILLED);

    cv::Mat mask = provider.Run(bgr);
    ASSERT_FALSE(mask.empty());
    EXPECT_EQ(mask.rows, 100);
    EXPECT_EQ(mask.cols, 100);
    EXPECT_EQ(mask.type(), CV_8UC1);

    EXPECT_EQ(mask.at<uint8_t>(0, 0), 0);
    EXPECT_EQ(mask.at<uint8_t>(50, 50), 255);
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
