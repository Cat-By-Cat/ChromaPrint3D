#pragma once

/// \file backend.h
/// \brief Abstract inference backend interface and session options.

#include "device.h"
#include "session.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace ChromaPrint3D::infer {

enum class BackendType : uint8_t {
    kOnnxRuntime = 0,
    kTensorRT    = 1,
    kOpenVINO    = 2,
    kNcnn        = 3,
};

std::string BackendTypeName(BackendType type);

/// Options for creating an inference session.
struct SessionOptions {
    Device device;
    int num_threads = 0;           ///< 0 = automatic (CPU core count).
    bool enable_profiling = false;
    /// Backend-specific key-value options.
    std::unordered_map<std::string, std::string> extra;
};

/// Abstract factory for a specific inference backend.
class IBackend {
public:
    virtual ~IBackend() = default;

    virtual BackendType Type() const = 0;
    virtual std::string Name() const = 0;

    /// Whether the runtime library is present and usable.
    virtual bool IsAvailable() const = 0;

    /// Whether this backend can execute on the given device type.
    virtual bool SupportsDevice(DeviceType device) const = 0;

    /// Load a model from a file path.
    virtual SessionPtr LoadModel(const std::string& model_path,
                                 const SessionOptions& options = {}) = 0;

    /// Load a model from an in-memory buffer.
    virtual SessionPtr LoadModel(const void* data, size_t size,
                                 const SessionOptions& options = {}) = 0;
};

using BackendPtr = std::unique_ptr<IBackend>;

} // namespace ChromaPrint3D::infer
