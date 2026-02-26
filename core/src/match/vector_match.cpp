#include "chromaprint3d/vector_recipe_map.h"
#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/color_db.h"
#include "chromaprint3d/print_profile.h"
#include "chromaprint3d/recipe_map.h"
#include "chromaprint3d/error.h"

#include <spdlog/spdlog.h>

#include <unordered_map>
#include <cstddef>
#include <cmath>

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
    float de = 0.0f;
};

} // namespace

VectorRecipeMap VectorRecipeMap::Match(const VectorProcResult& result, std::span<const ColorDB> dbs,
                                       const PrintProfile& profile, const MatchConfig& cfg,
                                       const ModelPackage* /*model_package*/,
                                       const ModelGateConfig& /*model_gate*/,
                                       MatchStats* out_stats) {
    if (dbs.empty()) { throw InputError("VectorRecipeMap::Match: no ColorDBs provided"); }

    VectorRecipeMap map;
    map.color_layers = profile.color_layers;
    map.num_channels = profile.NumChannels();
    map.layer_order  = profile.layer_order;

    if (result.shapes.empty()) { return map; }

    const bool use_lab = (cfg.color_space == ColorSpace::Lab);

    std::unordered_map<Rgb, CachedMatch, RgbHash, RgbEqual> color_cache;

    int matched_count = 0;
    double total_de   = 0.0;

    for (size_t i = 0; i < result.shapes.size(); ++i) {
        const VectorShape& shape = result.shapes[i];
        if (shape.fill_type != FillType::Solid) { continue; }

        auto it = color_cache.find(shape.fill_color);
        if (it != color_cache.end()) {
            ShapeEntry entry;
            entry.shape_idx     = static_cast<int>(i);
            entry.recipe        = it->second.recipe;
            entry.matched_color = it->second.matched_lab;
            map.entries.push_back(std::move(entry));
            continue;
        }

        Lab target_lab = Lab::FromRgb(shape.fill_color);

        const Entry* best_entry = nullptr;
        float best_de           = 1e30f;

        for (const ColorDB& db : dbs) {
            const Entry& nearest =
                use_lab ? db.NearestEntry(target_lab) : db.NearestEntry(shape.fill_color);
            float de = Lab::DeltaE76(target_lab, nearest.lab);
            if (de < best_de) {
                best_de    = de;
                best_entry = &nearest;
            }
        }

        std::vector<uint8_t> recipe(static_cast<size_t>(profile.color_layers), 0);
        Lab matched_lab = target_lab;

        if (best_entry) {
            matched_lab = best_entry->lab;
            for (int layer = 0; layer < profile.color_layers; ++layer) {
                if (layer < static_cast<int>(best_entry->recipe.size())) {
                    recipe[static_cast<size_t>(layer)] =
                        best_entry->recipe[static_cast<size_t>(layer)];
                }
            }
            total_de += best_de;
            ++matched_count;
        }

        color_cache[shape.fill_color] = CachedMatch{recipe, matched_lab, best_de};

        ShapeEntry entry;
        entry.shape_idx     = static_cast<int>(i);
        entry.recipe        = recipe;
        entry.matched_color = matched_lab;
        map.entries.push_back(std::move(entry));
    }

    if (out_stats) {
        out_stats->clusters_total = static_cast<int>(color_cache.size());
        out_stats->db_only        = matched_count;
        out_stats->avg_db_de =
            matched_count > 0 ? static_cast<float>(total_de / matched_count) : 0.0f;
    }

    spdlog::info("VectorRecipeMap::Match: {} shapes, {} unique colors, avg_de={:.2f}",
                 map.entries.size(), color_cache.size(),
                 matched_count > 0 ? total_de / matched_count : 0.0);

    return map;
}

} // namespace ChromaPrint3D
