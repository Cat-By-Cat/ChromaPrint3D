#pragma once

#include "server_context.h"
#include "http_utils.h"

#include "chromaprint3d/matting.h"

#include <spdlog/spdlog.h>

#include <string>

using namespace ChromaPrint3D;

namespace {

inline json MattingTaskInfoToJson(const MattingTaskInfo& info) {
    json j;
    j["id"]     = info.id;
    j["status"] = TaskStatusToString(info.status);
    j["method"] = info.method;

    if (!info.error_message.empty()) {
        j["error"] = info.error_message;
    } else {
        j["error"] = nullptr;
    }

    if (info.status == TaskBase::Status::Completed) {
        j["width"]  = info.result.width;
        j["height"] = info.result.height;
        j["timing"] = {
            {"preprocess_ms", info.timing.preprocess_ms},
            {"inference_ms", info.timing.inference_ms},
            {"postprocess_ms", info.timing.postprocess_ms},
            {"decode_ms", info.decode_ms},
            {"encode_ms", info.encode_ms},
            {"provider_ms", info.timing.total_ms},
            {"pipeline_ms", info.pipeline_ms},
        };
    } else {
        j["width"]  = 0;
        j["height"] = 0;
        j["timing"] = nullptr;
    }

    return j;
}

} // namespace

inline void RegisterMattingRoutes(ServerContext& ctx) {
    ctx.server.Get("/api/matting/methods",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       json methods = json::array();
                       for (const auto& key : ctx.matting_registry.Available()) {
                           auto* provider = ctx.matting_registry.Get(key);
                           methods.push_back({
                               {"key", key},
                               {"name", provider ? provider->Name() : key},
                               {"description", provider ? provider->Description() : ""},
                           });
                       }
                       SetJsonResponse(res, json{{"methods", methods}});
                   });

    ctx.server.Post("/api/matting", [&ctx](const httplib::Request& req, httplib::Response& res) {
        AddCorsHeaders(req, res);

        if (!req.form.has_file("image")) {
            SetJsonResponse(res, ErrorJson("Missing required field: image"), 400);
            return;
        }

        httplib::FormData image_file = req.form.get_file("image");
        if (image_file.content.empty()) {
            SetJsonResponse(res, ErrorJson("Uploaded image is empty"), 400);
            return;
        }

        std::string method = "opencv";
        if (req.form.has_field("method")) { method = req.form.get_field("method"); }

        if (!ctx.matting_registry.Has(method)) {
            SetJsonResponse(res, ErrorJson("Unknown matting method: " + method), 400);
            return;
        }

        std::vector<uint8_t> image_buffer(image_file.content.begin(), image_file.content.end());
        std::string image_name = image_file.filename.empty() ? "image" : image_file.filename;

        std::string task_id = SubmitMatting(ctx.matting_task_mgr, ctx.matting_registry,
                                            std::move(image_buffer), method, image_name);

        spdlog::info("Matting task submitted: id={}, method={}", task_id.substr(0, 8), method);
        SetJsonResponse(res, json{{"task_id", task_id}}, 202);
    });

    ctx.server.Get("/api/matting/tasks/:id",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       std::string id = req.path_params.at("id");
                       if (!ctx.matting_task_mgr.WithTask(id, [&](const MattingTaskInfo& task) {
                               SetJsonResponse(res, MattingTaskInfoToJson(task));
                           })) {
                           SetJsonResponse(res, ErrorJson("Matting task not found"), 404);
                       }
                   });

    ctx.server.Get(
        "/api/matting/tasks/:id/mask", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (!ctx.matting_task_mgr.WithTask(id, [&](const MattingTaskInfo& task) {
                    if (task.status != TaskBase::Status::Completed) {
                        SetJsonResponse(res, ErrorJson("Not ready"), 404);
                        return;
                    }
                    SetBinaryResponse(res, task.result.mask_png, "image/png", "mask.png");
                })) {
                SetJsonResponse(res, ErrorJson("Matting task not found"), 404);
            }
        });

    ctx.server.Get("/api/matting/tasks/:id/foreground", [&ctx](const httplib::Request& req,
                                                               httplib::Response& res) {
        AddCorsHeaders(req, res);
        std::string id = req.path_params.at("id");
        if (!ctx.matting_task_mgr.WithTask(id, [&](const MattingTaskInfo& task) {
                if (task.status != TaskBase::Status::Completed) {
                    SetJsonResponse(res, ErrorJson("Not ready"), 404);
                    return;
                }
                SetBinaryResponse(res, task.result.foreground_png, "image/png", "foreground.png");
            })) {
            SetJsonResponse(res, ErrorJson("Matting task not found"), 404);
        }
    });

    ctx.server.Delete(
        "/api/matting/tasks/:id", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (ctx.matting_task_mgr.DeleteTask(id)) {
                SetJsonResponse(res, json{{"deleted", true}});
            } else {
                SetJsonResponse(res, ErrorJson("Task not found or still running"), 404);
            }
        });
}
