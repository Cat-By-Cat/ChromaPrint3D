#pragma once

/// \file engine.h
/// \brief Top-level inference engine facade.

#include "backend.h"
#include "session.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ChromaPrint3D::infer {

/// Top-level facade that manages backends and creates sessions.
///
/// Use InferenceEngine::Create() to get an engine with all compiled-in
/// backends pre-registered.
class InferenceEngine {
public:
    InferenceEngine();
    ~InferenceEngine();

    InferenceEngine(const InferenceEngine&)            = delete;
    InferenceEngine& operator=(const InferenceEngine&) = delete;
    InferenceEngine(InferenceEngine&&) noexcept;
    InferenceEngine& operator=(InferenceEngine&&) noexcept;

    /// Register a backend.  Replaces any existing backend of the same type.
    void RegisterBackend(BackendPtr backend);

    /// List all registered backend types.
    std::vector<BackendType> AvailableBackends() const;

    /// Check if a specific backend is registered and available.
    bool HasBackend(BackendType type) const;

    /// Check if any registered backend supports the given device type.
    bool SupportsDevice(DeviceType device) const;

    /// Check if a specific backend supports the given device type.
    bool SupportsDevice(BackendType backend, DeviceType device) const;

    /// Load a model with a specific backend.
    SessionPtr LoadModel(const std::string& model_path, BackendType backend,
                         const SessionOptions& options = {});

    /// Load a model with automatic backend detection based on file extension.
    SessionPtr LoadModel(const std::string& model_path, const SessionOptions& options = {});

    /// Load a model from an in-memory buffer with a specific backend.
    SessionPtr LoadModel(const void* data, size_t size, BackendType backend,
                         const SessionOptions& options = {});

    /// Create an engine with all compiled-in backends pre-registered.
    static InferenceEngine Create();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ChromaPrint3D::infer
