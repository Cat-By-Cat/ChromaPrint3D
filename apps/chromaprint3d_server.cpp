#include "server/server_options.h"
#include "server/server_context.h"
#include "server/http_utils.h"
#include "server/routes_health.h"
#include "server/routes_convert.h"
#include "server/routes_colordb.h"
#include "server/routes_calibration.h"
#include "server/routes_session.h"
#include "server/routes_matting.h"

#include "chromaprint3d/logging.h"
#include "chromaprint3d/model_package.h"
#include "chromaprint3d/matting.h"

#if defined(CHROMAPRINT3D_HAS_INFER) && CHROMAPRINT3D_HAS_INFER
#include <chromaprint3d/infer/infer.h>
#endif

#include <spdlog/spdlog.h>

#include <chrono>
#include <filesystem>
#include <optional>

using namespace ChromaPrint3D;

namespace {

// ── Data Loading ─────────────────────────────────────────────────────────

ColorDBCache LoadColorDBs(const std::filesystem::path& data_root) {
    auto dbs_dir = data_root / "dbs";
    if (!std::filesystem::is_directory(dbs_dir))
        dbs_dir = data_root;

    spdlog::info("Loading ColorDBs from: {}", dbs_dir.string());
    ColorDBCache cache;
    cache.LoadFromDirectory(dbs_dir.string());
    spdlog::info("Loaded {} ColorDB(s)", cache.databases.size());
    return cache;
}

std::optional<ModelPackage> TryLoadModelPackage(const std::string& path) {
    if (path.empty()) return std::nullopt;
    auto pack = ModelPackage::LoadFromJson(path);
    spdlog::info("Loaded model package: {}", pack.name);
    return pack;
}

EightColorRecipeStore TryLoadRecipes(const std::filesystem::path& data_root) {
    auto recipe_path = data_root / "recipes" / "8color_boards.json";
    if (!std::filesystem::is_regular_file(recipe_path)) {
        spdlog::info("No 8-color recipe file found at {}", recipe_path.string());
        return {};
    }
    try {
        return EightColorRecipeStore::LoadFromFile(recipe_path.string());
    } catch (const std::exception& e) {
        spdlog::warn("Failed to load 8-color recipes: {}", e.what());
        return {};
    }
}

// ── Matting ──────────────────────────────────────────────────────────────

#if defined(CHROMAPRINT3D_HAS_INFER) && CHROMAPRINT3D_HAS_INFER
void LoadMattingModels(MattingRegistry& registry,
                       infer::InferenceEngine& engine,
                       const std::filesystem::path& model_dir) {
    auto discovered = DiscoverMattingModels(model_dir.string());
    if (discovered.empty()) return;

    spdlog::info("Inference engine ready, backends: {}",
                 engine.AvailableBackends().size());

    infer::SessionOptions sess_opts;
    if (engine.SupportsDevice(infer::DeviceType::kCUDA)) {
        sess_opts.device = infer::Device::CUDA();
        spdlog::info("CUDA available, matting models will use GPU");
    } else {
        sess_opts.device = infer::Device::CPU();
    }

    for (auto& [model_path, config] : discovered) {
        std::string stem = std::filesystem::path(model_path).stem().string();
        try {
            auto session  = engine.LoadModel(model_path, sess_opts);
            auto provider = CreateDLMattingProvider(stem, std::move(session), config);
            registry.Register(stem, std::move(provider));
        } catch (const std::exception& e) {
            spdlog::error("Failed to load matting model '{}': {}", stem, e.what());
        }
    }
}
#endif

void WarnIfModelsUnusable(const std::filesystem::path& model_dir) {
    auto discovered = DiscoverMattingModels(model_dir.string());
    if (!discovered.empty()) {
        spdlog::warn("Found {} matting model(s) but infer module is not available; "
                     "only opencv matting will be usable",
                     discovered.size());
    }
}

void LogMattingProviders(const MattingRegistry& registry) {
    auto keys = registry.Available();
    std::string joined;
    for (const auto& k : keys) {
        if (!joined.empty()) joined += ", ";
        joined += k;
    }
    spdlog::info("Matting providers: [{}]", joined);
}

// ── HTTP Server ──────────────────────────────────────────────────────────

void ConfigureServer(httplib::Server& svr, const ServerOptions& opts) {
    svr.set_payload_max_length(
        static_cast<size_t>(opts.max_upload_mb) * 1024 * 1024);

    if (!opts.web_dir.empty()) {
        if (std::filesystem::is_directory(opts.web_dir)) {
            svr.set_mount_point("/", opts.web_dir);
            spdlog::info("Serving static files from: {}", opts.web_dir);
        } else {
            spdlog::warn("--web directory does not exist: {}", opts.web_dir);
        }
    }

    svr.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        AddCorsHeaders(req, res);
        json j = ErrorJson("Not found");
        if (res.status == 413) j = ErrorJson("Payload too large");
        res.set_content(j.dump(), "application/json");
    });

    svr.set_exception_handler(
        [](const httplib::Request& req, httplib::Response& res, std::exception_ptr ep) {
            AddCorsHeaders(req, res);
            std::string msg = "Internal server error";
            try {
                if (ep) std::rethrow_exception(ep);
            } catch (const std::exception& e) {
                msg = e.what();
            } catch (...) {}
            spdlog::error("Unhandled exception: {}", msg);
            res.set_content(ErrorJson(msg).dump(), "application/json");
            res.status = 500;
        });

    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        if (req.path == "/api/health") return;
        auto elapsed = std::chrono::steady_clock::now() - req.start_time_;
        auto ms      = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        spdlog::info("{} {} {} {} {}ms",
                     req.remote_addr, req.method, req.path, res.status, ms);
    });
}

void RegisterAllRoutes(ServerContext& ctx) {
    RegisterHealthRoutes(ctx);
    RegisterConvertRoutes(ctx);
    RegisterColorDBRoutes(ctx);
    RegisterCalibrationRoutes(ctx);
    RegisterSessionRoutes(ctx);
    RegisterMattingRoutes(ctx);
}

} // namespace

// ── Entry Point ──────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    // 1. Parse arguments & initialize logging
    ServerOptions opts;
    if (!ParseArgs(argc, argv, opts)) return 1;

    InitLogging(ParseLogLevel(opts.log_level));

    spdlog::info("ChromaPrint3D Server v1.0.0");
    spdlog::info("Configuration: port={}, host={}, data={}, max_upload={}MB, max_tasks={}, "
                 "task_ttl={}s, log_level={}, cors_origin={}",
                 opts.port, opts.host, opts.data_dir, opts.max_upload_mb, opts.max_tasks,
                 opts.task_ttl_seconds, opts.log_level,
                 opts.cors_origin.empty() ? "(allow all)" : opts.cors_origin);

    if (!opts.cors_origin.empty()) {
        CorsAllowedOrigin() = opts.cors_origin;
    }

    // 2. Load data resources
    auto data_root = std::filesystem::path(opts.data_dir);

    ColorDBCache db_cache;
    try {
        db_cache = LoadColorDBs(data_root);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load ColorDBs: {}", e.what());
        return 1;
    }

    std::optional<ModelPackage> model_pack;
    try {
        model_pack = TryLoadModelPackage(opts.model_pack_path);
    } catch (const std::exception& e) {
        spdlog::error("Failed to load model package: {}", e.what());
        return 1;
    }

    auto recipe_store = TryLoadRecipes(data_root);

    // 3. Create services
    ConvertTaskManager task_mgr(opts.max_tasks, opts.task_ttl_seconds);

    SessionManager session_mgr;
    session_mgr.ttl_seconds = opts.task_ttl_seconds;

    BoardCache board_cache;
    board_cache.ttl_seconds = 600;

    BoardGeometryCache geometry_cache;

    // 4. Set up matting providers
    MattingRegistry matting_registry;
    matting_registry.Register("opencv", std::make_shared<ThresholdMattingProvider>());

    auto matting_model_dir = data_root / "models" / "matting";
#if defined(CHROMAPRINT3D_HAS_INFER) && CHROMAPRINT3D_HAS_INFER
    auto infer_engine = infer::InferenceEngine::Create();
    LoadMattingModels(matting_registry, infer_engine, matting_model_dir);
#else
    WarnIfModelsUnusable(matting_model_dir);
#endif
    LogMattingProviders(matting_registry);

    MattingTaskMgr matting_task_mgr(opts.max_tasks, opts.task_ttl_seconds);

    // 5. Create and configure HTTP server
    httplib::Server svr;
    ConfigureServer(svr, opts);

    ServerContext ctx{svr,         task_mgr,       db_cache,
                     model_pack ? &model_pack.value() : nullptr,
                     session_mgr,  board_cache,    geometry_cache,
                     recipe_store, matting_registry, matting_task_mgr};
    RegisterAllRoutes(ctx);

    // 6. Start listening
    spdlog::info("Starting server on {}:{}", opts.host, opts.port);
    if (!svr.listen(opts.host, opts.port)) {
        spdlog::error("Failed to start server on {}:{}", opts.host, opts.port);
        return 1;
    }

    return 0;
}
