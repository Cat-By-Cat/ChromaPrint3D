#include <chromaprint3d/infer/tensor.h>
#include <chromaprint3d/infer/error.h>

#include <opencv2/core.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <numeric>
#include <vector>

using namespace ChromaPrint3D::infer;

TEST(TensorTest, DefaultIsEmpty) {
    Tensor t;
    EXPECT_TRUE(t.Empty());
    EXPECT_EQ(t.NDim(), 0);
    EXPECT_EQ(t.NumElements(), 0);
    EXPECT_EQ(t.ByteSize(), 0);
    EXPECT_EQ(t.Data(), nullptr);
}

TEST(TensorTest, AllocateFloat32) {
    Tensor t = Tensor::Allocate(DataType::kFloat32, {2, 3, 4});
    EXPECT_FALSE(t.Empty());
    EXPECT_EQ(t.DType(), DataType::kFloat32);
    EXPECT_EQ(t.NDim(), 3);
    EXPECT_EQ(t.Shape(0), 2);
    EXPECT_EQ(t.Shape(1), 3);
    EXPECT_EQ(t.Shape(2), 4);
    EXPECT_EQ(t.NumElements(), 24);
    EXPECT_EQ(t.ByteSize(), 24 * sizeof(float));
    EXPECT_NE(t.Data(), nullptr);

    // Data should be zero-initialized
    const float* data = t.DataAs<float>();
    for (int64_t i = 0; i < t.NumElements(); ++i) { EXPECT_FLOAT_EQ(data[i], 0.0f); }
}

TEST(TensorTest, AllocateUInt8) {
    Tensor t = Tensor::Allocate(DataType::kUInt8, {100, 100});
    EXPECT_EQ(t.DType(), DataType::kUInt8);
    EXPECT_EQ(t.NumElements(), 10000);
    EXPECT_EQ(t.ByteSize(), 10000);
}

TEST(TensorTest, WrapExternalBuffer) {
    std::vector<float> buf(12, 1.5f);
    Tensor t = Tensor::Wrap(DataType::kFloat32, {3, 4}, buf.data(), buf.size() * sizeof(float));

    EXPECT_FALSE(t.Empty());
    EXPECT_EQ(t.NumElements(), 12);
    EXPECT_EQ(t.Data(), buf.data());

    // Verify data matches
    const float* data = t.DataAs<float>();
    for (int i = 0; i < 12; ++i) { EXPECT_FLOAT_EQ(data[i], 1.5f); }
}

TEST(TensorTest, FromCvMatUInt8) {
    cv::Mat mat(10, 20, CV_8UC3, cv::Scalar(100, 150, 200));
    Tensor t = Tensor::FromCvMat(mat);

    EXPECT_FALSE(t.Empty());
    EXPECT_EQ(t.DType(), DataType::kUInt8);
    EXPECT_EQ(t.NDim(), 3);
    EXPECT_EQ(t.Shape(0), 10);
    EXPECT_EQ(t.Shape(1), 20);
    EXPECT_EQ(t.Shape(2), 3);
    EXPECT_EQ(t.NumElements(), 10 * 20 * 3);

    // Shares data with Mat
    EXPECT_EQ(t.Data(), mat.data);
}

TEST(TensorTest, FromCvMatFloat32SingleChannel) {
    cv::Mat mat(5, 5, CV_32FC1, cv::Scalar(3.14f));
    Tensor t = Tensor::FromCvMat(mat);

    EXPECT_EQ(t.DType(), DataType::kFloat32);
    EXPECT_EQ(t.NDim(), 2);
    EXPECT_EQ(t.Shape(0), 5);
    EXPECT_EQ(t.Shape(1), 5);
}

TEST(TensorTest, FromEmptyCvMat) {
    cv::Mat empty;
    Tensor t = Tensor::FromCvMat(empty);
    EXPECT_TRUE(t.Empty());
}

TEST(TensorTest, Clone) {
    Tensor original = Tensor::Allocate(DataType::kFloat32, {2, 3});
    float* data     = original.DataAs<float>();
    for (int i = 0; i < 6; ++i) data[i] = static_cast<float>(i);

    Tensor copy = original.Clone();
    EXPECT_NE(copy.Data(), original.Data());
    EXPECT_EQ(copy.Shape(), original.Shape());
    EXPECT_EQ(copy.DType(), original.DType());

    const float* copy_data = copy.DataAs<float>();
    for (int i = 0; i < 6; ++i) { EXPECT_FLOAT_EQ(copy_data[i], static_cast<float>(i)); }
}

TEST(TensorTest, ReshapeValid) {
    Tensor t = Tensor::Allocate(DataType::kFloat32, {2, 3, 4});
    Tensor r = t.Reshape({6, 4});

    EXPECT_EQ(r.NDim(), 2);
    EXPECT_EQ(r.Shape(0), 6);
    EXPECT_EQ(r.Shape(1), 4);
    EXPECT_EQ(r.NumElements(), 24);
    EXPECT_EQ(r.Data(), t.Data()); // shares storage
}

TEST(TensorTest, ReshapeWithInferredDim) {
    Tensor t = Tensor::Allocate(DataType::kFloat32, {2, 3, 4});
    Tensor r = t.Reshape({-1, 4});

    EXPECT_EQ(r.Shape(0), 6);
    EXPECT_EQ(r.Shape(1), 4);
}

TEST(TensorTest, ReshapeMismatchThrows) {
    Tensor t = Tensor::Allocate(DataType::kFloat32, {2, 3});
    EXPECT_THROW(t.Reshape({2, 4}), InputError);
}

TEST(TensorTest, ToCvMatHWC) {
    cv::Mat src(4, 6, CV_32FC3);
    src.setTo(cv::Scalar(1.0f, 2.0f, 3.0f));
    Tensor t = Tensor::FromCvMat(src);

    cv::Mat back = t.ToCvMat();
    EXPECT_EQ(back.rows, 4);
    EXPECT_EQ(back.cols, 6);
    EXPECT_EQ(back.channels(), 3);

    auto pixel = back.at<cv::Vec3f>(0, 0);
    EXPECT_FLOAT_EQ(pixel[0], 1.0f);
    EXPECT_FLOAT_EQ(pixel[1], 2.0f);
    EXPECT_FLOAT_EQ(pixel[2], 3.0f);
}

TEST(TensorTest, ToCvMatCHW) {
    // Create a CHW tensor: 3 x 2 x 4
    Tensor t = Tensor::Allocate(DataType::kFloat32, {3, 2, 4});
    float* d = t.DataAs<float>();
    // Fill channel 0 with 10, channel 1 with 20, channel 2 with 30
    for (int i = 0; i < 8; ++i) d[i] = 10.0f;
    for (int i = 8; i < 16; ++i) d[i] = 20.0f;
    for (int i = 16; i < 24; ++i) d[i] = 30.0f;

    cv::Mat mat = t.ToCvMat(/*chw_to_hwc=*/true);
    EXPECT_EQ(mat.rows, 2);
    EXPECT_EQ(mat.cols, 4);
    EXPECT_EQ(mat.channels(), 3);

    auto pixel = mat.at<cv::Vec3f>(0, 0);
    EXPECT_FLOAT_EQ(pixel[0], 10.0f);
    EXPECT_FLOAT_EQ(pixel[1], 20.0f);
    EXPECT_FLOAT_EQ(pixel[2], 30.0f);
}

TEST(TensorTest, SharedStorageSemantics) {
    Tensor a             = Tensor::Allocate(DataType::kFloat32, {4});
    a.DataAs<float>()[0] = 42.0f;

    Tensor b = a; // copy — shares storage
    EXPECT_EQ(b.Data(), a.Data());
    EXPECT_FLOAT_EQ(b.DataAs<float>()[0], 42.0f);

    // Mutating through b is visible in a
    b.DataAs<float>()[0] = 99.0f;
    EXPECT_FLOAT_EQ(a.DataAs<float>()[0], 99.0f);
}

TEST(TensorTest, NegativeAxisAccess) {
    Tensor t = Tensor::Allocate(DataType::kFloat32, {2, 3, 4});
    EXPECT_EQ(t.Shape(-1), 4);
    EXPECT_EQ(t.Shape(-2), 3);
    EXPECT_EQ(t.Shape(-3), 2);
}
