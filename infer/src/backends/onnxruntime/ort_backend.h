#pragma once

#include "chromaprint3d/infer/backend.h"

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <mutex>

namespace ChromaPrint3D::infer {

class OrtBackend : public IBackend {
public:
    OrtBackend();
    ~OrtBackend() override;

    BackendType Type() const override;
    std::string Name() const override;
    bool IsAvailable() const override;
    bool SupportsDevice(DeviceType device) const override;

    SessionPtr LoadModel(const std::string& model_path,
                         const SessionOptions& options = {}) override;
    SessionPtr LoadModel(const void* data, size_t size,
                         const SessionOptions& options = {}) override;

    Ort::Env& GetEnv();

private:
    Ort::Env env_;
    bool cuda_available_ = false;

    void ProbeCuda();
    Ort::SessionOptions BuildOrtOptions(const SessionOptions& options) const;
};

} // namespace ChromaPrint3D::infer
