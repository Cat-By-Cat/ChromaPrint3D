#pragma once

/// \file model_info.h
/// \brief Model input/output metadata.

#include "data_type.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ChromaPrint3D::infer {

/// Describes one input or output tensor of a model.
struct TensorSpec {
    std::string name;
    DataType dtype = DataType::kFloat32;
    std::vector<int64_t> shape; ///< -1 for dynamic dimensions.
};

/// Metadata about a loaded model's inputs and outputs.
struct ModelInfo {
    std::vector<TensorSpec> inputs;
    std::vector<TensorSpec> outputs;
};

} // namespace ChromaPrint3D::infer
