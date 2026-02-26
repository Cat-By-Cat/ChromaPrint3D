#pragma once

#include "server_context.h"
#include "http_utils.h"
#include "color_db_cache.h"
#include "request_builder.h"
#include "session.h"

#include "chromaprint3d/pipeline.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <string>
#include <vector>

using json = nlohmann::json;

inline ConvertVectorRequest
BuildConvertVectorRequest(const json& params, const std::vector<uint8_t>& svg_buffer,
                          const std::string& svg_name, const ColorDBCache& db_cache,
                          const ModelPackage* model_pack, UserSession* session) {
    ConvertVectorRequest req;
    req.svg_buffer = svg_buffer;
    req.svg_name   = svg_name;

    if (params.contains("db_names") && params["db_names"].is_array()) {
        std::vector<const ColorDB*> selected;
        for (const auto& name_val : params["db_names"]) {
            std::string name  = name_val.get<std::string>();
            const ColorDB* db = db_cache.FindByName(name);
            if (!db && session) {
                auto it = session->color_dbs.find(name);
                if (it != session->color_dbs.end()) { db = &it->second; }
            }
            if (!db) { throw std::runtime_error("ColorDB not found: " + name); }
            selected.push_back(db);
        }
        if (selected.empty()) { throw std::runtime_error("No valid ColorDB names provided"); }
        req.preloaded_dbs = std::move(selected);
    } else {
        req.preloaded_dbs = db_cache.GetAll();
    }

    if (model_pack) { req.preloaded_model_pack = model_pack; }

    if (params.contains("target_width_mm")) {
        req.target_width_mm = params["target_width_mm"].get<float>();
    }
    if (params.contains("target_height_mm")) {
        req.target_height_mm = params["target_height_mm"].get<float>();
    }
    if (params.contains("print_mode")) {
        req.print_mode = ParsePrintMode(params["print_mode"].get<std::string>());
    }
    if (params.contains("color_space")) {
        req.color_space = ParseColorSpace(params["color_space"].get<std::string>());
    }
    if (params.contains("k_candidates")) { req.k_candidates = params["k_candidates"].get<int>(); }
    if (params.contains("flip_y")) { req.flip_y = params["flip_y"].get<bool>(); }
    if (params.contains("layer_height_mm")) {
        req.layer_height_mm = params["layer_height_mm"].get<float>();
    }
    if (params.contains("tessellation_tolerance_mm")) {
        req.tessellation_tolerance_mm = params["tessellation_tolerance_mm"].get<float>();
    }
    if (params.contains("gradient_dither")) {
        std::string d = params["gradient_dither"].get<std::string>();
        if (d == "blue_noise") {
            req.gradient_dither = DitherMethod::BlueNoise;
        } else if (d == "floyd_steinberg") {
            req.gradient_dither = DitherMethod::FloydSteinberg;
        } else if (d == "none") {
            req.gradient_dither = DitherMethod::None;
        } else {
            throw std::runtime_error("Invalid gradient_dither method: " + d);
        }
    }
    if (params.contains("gradient_dither_strength")) {
        req.gradient_dither_strength = params["gradient_dither_strength"].get<float>();
        if (req.gradient_dither_strength < 0.0f || req.gradient_dither_strength > 1.0f) {
            throw std::runtime_error("gradient_dither_strength must be in [0, 1]");
        }
    }
    if (params.contains("gradient_pixel_mm")) {
        req.gradient_pixel_mm = params["gradient_pixel_mm"].get<float>();
    }
    if (params.contains("allowed_channels") && params["allowed_channels"].is_array()) {
        for (const auto& ch : params["allowed_channels"]) {
            std::string color    = ch.value("color", "");
            std::string material = ch.value("material", "");
            if (!color.empty()) { req.allowed_channel_keys.push_back(color + "|" + material); }
        }
    }
    if (params.contains("generate_preview")) {
        req.generate_preview = params["generate_preview"].get<bool>();
    }

    return req;
}

inline std::string SubmitConvertVector(ConvertTaskManager& mgr, ConvertVectorRequest request) {
    ConvertTaskInfo initial;
    initial.input_name = request.svg_name;

    return mgr.Submit(std::move(initial), [req = std::move(request)](const std::string& /*id*/,
                                                                     auto update) mutable {
        ProgressCallback progress_cb = [&update](ConvertStage stage, float progress) {
            update([&](ConvertTaskInfo& task) {
                task.stage    = stage;
                task.progress = progress;
            });
        };

        ConvertResult result = ConvertVector(req, progress_cb);
        update([&](ConvertTaskInfo& task) {
            task.result   = std::move(result);
            task.progress = 1.0f;
        });
    });
}

inline void RegisterConvertVectorRoutes(ServerContext& ctx) {
    ctx.server.Post("/api/convert-svg", [&ctx](const httplib::Request& req,
                                               httplib::Response& res) {
        AddCorsHeaders(req, res);

        if (!req.form.has_file("svg")) {
            SetJsonResponse(res, ErrorJson("Missing required field: svg"), 400);
            return;
        }

        httplib::FormData svg_file = req.form.get_file("svg");
        if (svg_file.content.empty()) {
            SetJsonResponse(res, ErrorJson("Uploaded SVG is empty"), 400);
            return;
        }

        std::vector<uint8_t> svg_buffer(svg_file.content.begin(), svg_file.content.end());
        std::string svg_name = svg_file.filename;
        if (svg_name.empty()) { svg_name = "upload"; }
        auto dot_pos = svg_name.rfind('.');
        if (dot_pos != std::string::npos) { svg_name = svg_name.substr(0, dot_pos); }

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

        UserSession* session = nullptr;
        std::string token    = GetSessionToken(req);
        if (!token.empty()) { session = ctx.session_mgr.Find(token); }

        ConvertVectorRequest convert_req;
        try {
            convert_req = BuildConvertVectorRequest(params, svg_buffer, svg_name, ctx.db_cache,
                                                    ctx.model_pack, session);
        } catch (const std::exception& e) {
            SetJsonResponse(res, ErrorJson(std::string("Invalid parameters: ") + e.what()), 400);
            return;
        }

        std::string task_id = SubmitConvertVector(ctx.task_mgr, std::move(convert_req));
        spdlog::info("SVG task submitted: id={}, svg={}, size={} bytes", task_id, svg_name,
                     svg_buffer.size());
        SetJsonResponse(res, json{{"task_id", task_id}}, 202);
    });
}
