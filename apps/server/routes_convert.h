#pragma once

#include "server_context.h"
#include "http_utils.h"
#include "request_builder.h"

#include <spdlog/spdlog.h>

#include <string>
#include <vector>

inline void RegisterConvertRoutes(ServerContext& ctx) {
    ctx.server.Post("/api/convert", [&ctx](const httplib::Request& req, httplib::Response& res) {
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

        std::vector<uint8_t> image_buffer(image_file.content.begin(), image_file.content.end());
        std::string image_name = image_file.filename;
        if (image_name.empty()) { image_name = "upload"; }
        auto dot_pos = image_name.rfind('.');
        if (dot_pos != std::string::npos) { image_name = image_name.substr(0, dot_pos); }

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

        ConvertRasterRequest convert_req;
        try {
            convert_req = BuildConvertRasterRequest(params, image_buffer, image_name, ctx.db_cache,
                                                    ctx.model_pack, session);
        } catch (const std::exception& e) {
            SetJsonResponse(res, ErrorJson(std::string("Invalid parameters: ") + e.what()), 400);
            return;
        }

        std::string task_id = SubmitConvertRaster(ctx.task_mgr, std::move(convert_req));
        spdlog::info("Task submitted: id={}, image={}, size={} bytes", task_id, image_name,
                     image_buffer.size());
        SetJsonResponse(res, json{{"task_id", task_id}}, 202);
    });

    ctx.server.Get("/api/tasks/:id", [&ctx](const httplib::Request& req, httplib::Response& res) {
        AddCorsHeaders(req, res);
        std::string id = req.path_params.at("id");
        if (!ctx.task_mgr.WithTask(id, [&](const ConvertTaskInfo& task) {
                SetJsonResponse(res, TaskInfoToJson(task));
            })) {
            SetJsonResponse(res, ErrorJson("Task not found"), 404);
        }
    });

    ctx.server.Get(
        "/api/tasks/:id/result", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (!ctx.task_mgr.WithTask(id, [&](const ConvertTaskInfo& task) {
                    if (task.status != TaskBase::Status::Completed) {
                        SetJsonResponse(res, ErrorJson("Task not completed"), 409);
                        return;
                    }
                    if (task.result.model_3mf.empty()) {
                        SetJsonResponse(res, ErrorJson("3MF result not available"), 404);
                        return;
                    }
                    std::string filename =
                        (task.input_name.empty() ? task.id.substr(0, 8) : task.input_name) + ".3mf";
                    SetBinaryResponse(res, task.result.model_3mf,
                                      "application/vnd.ms-package.3dmanufacturing-3dmodel+xml",
                                      filename);
                })) {
                SetJsonResponse(res, ErrorJson("Task not found"), 404);
            }
        });

    ctx.server.Get("/api/tasks/:id/preview",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       std::string id = req.path_params.at("id");
                       if (!ctx.task_mgr.WithTask(id, [&](const ConvertTaskInfo& task) {
                               if (task.status != TaskBase::Status::Completed) {
                                   SetJsonResponse(res, ErrorJson("Task not completed"), 409);
                                   return;
                               }
                               if (task.result.preview_png.empty()) {
                                   SetJsonResponse(res, ErrorJson("Preview not available"), 404);
                                   return;
                               }
                               SetBinaryResponse(res, task.result.preview_png, "image/png");
                           })) {
                           SetJsonResponse(res, ErrorJson("Task not found"), 404);
                       }
                   });

    ctx.server.Get(
        "/api/tasks/:id/source-mask", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (!ctx.task_mgr.WithTask(id, [&](const ConvertTaskInfo& task) {
                    if (task.status != TaskBase::Status::Completed) {
                        SetJsonResponse(res, ErrorJson("Task not completed"), 409);
                        return;
                    }
                    if (task.result.source_mask_png.empty()) {
                        SetJsonResponse(res, ErrorJson("Source mask not available"), 404);
                        return;
                    }
                    SetBinaryResponse(res, task.result.source_mask_png, "image/png");
                })) {
                SetJsonResponse(res, ErrorJson("Task not found"), 404);
            }
        });

    ctx.server.Delete(
        "/api/tasks/:id", [&ctx](const httplib::Request& req, httplib::Response& res) {
            AddCorsHeaders(req, res);
            std::string id = req.path_params.at("id");
            if (ctx.task_mgr.DeleteTask(id)) {
                SetJsonResponse(res, json{{"deleted", true}});
            } else {
                auto task = ctx.task_mgr.GetTask(id);
                if (!task.has_value()) {
                    SetJsonResponse(res, ErrorJson("Task not found"), 404);
                } else {
                    SetJsonResponse(res, ErrorJson("Cannot delete running task"), 409);
                }
            }
        });

    ctx.server.Get("/api/tasks", [&ctx](const httplib::Request& req, httplib::Response& res) {
        AddCorsHeaders(req, res);
        json arr = json::array();
        ctx.task_mgr.ForEachTask(
            [&arr](const ConvertTaskInfo& t) { arr.push_back(TaskInfoToJson(t)); });
        SetJsonResponse(res, json{{"tasks", arr}});
    });
}
