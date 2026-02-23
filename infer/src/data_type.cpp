#include "chromaprint3d/infer/data_type.h"

#include <stdexcept>

namespace ChromaPrint3D::infer {

size_t DataTypeSize(DataType dtype) {
    switch (dtype) {
        case DataType::kFloat32: return 4;
        case DataType::kFloat16: return 2;
        case DataType::kInt32:   return 4;
        case DataType::kInt64:   return 8;
        case DataType::kUInt8:   return 1;
        case DataType::kInt8:    return 1;
        case DataType::kBool:    return 1;
    }
    throw std::invalid_argument("Unknown DataType");
}

std::string DataTypeName(DataType dtype) {
    switch (dtype) {
        case DataType::kFloat32: return "float32";
        case DataType::kFloat16: return "float16";
        case DataType::kInt32:   return "int32";
        case DataType::kInt64:   return "int64";
        case DataType::kUInt8:   return "uint8";
        case DataType::kInt8:    return "int8";
        case DataType::kBool:    return "bool";
    }
    return "unknown";
}

} // namespace ChromaPrint3D::infer
