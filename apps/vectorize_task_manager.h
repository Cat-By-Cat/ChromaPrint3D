#pragma once

#include "async_task_manager.h"
#include "chromaprint3d/vectorizer.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
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

        auto t0       = Clock::now();
        cv::Mat input = cv::imdecode(buf, cv::IMREAD_COLOR);
        auto t1       = Clock::now();
        if (input.empty()) throw std::runtime_error("Failed to decode image");

        auto vr = Vectorize(input, config);
        auto t2 = Clock::now();

        auto ms = [](auto d) { return std::chrono::duration<double, std::milli>(d).count(); };
        double decode_ms    = ms(t1 - t0);
        double vectorize_ms = ms(t2 - t1);
        double pipeline_ms  = ms(t2 - pipeline_start);

        spdlog::info("Vectorize task {}: decode={:.1f}ms, vectorize={:.1f}ms, total={:.1f}ms",
                     id.substr(0, 8), decode_ms, vectorize_ms, pipeline_ms);

        update([&](VectorizeTaskInfo& task) {
            task.decode_ms          = decode_ms;
            task.vectorize_ms       = vectorize_ms;
            task.pipeline_ms        = pipeline_ms;
            task.result.svg_content = std::move(vr.svg_content);
            task.result.width       = vr.width;
            task.result.height      = vr.height;
            task.result.num_shapes  = vr.num_shapes;
        });
    });
}

} // namespace ChromaPrint3D
