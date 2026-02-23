#include "ort_session.h"
#include "chromaprint3d/infer/error.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstring>

namespace ChromaPrint3D::infer {

namespace {

DataType OrtTypeToDataType(ONNXTensorElementDataType ort_type) {
    switch (ort_type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
        return DataType::kFloat32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:
        return DataType::kFloat16;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
        return DataType::kInt32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
        return DataType::kInt64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
        return DataType::kUInt8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
        return DataType::kInt8;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
        return DataType::kBool;
    default:
        spdlog::warn("Unmapped ONNX tensor type {}, defaulting to float32",
                     static_cast<int>(ort_type));
        return DataType::kFloat32;
    }
}

} // namespace

// -- Lifecycle -------------------------------------------------------------

OrtInferSession::OrtInferSession(std::unique_ptr<Ort::Session> session, Device device,
                                 Ort::Env& /*env*/)
    : session_(std::move(session)), device_(device) {
    PopulateModelInfo();
}

OrtInferSession::~OrtInferSession() = default;

// -- Metadata --------------------------------------------------------------

const ModelInfo& OrtInferSession::GetModelInfo() const { return model_info_; }

Device OrtInferSession::GetDevice() const { return device_; }

void OrtInferSession::PopulateModelInfo() {
    const size_t num_inputs  = session_->GetInputCount();
    const size_t num_outputs = session_->GetOutputCount();

    model_info_.inputs.resize(num_inputs);
    input_names_.resize(num_inputs);

    for (size_t i = 0; i < num_inputs; ++i) {
        auto name       = session_->GetInputNameAllocated(i, allocator_);
        input_names_[i] = name.get();

        auto type_info   = session_->GetInputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();

        model_info_.inputs[i].name  = input_names_[i];
        model_info_.inputs[i].dtype = OrtTypeToDataType(tensor_info.GetElementType());
        model_info_.inputs[i].shape = tensor_info.GetShape();
    }

    model_info_.outputs.resize(num_outputs);
    output_names_.resize(num_outputs);

    for (size_t i = 0; i < num_outputs; ++i) {
        auto name        = session_->GetOutputNameAllocated(i, allocator_);
        output_names_[i] = name.get();

        auto type_info   = session_->GetOutputTypeInfo(i);
        auto tensor_info = type_info.GetTensorTypeAndShapeInfo();

        model_info_.outputs[i].name  = output_names_[i];
        model_info_.outputs[i].dtype = OrtTypeToDataType(tensor_info.GetElementType());
        model_info_.outputs[i].shape = tensor_info.GetShape();
    }

    spdlog::debug("OrtSession loaded: {} input(s), {} output(s)", num_inputs, num_outputs);
}

// -- Helpers ---------------------------------------------------------------

namespace {

ONNXTensorElementDataType DataTypeToOrtType(DataType dtype) {
    switch (dtype) {
    case DataType::kFloat32:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
    case DataType::kFloat16:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16;
    case DataType::kInt32:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32;
    case DataType::kInt64:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    case DataType::kUInt8:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8;
    case DataType::kInt8:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8;
    case DataType::kBool:
        return ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
    }
    throw InputError("Unsupported DataType for ORT conversion");
}

Ort::Value TensorToOrtValue(const Tensor& tensor) {
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    const auto& shape                        = tensor.Shape();
    const ONNXTensorElementDataType ort_type = DataTypeToOrtType(tensor.DType());

    return Ort::Value::CreateTensor(memory_info, const_cast<void*>(tensor.Data()),
                                    tensor.ByteSize(), shape.data(), shape.size(), ort_type);
}

Tensor OrtValueToTensor(Ort::Value& value) {
    auto type_info       = value.GetTensorTypeAndShapeInfo();
    const auto ort_type  = type_info.GetElementType();
    const auto shape     = type_info.GetShape();
    const DataType dtype = OrtTypeToDataType(ort_type);

    const size_t elem_size = DataTypeSize(dtype);
    int64_t numel          = 1;
    for (int64_t d : shape) numel *= d;
    const size_t byte_size = static_cast<size_t>(numel) * elem_size;

    Tensor t = Tensor::Allocate(dtype, shape);
    std::memcpy(t.Data(), value.GetTensorData<void>(), byte_size);
    return t;
}

} // namespace

// -- Run -------------------------------------------------------------------

std::vector<Ort::Value> OrtInferSession::RunImpl(const std::vector<const char*>& input_name_ptrs,
                                                 const std::vector<Ort::Value>& input_values) {

    std::vector<const char*> output_name_ptrs;
    output_name_ptrs.reserve(output_names_.size());
    for (const auto& n : output_names_) output_name_ptrs.push_back(n.c_str());

    try {
        return session_->Run(Ort::RunOptions{nullptr}, input_name_ptrs.data(), input_values.data(),
                             input_values.size(), output_name_ptrs.data(), output_name_ptrs.size());
    } catch (const Ort::Exception& e) {
        throw BackendError(std::string("ONNX Runtime Run() failed: ") + e.what());
    }
}

std::unordered_map<std::string, Tensor>
OrtInferSession::Run(const std::unordered_map<std::string, Tensor>& inputs) {

    // Build ordered input values matching model input order
    std::vector<const char*> name_ptrs;
    std::vector<Ort::Value> ort_inputs;
    name_ptrs.reserve(input_names_.size());
    ort_inputs.reserve(input_names_.size());

    for (const auto& name : input_names_) {
        auto it = inputs.find(name);
        if (it == inputs.end()) { throw InputError("Missing input tensor: '" + name + "'"); }
        name_ptrs.push_back(name.c_str());
        ort_inputs.push_back(TensorToOrtValue(it->second));
    }

    auto ort_outputs = RunImpl(name_ptrs, ort_inputs);

    std::unordered_map<std::string, Tensor> result;
    for (size_t i = 0; i < ort_outputs.size(); ++i) {
        result.emplace(output_names_[i], OrtValueToTensor(ort_outputs[i]));
    }
    return result;
}

std::vector<Tensor> OrtInferSession::Run(const std::vector<Tensor>& inputs) {
    if (inputs.size() != input_names_.size()) {
        throw InputError("Expected " + std::to_string(input_names_.size()) + " input(s), got " +
                         std::to_string(inputs.size()));
    }

    std::vector<const char*> name_ptrs;
    std::vector<Ort::Value> ort_inputs;
    name_ptrs.reserve(inputs.size());
    ort_inputs.reserve(inputs.size());

    for (size_t i = 0; i < inputs.size(); ++i) {
        name_ptrs.push_back(input_names_[i].c_str());
        ort_inputs.push_back(TensorToOrtValue(inputs[i]));
    }

    auto ort_outputs = RunImpl(name_ptrs, ort_inputs);

    std::vector<Tensor> result;
    result.reserve(ort_outputs.size());
    for (auto& val : ort_outputs) { result.push_back(OrtValueToTensor(val)); }
    return result;
}

} // namespace ChromaPrint3D::infer
