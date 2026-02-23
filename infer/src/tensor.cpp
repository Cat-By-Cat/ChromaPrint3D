#include "chromaprint3d/infer/tensor.h"
#include "chromaprint3d/infer/error.h"

#include <opencv2/core.hpp>

#include <algorithm>
#include <cstring>
#include <numeric>

namespace ChromaPrint3D::infer {

// -- Storage ---------------------------------------------------------------

struct Tensor::Storage {
    std::vector<uint8_t> owned;
    cv::Mat mat; // keeps Mat ref-count alive when constructed from cv::Mat

    static std::shared_ptr<Storage> AllocateOwned(size_t bytes) {
        auto s = std::make_shared<Storage>();
        s->owned.resize(bytes, 0);
        return s;
    }

    static std::shared_ptr<Storage> FromMat(const cv::Mat& m) {
        auto s = std::make_shared<Storage>();
        s->mat = m; // increments Mat refcount
        return s;
    }
};

// -- Helpers ---------------------------------------------------------------

namespace {

int64_t ComputeNumElements(const std::vector<int64_t>& shape) {
    if (shape.empty()) return 0;
    int64_t n = 1;
    for (int64_t d : shape) {
        if (d < 0) return 0; // dynamic dimensions → unknown
        n *= d;
    }
    return n;
}

DataType CvDepthToDataType(int depth) {
    switch (depth) {
    case CV_8U:
        return DataType::kUInt8;
    case CV_8S:
        return DataType::kInt8;
    case CV_32S:
        return DataType::kInt32;
    case CV_32F:
        return DataType::kFloat32;
    default:
        throw InputError("Unsupported cv::Mat depth for Tensor conversion");
    }
}

int DataTypeToCvDepth(DataType dtype) {
    switch (dtype) {
    case DataType::kUInt8:
        return CV_8U;
    case DataType::kInt8:
        return CV_8S;
    case DataType::kInt32:
        return CV_32S;
    case DataType::kFloat32:
        return CV_32F;
    default:
        throw InputError("DataType " + DataTypeName(dtype) + " has no cv::Mat equivalent");
    }
}

} // namespace

// -- Constructors ----------------------------------------------------------

Tensor::Tensor() = default;

Tensor Tensor::Allocate(DataType dtype, std::vector<int64_t> shape) {
    const int64_t n    = ComputeNumElements(shape);
    const size_t bytes = static_cast<size_t>(n) * DataTypeSize(dtype);

    Tensor t;
    t.dtype_     = dtype;
    t.shape_     = std::move(shape);
    t.storage_   = Storage::AllocateOwned(bytes);
    t.data_      = t.storage_->owned.data();
    t.byte_size_ = bytes;
    return t;
}

Tensor Tensor::Wrap(DataType dtype, std::vector<int64_t> shape, void* data, size_t byte_size) {
    Tensor t;
    t.dtype_     = dtype;
    t.shape_     = std::move(shape);
    t.data_      = data;
    t.byte_size_ = byte_size;
    // storage_ remains null — non-owning
    return t;
}

Tensor Tensor::FromCvMat(const cv::Mat& mat) {
    if (mat.empty()) return {};
    if (!mat.isContinuous()) {
        return FromCvMat(mat.clone()); // ensure contiguous
    }

    const DataType dtype = CvDepthToDataType(mat.depth());
    const int channels   = mat.channels();

    std::vector<int64_t> shape;
    if (channels == 1) {
        shape = {static_cast<int64_t>(mat.rows), static_cast<int64_t>(mat.cols)};
    } else {
        shape = {static_cast<int64_t>(mat.rows), static_cast<int64_t>(mat.cols),
                 static_cast<int64_t>(channels)};
    }

    Tensor t;
    t.dtype_     = dtype;
    t.shape_     = std::move(shape);
    t.storage_   = Storage::FromMat(mat);
    t.data_      = mat.data;
    t.byte_size_ = mat.total() * mat.elemSize();
    return t;
}

// -- Metadata --------------------------------------------------------------

bool Tensor::Empty() const { return data_ == nullptr || byte_size_ == 0; }

DataType Tensor::DType() const { return dtype_; }

const std::vector<int64_t>& Tensor::Shape() const { return shape_; }

int64_t Tensor::Shape(int axis) const {
    if (axis < 0) axis += NDim();
    if (axis < 0 || axis >= NDim()) { throw InputError("Tensor::Shape axis out of range"); }
    return shape_[static_cast<size_t>(axis)];
}

int Tensor::NDim() const { return static_cast<int>(shape_.size()); }

int64_t Tensor::NumElements() const { return ComputeNumElements(shape_); }

size_t Tensor::ByteSize() const { return byte_size_; }

// -- Data access -----------------------------------------------------------

void* Tensor::Data() { return data_; }

const void* Tensor::Data() const { return data_; }

// -- Conversion ------------------------------------------------------------

cv::Mat Tensor::ToCvMat(bool chw_to_hwc) const {
    if (Empty()) return {};

    const int depth = DataTypeToCvDepth(dtype_);
    const int ndim  = NDim();

    if (chw_to_hwc && ndim >= 3) {
        // Handle NCHW or CHW → HWC
        int64_t c{}, h{}, w{};
        if (ndim == 4) {
            c = shape_[1];
            h = shape_[2];
            w = shape_[3];
        } else if (ndim == 3) {
            c = shape_[0];
            h = shape_[1];
            w = shape_[2];
        } else {
            throw InputError("chw_to_hwc requires 3D or 4D tensor");
        }

        const int ci             = static_cast<int>(c);
        const int hi             = static_cast<int>(h);
        const int wi             = static_cast<int>(w);
        const size_t elem_size   = DataTypeSize(dtype_);
        const size_t plane_bytes = static_cast<size_t>(hi) * static_cast<size_t>(wi) * elem_size;

        // Offset to the first (or only) batch element
        const uint8_t* base = static_cast<const uint8_t*>(data_);

        std::vector<cv::Mat> planes;
        planes.reserve(static_cast<size_t>(ci));
        for (int ch = 0; ch < ci; ++ch) {
            const void* plane_ptr = base + static_cast<size_t>(ch) * plane_bytes;
            planes.emplace_back(hi, wi, CV_MAKETYPE(depth, 1), const_cast<void*>(plane_ptr));
        }

        cv::Mat merged;
        cv::merge(planes, merged);
        return merged; // deep-copied by merge
    }

    // Direct wrapping for HWC or 2D
    if (ndim == 2) {
        return cv::Mat(static_cast<int>(shape_[0]), static_cast<int>(shape_[1]),
                       CV_MAKETYPE(depth, 1), const_cast<void*>(data_))
            .clone();
    }
    if (ndim == 3) {
        return cv::Mat(static_cast<int>(shape_[0]), static_cast<int>(shape_[1]),
                       CV_MAKETYPE(depth, static_cast<int>(shape_[2])), const_cast<void*>(data_))
            .clone();
    }

    throw InputError("ToCvMat requires 2D or 3D tensor (or chw_to_hwc for CHW/NCHW)");
}

Tensor Tensor::Clone() const {
    if (Empty()) return {};
    Tensor t = Allocate(dtype_, shape_);
    std::memcpy(t.data_, data_, byte_size_);
    return t;
}

Tensor Tensor::Reshape(std::vector<int64_t> new_shape) const {
    // Resolve at most one -1 dimension
    int64_t known_product = 1;
    int neg_idx           = -1;
    for (size_t i = 0; i < new_shape.size(); ++i) {
        if (new_shape[i] < 0) {
            if (neg_idx >= 0) throw InputError("Reshape: at most one -1 allowed");
            neg_idx = static_cast<int>(i);
        } else {
            known_product *= new_shape[i];
        }
    }

    const int64_t total = NumElements();
    if (neg_idx >= 0) {
        if (known_product == 0) throw InputError("Reshape: zero in shape with -1");
        new_shape[static_cast<size_t>(neg_idx)] = total / known_product;
    }

    if (ComputeNumElements(new_shape) != total) {
        throw InputError("Reshape: element count mismatch");
    }

    Tensor t;
    t.dtype_     = dtype_;
    t.shape_     = std::move(new_shape);
    t.storage_   = storage_;
    t.data_      = data_;
    t.byte_size_ = byte_size_;
    return t;
}

} // namespace ChromaPrint3D::infer
