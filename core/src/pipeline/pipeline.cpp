#include "chromaprint3d/pipeline.h"
#include "chromaprint3d/color_db.h"
#include "chromaprint3d/encoding.h"
#include "chromaprint3d/voxel.h"
#include "chromaprint3d/mesh.h"
#include "chromaprint3d/export_3mf.h"
#include "chromaprint3d/slicer_preset.h"
#include "chromaprint3d/raster_proc.h"
#include "chromaprint3d/recipe_map.h"
#include "chromaprint3d/print_profile.h"
#include "chromaprint3d/error.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ChromaPrint3D {
namespace {

void NotifyProgress(const ProgressCallback& cb, ConvertStage stage, float progress) {
    if (cb) { cb(stage, progress); }
}

std::vector<ColorDB> LoadColorDBsFromPaths(const std::vector<std::string>& resolved_paths) {
    std::vector<ColorDB> dbs;
    dbs.reserve(resolved_paths.size());
    for (const std::string& path : resolved_paths) { dbs.push_back(ColorDB::LoadFromJson(path)); }
    return dbs;
}

} // namespace

std::vector<std::string> ResolveDBPaths(const std::vector<std::string>& input_paths) {
    if (input_paths.empty()) { throw InputError("No ColorDB paths provided"); }

    spdlog::debug("ResolveDBPaths: {} input path(s)", input_paths.size());
    std::set<std::string> unique_paths;
    for (const std::string& raw_path : input_paths) {
        const std::filesystem::path p(raw_path);
        if (std::filesystem::is_regular_file(p)) {
            spdlog::debug("  file: {}", p.string());
            unique_paths.insert(p.string());
            continue;
        }
        if (std::filesystem::is_directory(p)) {
            spdlog::debug("  directory: {}", p.string());
            for (const auto& entry : std::filesystem::directory_iterator(p)) {
                if (!entry.is_regular_file()) { continue; }
                if (entry.path().extension() == ".json") {
                    unique_paths.insert(entry.path().string());
                }
            }
            continue;
        }
        throw IOError("Invalid ColorDB path: " + raw_path);
    }

    if (unique_paths.empty()) { throw IOError("No json ColorDB files found from provided inputs"); }
    spdlog::debug("ResolveDBPaths: resolved {} unique file(s)", unique_paths.size());
    return std::vector<std::string>(unique_paths.begin(), unique_paths.end());
}

ConvertResult ConvertRaster(const ConvertRasterRequest& request, ProgressCallback progress) {
    spdlog::info("ConvertRaster started: image={}, dbs={}, model_pack={}",
                 request.image_path.empty() ? "(buffer)" : request.image_path,
                 request.preloaded_dbs.empty() ? request.db_paths.size()
                                               : request.preloaded_dbs.size(),
                 request.preloaded_model_pack      ? "preloaded"
                 : request.model_pack_path.empty() ? "none"
                                                   : request.model_pack_path);

    // === 1. Load resources ===
    NotifyProgress(progress, ConvertStage::LoadingResources, 0.0f);

    // Load or reference ColorDBs
    std::vector<ColorDB> owned_dbs;
    std::vector<const ColorDB*> db_ptrs;

    if (!request.preloaded_dbs.empty()) {
        db_ptrs = request.preloaded_dbs;
    } else {
        const std::vector<std::string> resolved = ResolveDBPaths(request.db_paths);
        owned_dbs                               = LoadColorDBsFromPaths(resolved);
        db_ptrs.reserve(owned_dbs.size());
        for (const ColorDB& db : owned_dbs) { db_ptrs.push_back(&db); }
    }

    if (db_ptrs.empty()) { throw InputError("No ColorDB available"); }
    spdlog::info("Loaded {} ColorDB(s)", db_ptrs.size());

    // Build a contiguous span of ColorDBs for the API that requires span<const ColorDB>
    // We need a contiguous vector for span, so copy pointers' data if using preloaded
    std::vector<ColorDB> db_span_storage;
    if (!request.preloaded_dbs.empty()) {
        db_span_storage.reserve(db_ptrs.size());
        for (const ColorDB* p : db_ptrs) { db_span_storage.push_back(*p); }
    }
    const std::vector<ColorDB>& dbs_ref =
        request.preloaded_dbs.empty() ? owned_dbs : db_span_storage;

    // Load filament config if preset_dir is available
    std::optional<FilamentConfig> fil_config;
    if (!request.preset_dir.empty()) {
        fil_config.emplace(FilamentConfig::LoadFromDir(request.preset_dir));
    }

    // Build PrintProfile
    PrintProfile profile = PrintProfile::BuildFromColorDBs(dbs_ref, request.print_mode,
                                                           fil_config ? &*fil_config : nullptr);

    // Filter channels if requested
    if (!request.allowed_channel_keys.empty()) {
        profile.FilterChannels(request.allowed_channel_keys);
        spdlog::info("Filtered profile palette to {} channel(s)", profile.NumChannels());
    }

    // Load model package if needed
    std::optional<ModelPackage> owned_model_pack;
    const ModelPackage* model_pack_ptr = request.preloaded_model_pack;
    if (!model_pack_ptr && !request.model_pack_path.empty()) {
        owned_model_pack.emplace(ModelPackage::LoadFromJson(request.model_pack_path));
        model_pack_ptr = &owned_model_pack.value();
    }

    spdlog::info("Model pack: {}", model_pack_ptr ? "loaded" : "none");
    NotifyProgress(progress, ConvertStage::LoadingResources, 1.0f);
    spdlog::info("Stage: loading_resources completed");

    // === 2. Process image ===
    NotifyProgress(progress, ConvertStage::Preprocessing, 0.0f);

    // Resolve pixel_mm early (needed for target_mm -> pixel conversion)
    const float resolved_pixel_mm =
        (request.pixel_mm > 0.0f) ? request.pixel_mm
                                  : (profile.line_width_mm > 0.0f ? profile.line_width_mm : 0.42f);

    const bool use_target_mm = request.target_width_mm > 0.0f || request.target_height_mm > 0.0f;

    RasterProcConfig imgproc_cfg;
    if (use_target_mm) {
        if (request.target_width_mm > 0.0f)
            imgproc_cfg.max_width =
                static_cast<int>(std::floor(request.target_width_mm / resolved_pixel_mm));
        if (request.target_height_mm > 0.0f)
            imgproc_cfg.max_height =
                static_cast<int>(std::floor(request.target_height_mm / resolved_pixel_mm));
        imgproc_cfg.scale = 1e6f; // allow upscaling to fill target
        spdlog::info("Target-size mode: {:.1f}x{:.1f} mm -> {}x{} px (pixel_mm={:.3f})",
                     request.target_width_mm, request.target_height_mm, imgproc_cfg.max_width,
                     imgproc_cfg.max_height, resolved_pixel_mm);
    } else {
        imgproc_cfg.scale      = request.scale;
        imgproc_cfg.max_width  = request.max_width;
        imgproc_cfg.max_height = request.max_height;
    }
    RasterProc imgproc(imgproc_cfg);

    RasterProcResult img;
    if (!request.image_buffer.empty()) {
        img = imgproc.RunFromBuffer(request.image_buffer, request.image_name);
    } else if (!request.image_path.empty()) {
        img = imgproc.Run(request.image_path);
    } else {
        throw InputError("No image input provided (neither path nor buffer)");
    }

    NotifyProgress(progress, ConvertStage::Preprocessing, 1.0f);
    spdlog::info("Stage: preprocessing completed, result={}x{}", img.width, img.height);

    // === 3. Match colors ===
    NotifyProgress(progress, ConvertStage::Matching, 0.0f);

    MatchConfig match_cfg;
    match_cfg.color_space     = request.color_space;
    match_cfg.k_candidates    = request.k_candidates;
    match_cfg.cluster_count   = request.cluster_count;
    match_cfg.dither          = request.dither;
    match_cfg.dither_strength = request.dither_strength;

    ModelGateConfig model_gate;
    model_gate.enable     = false;
    model_gate.model_only = false;
    if (model_pack_ptr) {
        model_gate.model_only = request.model_only;
        model_gate.enable     = request.model_only ? true : request.model_enable;
        model_gate.threshold  = (request.model_threshold >= 0.0f)
                                    ? request.model_threshold
                                    : model_pack_ptr->default_threshold;
        model_gate.margin =
            (request.model_margin >= 0.0f) ? request.model_margin : model_pack_ptr->default_margin;
    }

    MatchStats match_stats;
    RecipeMap recipe_map = RecipeMap::MatchFromRaster(img, dbs_ref, profile, match_cfg,
                                                      model_pack_ptr, model_gate, &match_stats);

    NotifyProgress(progress, ConvertStage::Matching, 1.0f);
    spdlog::info("Stage: matching completed, clusters={}, db_only={}, model_fallback={}, "
                 "avg_db_de={:.2f}, avg_model_de={:.2f}",
                 match_stats.clusters_total, match_stats.db_only, match_stats.model_fallback,
                 match_stats.avg_db_de, match_stats.avg_model_de);

    // === 4. Build result ===
    ConvertResult result;
    result.stats        = match_stats;
    result.input_width  = img.width;
    result.input_height = img.height;

    // Generate preview
    if (request.generate_preview) {
        cv::Mat preview_bgr = recipe_map.ToBgrImage(255, 255, 255);
        if (!preview_bgr.empty()) {
            result.preview_png = EncodePng(preview_bgr);
            if (!request.preview_path.empty()) { SaveImage(preview_bgr, request.preview_path); }
        }
    }

    // Generate source mask
    if (request.generate_source_mask) {
        cv::Mat source_mask = recipe_map.ToSourceMaskImage();
        if (!source_mask.empty()) {
            result.source_mask_png = EncodePng(source_mask);
            if (!request.source_mask_path.empty()) {
                SaveImage(source_mask, request.source_mask_path);
            }
        }
    }

    result.layer_previews.layers      = profile.color_layers;
    result.layer_previews.width       = img.width;
    result.layer_previews.height      = img.height;
    result.layer_previews.layer_order = profile.layer_order;
    result.layer_previews.palette.reserve(profile.palette.size());
    for (std::size_t i = 0; i < profile.palette.size(); ++i) {
        const auto& channel = profile.palette[i];
        result.layer_previews.palette.push_back(
            LayerPreviewChannel{static_cast<int>(i), channel.color, channel.material});
    }
    result.layer_previews.layer_pngs.resize(static_cast<std::size_t>(profile.color_layers));
    std::atomic<bool> has_layer_preview_error{false};
    std::string layer_preview_error_message;
#ifdef _OPENMP
#    pragma omp parallel for schedule(dynamic)
#endif
    for (int layer_idx = 0; layer_idx < profile.color_layers; ++layer_idx) {
        if (has_layer_preview_error.load(std::memory_order_relaxed)) { continue; }

        try {
            cv::Mat layer_bgr =
                recipe_map.ToLayerBgrImage(layer_idx, profile.palette, 255, 255, 255);
            if (layer_bgr.empty()) {
                throw InputError("Failed to render raster layer preview image");
            }
            result.layer_previews.layer_pngs[static_cast<std::size_t>(layer_idx)] =
                EncodePng(layer_bgr);
        } catch (const std::exception& e) {
#ifdef _OPENMP
#    pragma omp critical(pipeline_layer_preview_error)
#endif
            {
                if (!has_layer_preview_error.load(std::memory_order_relaxed)) {
                    layer_preview_error_message = e.what();
                    has_layer_preview_error.store(true, std::memory_order_relaxed);
                }
            }
        } catch (...) {
#ifdef _OPENMP
#    pragma omp critical(pipeline_layer_preview_error)
#endif
            {
                if (!has_layer_preview_error.load(std::memory_order_relaxed)) {
                    layer_preview_error_message =
                        "Unknown error while rendering layer preview images";
                    has_layer_preview_error.store(true, std::memory_order_relaxed);
                }
            }
        }
    }
    if (has_layer_preview_error.load(std::memory_order_relaxed)) {
        throw InputError(layer_preview_error_message.empty()
                             ? "Failed to render raster layer preview image"
                             : layer_preview_error_message);
    }

    // === 5. Build 3D model and export ===
    NotifyProgress(progress, ConvertStage::BuildingModel, 0.0f);

    BuildModelIRConfig build_cfg;
    build_cfg.flip_y = request.flip_y;

    ColorDB profile_db = profile.ToColorDB("MergedPrintProfile");
    ModelIR model      = ModelIR::Build(recipe_map, profile_db, build_cfg);

    NotifyProgress(progress, ConvertStage::BuildingModel, 1.0f);
    spdlog::info("Stage: building_model completed, grids={}, layers={}", model.voxel_grids.size(),
                 model.base_layers + model.color_layers);
    NotifyProgress(progress, ConvertStage::Exporting, 0.0f);

    BuildMeshConfig mesh_cfg;
    mesh_cfg.pixel_mm = resolved_pixel_mm;
    mesh_cfg.layer_height_mm =
        (request.layer_height_mm > 0.0f)
            ? request.layer_height_mm
            : (profile.layer_height_mm > 0.0f ? profile.layer_height_mm : 0.08f);

    if (!request.preset_dir.empty()) {
        auto preset = SlicerPreset::FromProfile(request.preset_dir, profile,
                                                fil_config ? &*fil_config : nullptr);
        if (!preset.preset_json_path.empty()) {
            result.model_3mf = Export3mfToBuffer(model, mesh_cfg, preset);
            spdlog::info("Raster pipeline: injected slicer preset from {}",
                         preset.preset_json_path);
        } else {
            result.model_3mf = Export3mfToBuffer(model, mesh_cfg);
        }
    } else {
        result.model_3mf = Export3mfToBuffer(model, mesh_cfg);
    }

    if (!request.output_3mf_path.empty()) {
        auto dir = std::filesystem::path(request.output_3mf_path).parent_path();
        if (!dir.empty()) { std::filesystem::create_directories(dir); }
        std::ofstream ofs(request.output_3mf_path, std::ios::binary);
        if (ofs) {
            ofs.write(reinterpret_cast<const char*>(result.model_3mf.data()),
                      static_cast<std::streamsize>(result.model_3mf.size()));
        }
    }

    result.resolved_pixel_mm  = resolved_pixel_mm;
    result.physical_width_mm  = static_cast<float>(img.width) * resolved_pixel_mm;
    result.physical_height_mm = static_cast<float>(img.height) * resolved_pixel_mm;

    NotifyProgress(progress, ConvertStage::Exporting, 1.0f);
    spdlog::info("ConvertRaster completed: 3mf={} bytes, preview={} bytes, source_mask={} bytes, "
                 "physical={:.1f}x{:.1f} mm",
                 result.model_3mf.size(), result.preview_png.size(), result.source_mask_png.size(),
                 result.physical_width_mm, result.physical_height_mm);

    return result;
}

} // namespace ChromaPrint3D
