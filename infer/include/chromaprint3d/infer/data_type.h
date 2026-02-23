#pragma once

/// \file data_type.h
/// \brief Tensor element data type enumeration.

#include <cstddef>
#include <cstdint>
#include <string>

namespace ChromaPrint3D::infer {

enum class DataType : uint8_t {
    kFloat32 = 0,
    kFloat16 = 1,
    kInt32   = 2,
    kInt64   = 3,
    kUInt8   = 4,
    kInt8    = 5,
    kBool    = 6,
};

/// Returns the byte size of a single element for the given data type.
size_t DataTypeSize(DataType dtype);

/// Returns a human-readable name (e.g. "float32", "uint8").
std::string DataTypeName(DataType dtype);

} // namespace ChromaPrint3D::infer
