#pragma once

/// \file tensor.h
/// \brief Tensor container with shared-storage semantics.

#include "data_type.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace cv {
class Mat;
}

namespace ChromaPrint3D::infer {

/// Multi-dimensional tensor with reference-counted storage.
///
/// Copies share the underlying buffer (like cv::Mat).
/// Use Clone() for a deep copy.
class Tensor {
public:
    Tensor();

    /// Allocate a tensor that owns its storage.
    static Tensor Allocate(DataType dtype, std::vector<int64_t> shape);

    /// Wrap an external buffer without taking ownership.
    /// The caller must keep the buffer alive for the lifetime of this Tensor.
    static Tensor Wrap(DataType dtype, std::vector<int64_t> shape,
                       void* data, size_t byte_size);

    /// Create a tensor from a cv::Mat, sharing its ref-counted storage.
    /// Supported Mat types: CV_8UC1/3, CV_32FC1/3.
    static Tensor FromCvMat(const cv::Mat& mat);

    // -- Metadata ----------------------------------------------------------

    bool Empty() const;
    DataType DType() const;
    const std::vector<int64_t>& Shape() const;
    int64_t Shape(int axis) const;
    int NDim() const;
    int64_t NumElements() const;
    size_t ByteSize() const;

    // -- Data access -------------------------------------------------------

    void* Data();
    const void* Data() const;

    template <typename T>
    T* DataAs() {
        return static_cast<T*>(Data());
    }

    template <typename T>
    const T* DataAs() const {
        return static_cast<const T*>(Data());
    }

    // -- Conversion --------------------------------------------------------

    /// Convert to cv::Mat (shares data when layout is HWC).
    /// If \p chw_to_hwc is true, transposes from CHW to HWC (copies data).
    cv::Mat ToCvMat(bool chw_to_hwc = false) const;

    /// Deep copy of data and metadata.
    Tensor Clone() const;

    /// Return a tensor that shares storage but with a different shape.
    /// Total element count must remain the same.
    Tensor Reshape(std::vector<int64_t> new_shape) const;

private:
    struct Storage;
    std::shared_ptr<Storage> storage_;
    DataType dtype_ = DataType::kFloat32;
    std::vector<int64_t> shape_;
    void* data_ = nullptr;
    size_t byte_size_ = 0;
};

} // namespace ChromaPrint3D::infer
