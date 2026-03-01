#include "chromaprint3d/vector_recipe_map.h"
#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/color_db.h"
#include "chromaprint3d/print_profile.h"
#include "chromaprint3d/recipe_map.h"
#include "chromaprint3d/error.h"
#include "detail/candidate_select.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <unordered_map>

namespace ChromaPrint3D {

namespace {

struct RgbHash {
    size_t operator()(const Rgb& c) const {
        auto h = std::hash<float>{}(c.x);
        h ^= std::hash<float>{}(c.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(c.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

struct RgbEqual {
    bool operator()(const Rgb& a, const Rgb& b) const {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
};

struct CachedMatch {
    std::vector<uint8_t> recipe;
    Lab matched_lab;
    float de        = 0.0f;
    bool from_model = false;
};

} // namespace

VectorRecipeMap VectorRecipeMap::Match(const VectorProcResult& result, std::span<const ColorDB> dbs,
                                       const PrintProfile& profile, const MatchConfig& cfg,
                                       const ModelPackage* model_package,
                                       const ModelGateConfig& model_gate, MatchStats* out_stats) {
    if (dbs.empty() && !model_gate.model_only) {
        throw InputError("VectorRecipeMap::Match: no ColorDBs provided");
    }
    profile.Validate();

    VectorRecipeMap map;
    map.color_layers = profile.color_layers;
    map.num_channels = profile.NumChannels();
    map.layer_order  = profile.layer_order;

    if (result.shapes.empty()) { return map; }

    const bool use_lab    = (cfg.color_space == ColorSpace::Lab);
    const bool model_only = model_gate.model_only;
    std::vector<detail::PreparedDB> prepared_dbs;
    if (!model_only) { prepared_dbs = detail::PrepareDBs(dbs, profile); }

    const std::optional<detail::PreparedModel> prepared_model =
        detail::PrepareModel(model_package, model_gate, profile);
    if (model_only && !prepared_model.has_value()) {
        throw ConfigError("Model-only matching requires a compatible model package");
    }

    std::unordered_map<Rgb, CachedMatch, RgbHash, RgbEqual> color_cache;

    int stat_total_queries   = 0;
    int stat_db_only         = 0;
    int stat_model_used      = 0;
    int stat_model_queries   = 0;
    double stat_sum_db_de    = 0.0;
    double stat_sum_model_de = 0.0;

    for (size_t i = 0; i < result.shapes.size(); ++i) {
        const VectorShape& shape = result.shapes[i];
        if (shape.fill_type != FillType::Solid) { continue; }

        auto it = color_cache.find(shape.fill_color);
        if (it != color_cache.end()) {
            ShapeEntry entry;
            entry.shape_idx     = static_cast<int>(i);
            entry.recipe        = it->second.recipe;
            entry.matched_color = it->second.matched_lab;
            entry.from_model    = it->second.from_model;
            map.entries.push_back(std::move(entry));
            continue;
        }

        Lab target_lab = Lab::FromRgb(shape.fill_color);
        const cv::Vec3f target_color =
            use_lab ? cv::Vec3f(target_lab.l(), target_lab.a(), target_lab.b())
                    : cv::Vec3f(shape.fill_color.r(), shape.fill_color.g(), shape.fill_color.b());
        const detail::CandidateDecision decision =
            detail::SelectCandidate(target_color, use_lab, prepared_dbs, profile, cfg,
                                    prepared_model ? &prepared_model.value() : nullptr, model_only);
        if (!decision.selected.valid) {
            throw MatchError("No valid match candidate after DB/model selection");
        }

        std::vector<uint8_t> recipe(static_cast<size_t>(profile.color_layers), 0);
        Lab matched_lab = target_lab;
        float best_de   = 0.0f;
        matched_lab     = decision.selected.mapped_lab;
        recipe          = decision.selected.recipe;
        best_de         = decision.selected.from_model ? decision.model_de : decision.db_de;
        ++stat_total_queries;
        stat_sum_db_de += static_cast<double>(decision.db_de);
        if (decision.model_queried) {
            ++stat_model_queries;
            stat_sum_model_de += static_cast<double>(decision.model_de);
        }
        if (decision.selected.from_model) {
            ++stat_model_used;
        } else {
            ++stat_db_only;
        }

        color_cache[shape.fill_color] =
            CachedMatch{recipe, matched_lab, best_de, decision.selected.from_model};

        ShapeEntry entry;
        entry.shape_idx     = static_cast<int>(i);
        entry.recipe        = recipe;
        entry.matched_color = matched_lab;
        entry.from_model    = decision.selected.from_model;
        map.entries.push_back(std::move(entry));
    }

    if (out_stats) {
        out_stats->clusters_total = stat_total_queries;
        out_stats->db_only        = stat_db_only;
        out_stats->model_fallback = stat_model_used;
        out_stats->model_queries  = stat_model_queries;
        out_stats->avg_db_de =
            (stat_total_queries > 0)
                ? static_cast<float>(stat_sum_db_de / static_cast<double>(stat_total_queries))
                : 0.0f;
        out_stats->avg_model_de =
            (stat_model_queries > 0)
                ? static_cast<float>(stat_sum_model_de / static_cast<double>(stat_model_queries))
                : 0.0f;
    }

    spdlog::info(
        "VectorRecipeMap::Match: {} shapes, {} unique colors, db_only={}, model_fallback={}, "
        "avg_db_de={:.2f}, avg_model_de={:.2f}",
        map.entries.size(), color_cache.size(), stat_db_only, stat_model_used,
        stat_total_queries > 0 ? stat_sum_db_de / static_cast<double>(stat_total_queries) : 0.0,
        stat_model_queries > 0 ? stat_sum_model_de / static_cast<double>(stat_model_queries) : 0.0);

    return map;
}

} // namespace ChromaPrint3D
