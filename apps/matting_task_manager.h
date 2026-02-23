#pragma once

#include "async_task_manager.h"
#include "chromaprint3d/matting.h"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <string>
#include <vector>

namespace ChromaPrint3D {

struct MattingTaskResult {
    std::vector<uint8_t> mask_png;
    std::vector<uint8_t> foreground_png;
    int width  = 0;
    int height = 0;
};

struct MattingTaskInfo : TaskBase {
    std::string method;
    std::string image_name;

    MattingTimingInfo timing;
    double decode_ms   = 0;
    double encode_ms   = 0;
    double pipeline_ms = 0;

    MattingTaskResult result;
};

using MattingTaskMgr = AsyncTaskManager<MattingTaskInfo>;

namespace detail {

inline cv::Mat CompositeForeground(const cv::Mat& bgr, const cv::Mat& mask) {
    cv::Mat bgra;
    cv::cvtColor(bgr, bgra, cv::COLOR_BGR2BGRA);
    std::vector<cv::Mat> channels(4);
    cv::split(bgra, channels);
    channels[3] = mask;
    cv::merge(channels, bgra);
    return bgra;
}

} // namespace detail

// Submit a matting task. Returns the task ID.
inline std::string SubmitMatting(MattingTaskMgr& mgr,
                                 const MattingRegistry& registry,
                                 std::vector<uint8_t> image_buffer,
                                 const std::string& method,
                                 const std::string& image_name) {
    MattingTaskInfo initial;
    initial.method     = method;
    initial.image_name = image_name;

    return mgr.Submit(std::move(initial),
        [&registry, buf = std::move(image_buffer), method]
        (const std::string& id, auto update) mutable {
            using Clock = std::chrono::steady_clock;
            auto pipeline_start = Clock::now();

            auto provider = registry.GetShared(method);
            if (!provider) throw std::runtime_error("Unknown matting method: " + method);

            auto t0 = Clock::now();
            cv::Mat input = cv::imdecode(buf, cv::IMREAD_COLOR);
            auto t1 = Clock::now();
            if (input.empty()) throw std::runtime_error("Failed to decode image");

            MattingTimingInfo timing;
            cv::Mat mask = provider->Run(input, &timing);
            if (mask.empty()) throw std::runtime_error("Matting produced empty mask");
            if (mask.size() != input.size())
                cv::resize(mask, mask, input.size(), 0, 0, cv::INTER_NEAREST);

            cv::Mat foreground = detail::CompositeForeground(input, mask);

            auto t2 = Clock::now();
            std::vector<uint8_t> mask_png, fg_png;
            cv::imencode(".png", mask, mask_png);
            cv::imencode(".png", foreground, fg_png);
            auto t3 = Clock::now();

            auto ms = [](auto d) {
                return std::chrono::duration<double, std::milli>(d).count();
            };
            double decode_ms   = ms(t1 - t0);
            double encode_ms   = ms(t3 - t2);
            double pipeline_ms = ms(t3 - pipeline_start);

            spdlog::info("Matting task {}: decode={:.1f}ms, provider={:.1f}ms, "
                         "encode={:.1f}ms, total={:.1f}ms",
                         id.substr(0, 8), decode_ms, timing.total_ms, encode_ms, pipeline_ms);

            update([&](MattingTaskInfo& task) {
                task.timing      = timing;
                task.decode_ms   = decode_ms;
                task.encode_ms   = encode_ms;
                task.pipeline_ms = pipeline_ms;
                task.result.mask_png       = std::move(mask_png);
                task.result.foreground_png = std::move(fg_png);
                task.result.width  = input.cols;
                task.result.height = input.rows;
            });
        });
}

} // namespace ChromaPrint3D
