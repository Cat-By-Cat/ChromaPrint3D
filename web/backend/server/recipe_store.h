#pragma once

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

struct BoardRecipeSet {
    int board_index = 0;
    int grid_rows   = 0;
    int grid_cols   = 0;
    std::vector<std::vector<uint8_t>> recipes;
};

struct EightColorBoardLayout {
    float line_width_mm  = 0.0f;
    int resolution_scale = 0;
    int tile_factor      = 0;
    int gap_factor       = 0;
    int margin_factor    = 0;
};

struct EightColorRecipeStore {
    bool loaded           = false;
    int base_channel_idx  = 0;
    int base_layers       = 0;
    float layer_height_mm = 0.0f;
    int num_channels      = 0;
    int color_layers      = 0;
    std::string layer_order;
    EightColorBoardLayout layout;
    std::vector<BoardRecipeSet> boards; // indexed 0 = board 1, 1 = board 2

    const BoardRecipeSet* FindBoard(int board_index) const {
        for (const auto& b : boards) {
            if (b.board_index == board_index) { return &b; }
        }
        return nullptr;
    }

    static EightColorRecipeStore LoadFromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in.is_open()) { throw std::runtime_error("Cannot open recipe file: " + path); }
        nlohmann::json j;
        in >> j;
        return FromJson(j);
    }

    static EightColorRecipeStore FromJson(const nlohmann::json& j) {
        if (!j.is_object()) { throw std::runtime_error("Recipe JSON root must be an object"); }

        const auto require_int = [](const nlohmann::json& obj, const char* key) -> int {
            if (!obj.contains(key) || !obj.at(key).is_number_integer()) {
                throw std::runtime_error(std::string("Missing or invalid integer field: ") + key);
            }
            return obj.at(key).get<int>();
        };
        const auto require_float = [](const nlohmann::json& obj, const char* key) -> float {
            if (!obj.contains(key) || !obj.at(key).is_number()) {
                throw std::runtime_error(std::string("Missing or invalid number field: ") + key);
            }
            return obj.at(key).get<float>();
        };
        const auto require_string = [](const nlohmann::json& obj, const char* key) -> std::string {
            if (!obj.contains(key) || !obj.at(key).is_string()) {
                throw std::runtime_error(std::string("Missing or invalid string field: ") + key);
            }
            return obj.at(key).get<std::string>();
        };

        if (!j.contains("meta") || !j.at("meta").is_object()) {
            throw std::runtime_error("Recipe JSON missing required object: meta");
        }
        if (!j.contains("layout") || !j.at("layout").is_object()) {
            throw std::runtime_error("Recipe JSON missing required object: layout");
        }
        if (!j.contains("palette") || !j.at("palette").is_array()) {
            throw std::runtime_error("Recipe JSON missing required array: palette");
        }
        if (!j.contains("boards") || !j.at("boards").is_array()) {
            throw std::runtime_error("Recipe JSON missing required array: boards");
        }

        EightColorRecipeStore store;
        const auto& meta   = j.at("meta");
        store.num_channels = require_int(meta, "num_channels");
        store.color_layers = require_int(meta, "color_layers");

        store.base_channel_idx = require_int(j, "base_channel_idx");
        store.base_layers      = require_int(j, "base_layers");
        store.layer_height_mm  = require_float(j, "layer_height_mm");
        store.layer_order      = require_string(j, "layer_order");

        const auto& layout            = j.at("layout");
        store.layout.line_width_mm    = require_float(layout, "line_width_mm");
        store.layout.resolution_scale = require_int(layout, "resolution_scale");
        store.layout.tile_factor      = require_int(layout, "tile_factor");
        store.layout.gap_factor       = require_int(layout, "gap_factor");
        store.layout.margin_factor    = require_int(layout, "margin_factor");

        if (store.num_channels <= 0) { throw std::runtime_error("num_channels must be positive"); }
        if (store.color_layers <= 0) { throw std::runtime_error("color_layers must be positive"); }
        if (store.base_channel_idx < 0 || store.base_channel_idx >= store.num_channels) {
            throw std::runtime_error("base_channel_idx is out of range");
        }
        if (store.base_layers < 0) { throw std::runtime_error("base_layers must be >= 0"); }
        if (store.layer_height_mm <= 0.0f) {
            throw std::runtime_error("layer_height_mm must be positive");
        }
        if (store.layer_order != "Top2Bottom" && store.layer_order != "Bottom2Top") {
            throw std::runtime_error("layer_order must be Top2Bottom or Bottom2Top");
        }
        if (store.layout.line_width_mm <= 0.0f) {
            throw std::runtime_error("layout.line_width_mm must be positive");
        }
        if (store.layout.resolution_scale <= 0) {
            throw std::runtime_error("layout.resolution_scale must be positive");
        }
        if (store.layout.tile_factor <= 0) {
            throw std::runtime_error("layout.tile_factor must be positive");
        }
        if (store.layout.gap_factor < 0) {
            throw std::runtime_error("layout.gap_factor must be >= 0");
        }
        if (store.layout.margin_factor < 0) {
            throw std::runtime_error("layout.margin_factor must be >= 0");
        }

        if (static_cast<int>(j.at("palette").size()) != store.num_channels) {
            throw std::runtime_error("palette size must match meta.num_channels");
        }
        for (const auto& bj : j.at("boards")) {
            if (!bj.is_object()) { throw std::runtime_error("board item must be object"); }
            BoardRecipeSet brs;
            brs.board_index = require_int(bj, "board_index");
            brs.grid_rows   = require_int(bj, "grid_rows");
            brs.grid_cols   = require_int(bj, "grid_cols");
            if (brs.board_index <= 0) { throw std::runtime_error("board_index must be positive"); }
            if (brs.grid_rows <= 0 || brs.grid_cols <= 0) {
                throw std::runtime_error("grid_rows/grid_cols must be positive");
            }
            if (!bj.contains("recipes") || !bj.at("recipes").is_array()) {
                throw std::runtime_error("Board entry missing 'recipes' array");
            }
            const auto expected_max =
                static_cast<size_t>(brs.grid_rows) * static_cast<size_t>(brs.grid_cols);
            if (bj.at("recipes").size() > expected_max) {
                throw std::runtime_error("recipes count exceeds grid capacity");
            }

            for (const auto& rj : bj.at("recipes")) {
                if (!rj.is_array()) { throw std::runtime_error("recipe entry must be an array"); }
                if (static_cast<int>(rj.size()) != store.color_layers) {
                    throw std::runtime_error("recipe layer count must match meta.color_layers");
                }
                std::vector<uint8_t> recipe;
                recipe.reserve(static_cast<size_t>(store.color_layers));
                for (const auto& v : rj) {
                    if (!v.is_number_integer()) {
                        throw std::runtime_error("recipe channel index must be integer");
                    }
                    const int channel_idx = v.get<int>();
                    if (channel_idx < 0 || channel_idx >= store.num_channels) {
                        throw std::runtime_error("recipe channel index out of range");
                    }
                    recipe.push_back(static_cast<uint8_t>(channel_idx));
                }
                brs.recipes.push_back(std::move(recipe));
            }
            store.boards.push_back(std::move(brs));
        }
        if (store.boards.empty()) { throw std::runtime_error("boards array must not be empty"); }
        store.loaded = true;

        spdlog::info("Loaded 8-color recipe store: {} boards", store.boards.size());
        for (const auto& b : store.boards) {
            spdlog::info("  Board {}: {}x{}, {} recipes", b.board_index, b.grid_rows, b.grid_cols,
                         b.recipes.size());
        }
        return store;
    }
};
