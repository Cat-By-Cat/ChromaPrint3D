#include "ort_backend.h"
#include "ort_session.h"
#include "chromaprint3d/infer/error.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <thread>

namespace ChromaPrint3D::infer {

namespace {

std::filesystem::path ToOrtModelPath(const std::string& model_path) {
#ifdef _WIN32
    // ONNX Runtime on Windows expects wide-char model path.
    return std::filesystem::u8path(model_path);
#else
    return std::filesystem::path(model_path);
#endif
}

} // namespace

OrtBackend::OrtBackend() : env_(ORT_LOGGING_LEVEL_WARNING, "ChromaPrint3D") {
    ProbeCuda();
    spdlog::info("OrtBackend initialized (CUDA available: {})", cuda_available_);
}

OrtBackend::~OrtBackend() = default;

BackendType OrtBackend::Type() const { return BackendType::kOnnxRuntime; }

std::string OrtBackend::Name() const { return "OnnxRuntime"; }

bool OrtBackend::IsAvailable() const { return true; }

bool OrtBackend::SupportsDevice(DeviceType device) const {
    if (device == DeviceType::kCPU) return true;
    if (device == DeviceType::kCUDA) return cuda_available_;
    return false;
}

Ort::Env& OrtBackend::GetEnv() { return env_; }

void OrtBackend::ProbeCuda() {
    try {
        Ort::SessionOptions opts;
        OrtCUDAProviderOptions cuda_opts{};
        cuda_opts.device_id = 0;
        opts.AppendExecutionProvider_CUDA(cuda_opts);
        cuda_available_ = true;
    } catch (const Ort::Exception&) { cuda_available_ = false; }
}

Ort::SessionOptions OrtBackend::BuildOrtOptions(const SessionOptions& options) const {
    Ort::SessionOptions ort_opts;

    // Thread count
    int threads = options.num_threads;
    if (threads <= 0) {
        threads = static_cast<int>(std::thread::hardware_concurrency());
        if (threads <= 0) threads = 4;
    }
    ort_opts.SetIntraOpNumThreads(threads);

    // Graph optimization
    auto it = options.extra.find("graph_opt_level");
    if (it != options.extra.end()) {
        if (it->second == "disabled") {
            ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_DISABLE_ALL);
        } else if (it->second == "basic") {
            ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        } else if (it->second == "extended") {
            ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);
        } else {
            ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        }
    } else {
        ort_opts.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    }

    if (options.enable_profiling) {
#ifdef _WIN32
        ort_opts.EnableProfiling(L"chromaprint3d_ort_profile");
#else
        ort_opts.EnableProfiling("chromaprint3d_ort_profile");
#endif
    }

    // CUDA execution provider
    if (options.device.type == DeviceType::kCUDA) {
        if (!cuda_available_) {
            throw DeviceError("CUDA requested but not available in this OnnxRuntime build");
        }
        OrtCUDAProviderOptions cuda_opts{};
        cuda_opts.device_id = options.device.index;

        auto arena_it = options.extra.find("cuda_arena_extend_strategy");
        if (arena_it != options.extra.end() && arena_it->second == "same_as_alloc") {
            cuda_opts.arena_extend_strategy = 1;
        }

        ort_opts.AppendExecutionProvider_CUDA(cuda_opts);
    }

    return ort_opts;
}

SessionPtr OrtBackend::LoadModel(const std::string& model_path, const SessionOptions& options) {
    try {
        Ort::SessionOptions ort_opts = BuildOrtOptions(options);
        const auto ort_model_path    = ToOrtModelPath(model_path);
        auto ort_session             = std::make_unique<Ort::Session>(env_, ort_model_path.c_str(), ort_opts);
        return std::make_unique<OrtInferSession>(std::move(ort_session), options.device, env_);
    } catch (const Ort::Exception& e) {
        throw ModelError(std::string("ONNX Runtime failed to load '") + model_path +
                         "': " + e.what());
    }
}

SessionPtr OrtBackend::LoadModel(const void* data, size_t size, const SessionOptions& options) {
    try {
        Ort::SessionOptions ort_opts = BuildOrtOptions(options);
        auto ort_session             = std::make_unique<Ort::Session>(env_, data, size, ort_opts);
        return std::make_unique<OrtInferSession>(std::move(ort_session), options.device, env_);
    } catch (const Ort::Exception& e) {
        throw ModelError(std::string("ONNX Runtime failed to load model from buffer: ") + e.what());
    }
}

} // namespace ChromaPrint3D::infer
