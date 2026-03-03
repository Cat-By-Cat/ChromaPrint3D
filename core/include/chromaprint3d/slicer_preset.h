#pragma once

/// \file slicer_preset.h
/// \brief Slicer preset configuration for embedding print parameters in 3MF exports.

#include "print_profile.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ChromaPrint3D {

/// Per-slot filament description used when patching slicer presets.
struct FilamentSlot {
    std::string type        = "PLA";
    std::string colour      = "#FFFFFF";
    std::string settings_id = "Bambu PLA Basic @BBL P2S";
    std::string vendor      = "Bambu Lab";
    std::string filament_id = "GFA00";
    int nozzle_temp         = 220;
    int nozzle_temp_initial = 220;
};

/// External filament configuration loaded from JSON.
/// Replaces hardcoded color maps, material tables, and filament defaults.
struct FilamentConfig {
    /// Color name (lowercase) -> hex color string (e.g. "#00AE42").
    std::unordered_map<std::string, std::string> colors;

    /// Fallback hex colors for unknown color names (cycled by index).
    std::vector<std::string> fallback_palette;

    /// Material type key (e.g. "PLA") -> default FilamentSlot template.
    std::unordered_map<std::string, FilamentSlot> materials;

    /// Lowercase material alias -> canonical material type key.
    std::unordered_map<std::string, std::string> material_aliases;

    /// Load from a specific JSON file path.
    static FilamentConfig LoadFromJson(const std::string& path);

    /// Load from preset directory (looks for filaments.json); returns built-in defaults if missing.
    static FilamentConfig LoadFromDir(const std::string& preset_dir);

    /// Returns a FilamentConfig populated with the built-in hardcoded defaults.
    static FilamentConfig BuiltinDefaults();
};

/// Slicer preset loaded from an external JSON template, with runtime filament overrides.
struct SlicerPreset {
    std::string preset_json_path;
    std::vector<FilamentSlot> filaments;
    std::vector<int> flush_volumes_matrix;

    /// Build a SlicerPreset by selecting the best preset file from \p preset_dir
    /// and populating filament slots from the given \p profile.
    /// If \p config is non-null, material lookup uses its tables instead of hardcoded logic.
    static SlicerPreset FromProfile(const std::string& preset_dir, const PrintProfile& profile,
                                    const FilamentConfig* config = nullptr);
};

/// Select the best-matching preset JSON filename within \p preset_dir
/// for the given printer model and layer height.
/// Returns the full path to the preset file, or empty string if none found.
std::string FindPresetFile(const std::string& preset_dir, const std::string& printer_model,
                           float layer_height);

} // namespace ChromaPrint3D
