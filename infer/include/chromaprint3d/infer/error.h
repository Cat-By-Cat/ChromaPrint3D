#pragma once

/// \file error.h
/// \brief Inference-specific exception hierarchy.

#include <stdexcept>
#include <string>

namespace ChromaPrint3D::infer {

enum class InferErrorCode : int {
    kBackendError = 1,
    kModelError   = 2,
    kDeviceError  = 3,
    kInputError   = 4,
};

class InferError : public std::runtime_error {
public:
    InferError(InferErrorCode code, const std::string& msg)
        : std::runtime_error(msg), code_(code) {}

    InferErrorCode code() const noexcept { return code_; }

private:
    InferErrorCode code_;
};

class BackendError : public InferError {
public:
    explicit BackendError(const std::string& msg)
        : InferError(InferErrorCode::kBackendError, msg) {}
};

class ModelError : public InferError {
public:
    explicit ModelError(const std::string& msg) : InferError(InferErrorCode::kModelError, msg) {}
};

class DeviceError : public InferError {
public:
    explicit DeviceError(const std::string& msg) : InferError(InferErrorCode::kDeviceError, msg) {}
};

class InputError : public InferError {
public:
    explicit InputError(const std::string& msg) : InferError(InferErrorCode::kInputError, msg) {}
};

} // namespace ChromaPrint3D::infer
