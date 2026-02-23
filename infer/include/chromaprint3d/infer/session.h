#pragma once

/// \file session.h
/// \brief Abstract inference session interface.

#include "device.h"
#include "model_info.h"
#include "tensor.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D::infer {

/// An inference session bound to a loaded model and a device.
///
/// Thread-safety depends on the concrete backend implementation.
/// ONNX Runtime sessions are safe for concurrent Run() calls.
class ISession {
public:
    virtual ~ISession() = default;

    /// Model input/output metadata.
    virtual const ModelInfo& GetModelInfo() const = 0;

    /// Device this session is running on.
    virtual Device GetDevice() const = 0;

    /// Run inference with named tensors.
    virtual std::unordered_map<std::string, Tensor>
    Run(const std::unordered_map<std::string, Tensor>& inputs) = 0;

    /// Run inference with ordered tensors (matching model input/output order).
    virtual std::vector<Tensor> Run(const std::vector<Tensor>& inputs) = 0;
};

using SessionPtr = std::unique_ptr<ISession>;

} // namespace ChromaPrint3D::infer
