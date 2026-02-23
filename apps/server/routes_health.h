#pragma once

#include "server_context.h"
#include "http_utils.h"
#include "chromaprint3d/version.h"

inline void RegisterHealthRoutes(ServerContext& ctx) {
    ctx.server.Get("/api/health",
                   [&ctx](const httplib::Request& req, httplib::Response& res) {
                       AddCorsHeaders(req, res);
                       int active = ctx.task_mgr.ActiveTaskCount()
                                  + ctx.matting_task_mgr.ActiveTaskCount();
                       int total  = ctx.task_mgr.TotalTaskCount()
                                  + ctx.matting_task_mgr.TotalTaskCount();
                       json j = {
                           {"status", "ok"},
                           {"version", CHROMAPRINT3D_VERSION_STRING},
                           {"active_tasks", active},
                           {"total_tasks", total},
                       };
                       SetJsonResponse(res, j);
                   });
}
