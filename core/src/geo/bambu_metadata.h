#pragma once

/// \file bambu_metadata.h
/// \brief Internal: generate Bambu Studio metadata files for 3MF embedding.

#include "chromaprint3d/slicer_preset.h"

#include <string>
#include <vector>

namespace ChromaPrint3D::detail {

/// Describes a single exported mesh object and its filament slot assignment.
struct ExportedObject {
    std::string name;
    int filament_slot; ///< 1-based filament slot index for Bambu Studio extruder assignment.
    int resource_id;   ///< 3MF resource ID assigned by exporter.
};

/// Describes all exported mesh objects (flat list, each is an independent build item).
struct ExportedGroup {
    std::vector<ExportedObject> objects;
};

/// Load a preset JSON file and patch filament-related fields from the SlicerPreset.
std::string BuildProjectSettings(const SlicerPreset& preset);

/// Generate model_settings.config XML for independent mesh objects.
std::string BuildModelSettings(const ExportedGroup& group);

/// Generate slice_info.config XML.
std::string BuildSliceInfo();

/// Generate cut_information.xml (minimal stub).
std::string BuildCutInformation(const ExportedGroup& group);

/// Generate filament_sequence.json.
std::string BuildFilamentSequence();

} // namespace ChromaPrint3D::detail
