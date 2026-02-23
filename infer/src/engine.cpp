#include "chromaprint3d/infer/engine.h"
#include "chromaprint3d/infer/error.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <unordered_map>

#if defined(CHROMAPRINT3D_HAS_ONNXRUNTIME)
#include "backends/onnxruntime/ort_backend.h"
#endif

namespace ChromaPrint3D::infer {

// -- Impl ------------------------------------------------------------------

struct InferenceEngine::Impl {
    std::unordered_map<BackendType, BackendPtr> backends;
};

// -- Lifecycle -------------------------------------------------------------

InferenceEngine::InferenceEngine() : impl_(std::make_unique<Impl>()) {}
InferenceEngine::~InferenceEngine() = default;
InferenceEngine::InferenceEngine(InferenceEngine&&) noexcept = default;
InferenceEngine& InferenceEngine::operator=(InferenceEngine&&) noexcept = default;

// -- Backend management ----------------------------------------------------

void InferenceEngine::RegisterBackend(BackendPtr backend) {
    if (!backend) return;
    const BackendType type = backend->Type();
    spdlog::info("InferenceEngine: registering backend '{}' (available={})",
                 backend->Name(), backend->IsAvailable());
    impl_->backends[type] = std::move(backend);
}

std::vector<BackendType> InferenceEngine::AvailableBackends() const {
    std::vector<BackendType> result;
    result.reserve(impl_->backends.size());
    for (const auto& [type, ptr] : impl_->backends) {
        if (ptr && ptr->IsAvailable()) result.push_back(type);
    }
    return result;
}

bool InferenceEngine::HasBackend(BackendType type) const {
    auto it = impl_->backends.find(type);
    return it != impl_->backends.end() && it->second && it->second->IsAvailable();
}

bool InferenceEngine::SupportsDevice(DeviceType device) const {
    for (const auto& [type, ptr] : impl_->backends) {
        if (ptr && ptr->IsAvailable() && ptr->SupportsDevice(device)) return true;
    }
    return false;
}

bool InferenceEngine::SupportsDevice(BackendType backend, DeviceType device) const {
    auto it = impl_->backends.find(backend);
    if (it == impl_->backends.end() || !it->second) return false;
    return it->second->IsAvailable() && it->second->SupportsDevice(device);
}

// -- Model loading ---------------------------------------------------------

SessionPtr InferenceEngine::LoadModel(const std::string& model_path,
                                      BackendType backend,
                                      const SessionOptions& options) {
    auto it = impl_->backends.find(backend);
    if (it == impl_->backends.end() || !it->second) {
        throw BackendError("Backend '" + BackendTypeName(backend) +
                           "' is not registered");
    }
    if (!it->second->IsAvailable()) {
        throw BackendError("Backend '" + it->second->Name() +
                           "' is registered but not available");
    }
    if (!it->second->SupportsDevice(options.device.type)) {
        throw DeviceError("Backend '" + it->second->Name() +
                          "' does not support device " + options.device.ToString());
    }
    return it->second->LoadModel(model_path, options);
}

namespace {

BackendType DetectBackendFromPath(const std::string& path) {
    auto pos = path.rfind('.');
    if (pos == std::string::npos) {
        throw ModelError("Cannot detect backend: no file extension in '" + path + "'");
    }
    std::string ext = path.substr(pos);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == ".onnx") return BackendType::kOnnxRuntime;
    if (ext == ".engine" || ext == ".trt") return BackendType::kTensorRT;
    if (ext == ".xml") return BackendType::kOpenVINO;
    if (ext == ".param") return BackendType::kNcnn;

    throw ModelError("Unknown model file extension '" + ext + "'");
}

} // namespace

SessionPtr InferenceEngine::LoadModel(const std::string& model_path,
                                      const SessionOptions& options) {
    const BackendType backend = DetectBackendFromPath(model_path);
    return LoadModel(model_path, backend, options);
}

SessionPtr InferenceEngine::LoadModel(const void* data, size_t size,
                                      BackendType backend,
                                      const SessionOptions& options) {
    auto it = impl_->backends.find(backend);
    if (it == impl_->backends.end() || !it->second) {
        throw BackendError("Backend '" + BackendTypeName(backend) +
                           "' is not registered");
    }
    if (!it->second->IsAvailable()) {
        throw BackendError("Backend '" + it->second->Name() +
                           "' is registered but not available");
    }
    if (!it->second->SupportsDevice(options.device.type)) {
        throw DeviceError("Backend '" + it->second->Name() +
                          "' does not support device " + options.device.ToString());
    }
    return it->second->LoadModel(data, size, options);
}

// -- Factory ---------------------------------------------------------------

InferenceEngine InferenceEngine::Create() {
    InferenceEngine engine;

#if defined(CHROMAPRINT3D_HAS_ONNXRUNTIME)
    engine.RegisterBackend(std::make_unique<OrtBackend>());
#endif

    return engine;
}

// -- BackendTypeName -------------------------------------------------------

std::string BackendTypeName(BackendType type) {
    switch (type) {
        case BackendType::kOnnxRuntime: return "OnnxRuntime";
        case BackendType::kTensorRT:    return "TensorRT";
        case BackendType::kOpenVINO:    return "OpenVINO";
        case BackendType::kNcnn:        return "ncnn";
    }
    return "Unknown";
}

} // namespace ChromaPrint3D::infer
