#include "config/server_config.h"

#include <sstream>
#include <string>
#include <unordered_map>

namespace chromaprint3d::backend {
namespace {

bool ParseI64(const std::string& s, std::int64_t& out) {
    try {
        size_t idx = 0;
        auto v     = std::stoll(s, &idx, 10);
        if (idx != s.size()) return false;
        out = v;
        return true;
    } catch (...) { return false; }
}

} // namespace

std::string BuildUsage(const char* exe_name) {
    std::ostringstream oss;
    oss << "Usage: " << exe_name << " --data <dir> [options]\n"
        << "Options:\n"
        << "  --host HOST                 Bind address (default: 0.0.0.0)\n"
        << "  --port PORT                 HTTP port (default: 8080)\n"
        << "  --data DIR                  Data root directory (required)\n"
        << "  --web DIR                   Static web root directory\n"
        << "  --model-pack PATH           Model package json path\n"
        << "  --log-level LEVEL           trace/debug/info/warn/error/off\n"
        << "  --cors-origin URL           Allowed CORS origin for cross-origin mode\n"
        << "  --max-upload-mb N           Max request body MB (default: 50)\n"
        << "  --http-threads N            Drogon HTTP worker threads (default: 4)\n"
        << "  --max-tasks N               Async task worker threads (default: 4)\n"
        << "  --max-queue N               Max queued async tasks (default: 256)\n"
        << "  --task-ttl N                Completed task TTL seconds (default: 3600)\n"
        << "  --session-ttl N             Session TTL seconds (default: 3600)\n"
        << "  --max-owner-tasks N         Max pending/running tasks per owner (default: 32)\n"
        << "  --max-result-mb N           Max total in-memory result payload MB (default: 512)\n"
        << "  --max-pixels N              Max decoded pixels per image (default: 16777216)\n"
        << "  --max-session-colordbs N    Max uploaded ColorDB count per session (default: 10)\n"
        << "  --board-cache-ttl N         Calibration board cache TTL seconds (default: 600)\n"
        << "  -h, --help                  Show this help\n";
    return oss.str();
}

ConfigParseResult ParseConfig(int argc, char** argv) {
    ConfigParseResult result;
    ServerConfig cfg;

    std::unordered_map<std::string, std::int64_t*> i64_flags = {
        {"--max-upload-mb", &cfg.max_upload_mb},
        {"--http-threads", &cfg.http_threads},
        {"--max-tasks", &cfg.max_tasks},
        {"--max-queue", &cfg.max_task_queue},
        {"--task-ttl", &cfg.task_ttl_seconds},
        {"--session-ttl", &cfg.session_ttl_seconds},
        {"--max-owner-tasks", &cfg.max_tasks_per_owner},
        {"--max-result-mb", &cfg.max_task_result_mb},
        {"--max-pixels", &cfg.max_pixels_per_image},
        {"--max-session-colordbs", &cfg.max_session_colordbs},
        {"--board-cache-ttl", &cfg.board_cache_ttl},
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            result.show_help = true;
            result.ok        = true;
            result.config    = cfg;
            return result;
        }
        if (i + 1 >= argc) {
            result.error_message = "Missing value for argument: " + arg;
            return result;
        }
        std::string value = argv[++i];

        if (arg == "--host") {
            cfg.host = value;
            continue;
        }
        if (arg == "--port") {
            std::int64_t parsed = 0;
            if (!ParseI64(value, parsed) || parsed <= 0 || parsed > 65535) {
                result.error_message = "Invalid port: " + value;
                return result;
            }
            cfg.port = static_cast<int>(parsed);
            continue;
        }
        if (arg == "--data") {
            cfg.data_dir = value;
            continue;
        }
        if (arg == "--web") {
            cfg.web_dir = value;
            continue;
        }
        if (arg == "--model-pack") {
            cfg.model_pack_path = value;
            continue;
        }
        if (arg == "--log-level") {
            cfg.log_level = value;
            continue;
        }
        if (arg == "--cors-origin") {
            cfg.cors_origin = value;
            continue;
        }

        auto it = i64_flags.find(arg);
        if (it == i64_flags.end()) {
            result.error_message = "Unknown argument: " + arg;
            return result;
        }
        std::int64_t parsed = 0;
        if (!ParseI64(value, parsed)) {
            result.error_message = "Invalid integer value for " + arg + ": " + value;
            return result;
        }
        *it->second = parsed;
    }

    if (cfg.data_dir.empty()) {
        result.error_message = "Missing required argument: --data";
        return result;
    }

    if (cfg.http_threads <= 0) cfg.http_threads = 1;
    if (cfg.max_tasks <= 0) cfg.max_tasks = 1;
    if (cfg.max_task_queue <= 0) cfg.max_task_queue = 1;
    if (cfg.task_ttl_seconds <= 0) cfg.task_ttl_seconds = 60;
    if (cfg.session_ttl_seconds <= 0) cfg.session_ttl_seconds = 60;
    if (cfg.max_task_result_mb <= 0) cfg.max_task_result_mb = 64;
    if (cfg.max_pixels_per_image <= 0) cfg.max_pixels_per_image = 1024 * 1024;
    if (cfg.max_session_colordbs <= 0) cfg.max_session_colordbs = 1;
    if (cfg.board_cache_ttl <= 0) cfg.board_cache_ttl = 60;

    cfg.allow_open_cors = cfg.cors_origin.empty();
    result.ok           = true;
    result.config       = cfg;
    return result;
}

} // namespace chromaprint3d::backend
