#pragma once

#include "async_task_manager.h"
#include "chromaprint3d/vectorizer.h"

#include <opencv2/core.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <string>
#include <vector>

namespace ChromaPrint3D {

struct VectorizeTaskResult {
    std::string svg_content;
    int width      = 0;
    int height     = 0;
    int num_shapes = 0;
};

struct VectorizeTaskInfo : TaskBase {
    std::string image_name;
    VectorizerConfig config;

    double decode_ms    = 0;
    double vectorize_ms = 0;
    double pipeline_ms  = 0;

    VectorizeTaskResult result;
};

using VectorizeTaskMgr = AsyncTaskManager<VectorizeTaskInfo>;

inline std::string SubmitVectorize(VectorizeTaskMgr& mgr, std::vector<uint8_t> image_buffer,
                                   VectorizerConfig config, const std::string& image_name) {
    VectorizeTaskInfo initial;
    initial.image_name = image_name;
    initial.config     = config;

    return mgr.Submit(std::move(initial), [buf = std::move(image_buffer),
                                           config](const std::string& id, auto update) mutable {
        using Clock         = std::chrono::steady_clock;
        auto pipeline_start = Clock::now();

        auto vr = Vectorize(buf.data(), buf.size(), config);
        auto t_end = Clock::now();

        auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
        double pipeline_ms = ms(t_end - pipeline_start);

        spdlog::info("Vectorize task {}: total={:.1f}ms", id.substr(0, 8), pipeline_ms);

        update([&](VectorizeTaskInfo& task) {
            task.decode_ms          = 0;
            task.vectorize_ms       = pipeline_ms;
            task.pipeline_ms        = pipeline_ms;
            task.result.svg_content = std::move(vr.svg_content);
            task.result.width       = vr.width;
            task.result.height      = vr.height;
            task.result.num_shapes  = vr.num_shapes;
        });
    });
}

} // namespace ChromaPrint3D
