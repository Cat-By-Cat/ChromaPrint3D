#pragma once

#include "server_context.h"
#include "http_utils.h"
#include "request_builder.h"

#include "chromaprint3d/vectorizer.h"

#include <spdlog/spdlog.h>

#include <string>

using namespace ChromaPrint3D;

namespace {

inline json VectorizeTaskInfoToJson(const VectorizeTaskInfo& info) {
    json j;
    j["id"]     = info.id;
    j["status"] = TaskStatusToString(info.status);

    if (!info.error_message.empty()) {
        j["error"] = info.error_message;
    } else {
        j["error"] = nullptr;
    }

    if (info.status == TaskBase::Status::Completed) {
        j["width"]      = info.result.width;
        j["height"]     = info.result.height;
        j["num_shapes"] = info.result.num_shapes;
        j["svg_size"]   = info.result.svg_content.size();
        j["timing"]     = {
            {"decode_ms", info.decode_ms},
            {"vectorize_ms", info.vectorize_ms},
            {"pipeline_ms", info.pipeline_ms},
        };
    } else {
        j["width"]      = 0;
        j["height"]     = 0;
        j["num_shapes"] = 0;
        j["svg_size"]   = 0;
        j["timing"]     = nullptr;
    }

    return j;
}

inline VectorizerConfig ParseVectorizerConfig(const json& params) {
    VectorizerConfig cfg;
    if (params.contains("num_colors")) cfg.num_colors = params["num_colors"].get<int>();
    if (params.contains("merge_lambda")) cfg.merge_lambda = params["merge_lambda"].get<float>();
    if (params.contains("min_region_area"))
        cfg.min_region_area = params["min_region_area"].get<int>();
    if (params.contains("morph_kernel_size"))
        cfg.morph_kernel_size = params["morph_kernel_size"].get<int>();
    if (params.contains("min_contour_area"))
        cfg.min_contour_area = params["min_contour_area"].get<float>();
    if (params.contains("alpha_max")) cfg.alpha_max = params["alpha_max"].get<float>();
    if (params.contains("opt_tolerance")) cfg.opt_tolerance = params["opt_tolerance"].get<float>();
    if (params.contains("enable_curve_opt"))
        cfg.enable_curve_opt = params["enable_curve_opt"].get<bool>();
    if (params.contains("curve_tolerance"))
        cfg.curve_tolerance = params["curve_tolerance"].get<float>();
    if (params.contains("corner_threshold"))
        cfg.corner_threshold = params["corner_threshold"].get<float>();
    if (params.contains("color_space"))
        cfg.color_space = ParseColorSpace(params["color_space"].get<std::string>());
    return cfg;
}

} // namespace

inline void RegisterVectorizeRoutes(ServerContext& ctx) {
    ctx.server.Post("/api/vectorize", [&ctx](const httplib::Request& req, httplib::Response& res) {
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

        json params = json::object();
        if (req.form.has_field("params")) {
            std::string params_str = req.form.get_field("params");
            try {
                params = json::parse(params_str);
            } catch (const json::exception& e) {
                SetJsonResponse(res, ErrorJson(std::string("Invalid params JSON: ") + e.what()),
                                400);
                return;
            }
        }

        VectorizerConfig config;
        try {
            config = ParseVectorizerConfig(params);
        } catch (const std::exception& e) {
            SetJsonResponse(res, ErrorJson(std::string("Invalid parameters: ") + e.what()), 400);
            return;
        }

        std::vector<uint8_t> image_buffer(image_file.content.begin(), image_file.content.end());
        std::string image_name = image_file.filename.empty() ? "image" : image_file.filename;

        std::string task_id =
            SubmitVectorize(ctx.vectorize_task_mgr, std::move(image_buffer), config, image_name);

        spdlog::info("Vectorize task submitted: id={}, image={}", task_id.substr(0, 8), image_name);
        SetJsonResponse(res, json{{"task_id", task_id}}, 202);
    });

    ctx.server.Get("/api/vectorize/tasks/:id",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       std::string id = req.path_params.at("id");
                       if (!ctx.vectorize_task_mgr.WithTask(id, [&](const VectorizeTaskInfo& task) {
                               SetJsonResponse(res, VectorizeTaskInfoToJson(task));
                           })) {
                           SetJsonResponse(res, ErrorJson("Vectorize task not found"), 404);
                       }
                   });

    ctx.server.Get("/api/vectorize/tasks/:id/svg", [&ctx](const httplib::Request& req,
                                                          httplib::Response& res) {
        AddCorsHeaders(req, res);
        std::string id = req.path_params.at("id");
        if (!ctx.vectorize_task_mgr.WithTask(id, [&](const VectorizeTaskInfo& task) {
                if (task.status != TaskBase::Status::Completed) {
                    SetJsonResponse(res, ErrorJson("Not ready"), 404);
                    return;
                }
                res.set_content(task.result.svg_content, "image/svg+xml");
                std::string filename = task.image_name;
                auto dot             = filename.rfind('.');
                if (dot != std::string::npos) filename = filename.substr(0, dot);
                filename += ".svg";
                if (IsAsciiPrintable(filename)) {
                    res.set_header("Content-Disposition",
                                   "attachment; filename=\"" + filename + "\"");
                } else {
                    res.set_header("Content-Disposition", "attachment; filename=\"result.svg\"; "
                                                          "filename*=UTF-8''" +
                                                              PercentEncodeUTF8(filename));
                }
                res.status = 200;
            })) {
            SetJsonResponse(res, ErrorJson("Vectorize task not found"), 404);
        }
    });

    ctx.server.Delete(
        "/api/vectorize/tasks/:id", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (ctx.vectorize_task_mgr.DeleteTask(id)) {
                SetJsonResponse(res, json{{"deleted", true}});
            } else {
                SetJsonResponse(res, ErrorJson("Task not found or still running"), 404);
            }
        });
}
