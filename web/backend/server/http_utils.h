#pragma once

#include "task_manager.h"

#include "chromaprint3d/color_db.h"

#include <httplib/httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace ChromaPrint3D;
using json = nlohmann::json;

inline json ErrorJson(const std::string& message) { return json{{"error", message}}; }

inline void SetJsonResponse(httplib::Response& res, const json& j, int status = 200) {
    res.set_content(j.dump(), "application/json");
    res.status = status;
}

/// Percent-encode a UTF-8 string for use in Content-Disposition filename* (RFC 5987).
inline std::string PercentEncodeUTF8(const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            const char hex[] = "0123456789ABCDEF";
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

/// Check if a string contains only ASCII printable characters (no need for RFC 5987 encoding).
inline bool IsAsciiPrintable(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c > 0x7E || c == '"' || c == '\\') return false;
    }
    return true;
}

inline void SetBinaryResponse(httplib::Response& res, const std::vector<uint8_t>& data,
                              const std::string& content_type, const std::string& filename = "") {
    res.set_content(std::string(reinterpret_cast<const char*>(data.data()), data.size()),
                    content_type);
    if (!filename.empty()) {
        if (IsAsciiPrintable(filename)) {
            res.set_header("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        } else {
            res.set_header("Content-Disposition",
                           "attachment; filename=\"download\"; filename*=UTF-8''" +
                               PercentEncodeUTF8(filename));
        }
    }
    res.status = 200;
}

// Global CORS origin configuration (set once at startup, read-only afterwards).
// Empty = allow all origins (development / single-origin mode).
// Non-empty = only allow this specific origin (cross-origin production mode).
inline std::string& CorsAllowedOrigin() {
    static std::string origin;
    return origin;
}

inline bool IsCrossOriginMode() { return !CorsAllowedOrigin().empty(); }

inline void AddCorsHeaders(const httplib::Request& req, httplib::Response& res) {
    std::string origin  = req.has_header("Origin") ? req.get_header_value("Origin") : "";
    const auto& allowed = CorsAllowedOrigin();

    if (allowed.empty()) {
        if (!origin.empty()) {
            res.set_header("Access-Control-Allow-Origin", origin);
        } else {
            res.set_header("Access-Control-Allow-Origin", "*");
        }
    } else {
        if (origin != allowed) { return; }
        res.set_header("Access-Control-Allow-Origin", allowed);
    }
    res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");
    res.set_header("Access-Control-Allow-Credentials", "true");
    res.set_header("Access-Control-Max-Age", "86400");
}

inline json TaskInfoToJson(const ConvertTaskInfo& info) {
    json j;
    j["id"]       = info.id;
    j["status"]   = TaskStatusToString(info.status);
    j["stage"]    = ConvertStageToString(info.stage);
    j["progress"] = info.progress;

    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(info.created_at.time_since_epoch())
            .count();
    j["created_at_ms"] = elapsed_ms;

    if (!info.error_message.empty()) {
        j["error"] = info.error_message;
    } else {
        j["error"] = nullptr;
    }

    if (info.status == TaskBase::Status::Completed) {
        j["result"] = {
            {"input_width", info.result.input_width},
            {"input_height", info.result.input_height},
            {"resolved_pixel_mm", info.result.resolved_pixel_mm},
            {"physical_width_mm", info.result.physical_width_mm},
            {"physical_height_mm", info.result.physical_height_mm},
            {"stats",
             {{"clusters_total", info.result.stats.clusters_total},
              {"db_only", info.result.stats.db_only},
              {"model_fallback", info.result.stats.model_fallback},
              {"model_queries", info.result.stats.model_queries},
              {"avg_db_de", info.result.stats.avg_db_de},
              {"avg_model_de", info.result.stats.avg_model_de}}},
            {"has_3mf", !info.result.model_3mf.empty()},
            {"has_preview", !info.result.preview_png.empty()},
            {"has_source_mask", !info.result.source_mask_png.empty()},
        };
    } else {
        j["result"] = nullptr;
    }

    return j;
}

inline json ColorDBInfoToJson(const ColorDB& db, const std::string& material_type = "",
                              const std::string& vendor = "") {
    json j;
    j["name"]             = db.name;
    j["num_channels"]     = db.NumChannels();
    j["num_entries"]      = db.entries.size();
    j["max_color_layers"] = db.max_color_layers;
    j["base_layers"]      = db.base_layers;
    j["layer_height_mm"]  = db.layer_height_mm;
    j["line_width_mm"]    = db.line_width_mm;
    j["material_type"]    = material_type;
    j["vendor"]           = vendor;

    json palette = json::array();
    for (const auto& ch : db.palette) {
        palette.push_back({{"color", ch.color}, {"material", ch.material}});
    }
    j["palette"] = palette;
    return j;
}
