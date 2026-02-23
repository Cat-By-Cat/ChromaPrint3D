#pragma once

#include "chromaprint3d/infer/session.h"

#include <onnxruntime_cxx_api.h>

#include <memory>
#include <string>
#include <vector>

namespace ChromaPrint3D::infer {

class OrtInferSession : public ISession {
public:
    OrtInferSession(std::unique_ptr<Ort::Session> session,
                    Device device,
                    Ort::Env& env);
    ~OrtInferSession() override;

    const ModelInfo& GetModelInfo() const override;
    Device GetDevice() const override;

    std::unordered_map<std::string, Tensor> Run(
        const std::unordered_map<std::string, Tensor>& inputs) override;
    std::vector<Tensor> Run(const std::vector<Tensor>& inputs) override;

private:
    std::unique_ptr<Ort::Session> session_;
    Device device_;
    ModelInfo model_info_;
    Ort::AllocatorWithDefaultOptions allocator_;

    std::vector<std::string> input_names_;
    std::vector<std::string> output_names_;

    void PopulateModelInfo();

    std::vector<Ort::Value> RunImpl(
        const std::vector<const char*>& input_name_ptrs,
        const std::vector<Ort::Value>& input_values);
};

} // namespace ChromaPrint3D::infer
