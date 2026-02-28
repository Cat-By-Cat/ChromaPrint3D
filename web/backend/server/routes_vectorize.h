#pragma once

#include "server_context.h"
#include "http_utils.h"
#include "request_builder.h"

#include "chromaprint3d/vectorizer.h"

#include <spdlog/spdlog.h>

#include <string>
#include <stdexcept>

using namespace ChromaPrint3D;

namespace {

inline const json* GetOptionalParam(const json& params, const char* key) {
    auto it = params.find(key);
    if (it == params.end() || it->is_null()) return nullptr;
    return &(*it);
}

template <typename T>
inline bool ReadOptionalParam(const json& params, const char* key, T& out) {
    const json* v = GetOptionalParam(params, key);
    if (!v) return false;
    out = v->get<T>();
    return true;
}

inline void ValidateVectorizerConfig(const VectorizerConfig& cfg) {
    if (cfg.num_colors < 2 || cfg.num_colors > 256) {
        throw std::runtime_error("num_colors must be in [2, 256]");
    }
    if (cfg.min_region_area < 0) { throw std::runtime_error("min_region_area must be >= 0"); }
    if (cfg.min_contour_area < 0.0f) { throw std::runtime_error("min_contour_area must be >= 0"); }
    if (cfg.min_hole_area < 0.0f) { throw std::runtime_error("min_hole_area must be >= 0"); }
    if (cfg.svg_stroke_width < 0.0f || cfg.svg_stroke_width > 20.0f) {
        throw std::runtime_error("svg_stroke_width must be in [0, 20]");
    }
    if (cfg.contour_simplify < 0.0f || cfg.contour_simplify > 10.0f) {
        throw std::runtime_error("contour_simplify must be in [0, 10]");
    }
    if (cfg.topology_cleanup < 0.0f || cfg.topology_cleanup > 10.0f) {
        throw std::runtime_error("topology_cleanup must be in [0, 10]");
    }
    if (cfg.min_coverage_ratio < 0.0f || cfg.min_coverage_ratio > 1.0f) {
        throw std::runtime_error("min_coverage_ratio must be in [0, 1]");
    }
}

inline json VectorizerConfigDefaultsToJson(const VectorizerConfig& cfg) {
    return json{
        {"num_colors", cfg.num_colors},
        {"min_region_area", cfg.min_region_area},
        {"min_contour_area", cfg.min_contour_area},
        {"min_hole_area", cfg.min_hole_area},
        {"contour_simplify", cfg.contour_simplify},
        {"topology_cleanup", cfg.topology_cleanup},
        {"enable_coverage_fix", cfg.enable_coverage_fix},
        {"min_coverage_ratio", cfg.min_coverage_ratio},
        {"svg_enable_stroke", cfg.svg_enable_stroke},
        {"svg_stroke_width", cfg.svg_stroke_width},
    };
}

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
    ReadOptionalParam(params, "num_colors", cfg.num_colors);
    ReadOptionalParam(params, "min_region_area", cfg.min_region_area);
    ReadOptionalParam(params, "min_contour_area", cfg.min_contour_area);
    ReadOptionalParam(params, "min_hole_area", cfg.min_hole_area);
    ReadOptionalParam(params, "contour_simplify", cfg.contour_simplify);
    ReadOptionalParam(params, "topology_cleanup", cfg.topology_cleanup);
    ReadOptionalParam(params, "enable_coverage_fix", cfg.enable_coverage_fix);
    ReadOptionalParam(params, "min_coverage_ratio", cfg.min_coverage_ratio);
    ReadOptionalParam(params, "svg_enable_stroke", cfg.svg_enable_stroke);
    ReadOptionalParam(params, "svg_stroke_width", cfg.svg_stroke_width);
    ValidateVectorizerConfig(cfg);
    return cfg;
}

} // namespace

inline void RegisterVectorizeRoutes(ServerContext& ctx) {
    ctx.server.Get("/api/vectorize/config/defaults",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       (void)ctx;
                       VectorizerConfig defaults;
                       SetJsonResponse(res, VectorizerConfigDefaultsToJson(defaults), 200);
                   });

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
