#pragma once

/// \file export_3mf.h
/// \brief 3MF file export from ModelIR or pre-built meshes.

#include "mesh.h"
#include "color_db.h"
#include "slicer_preset.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ChromaPrint3D {

struct ModelIR;

/// Export a ModelIR to a 3MF file using default mesh config.
void Export3mf(const std::string& path, const ModelIR& model_ir);

/// Export a ModelIR to a 3MF file with custom mesh config.
void Export3mf(const std::string& path, const ModelIR& model_ir, const BuildMeshConfig& cfg);

/// Export a ModelIR to an in-memory 3MF buffer.
std::vector<uint8_t> Export3mfToBuffer(const ModelIR& model_ir, const BuildMeshConfig& cfg = {});

/// Export a ModelIR to an in-memory 3MF buffer with preset-compatible signature.
/// When preset_json_path is available, private slicer metadata attachments are injected;
/// otherwise the export safely falls back to standard 3MF.
std::vector<uint8_t> Export3mfToBuffer(const ModelIR& model_ir, const BuildMeshConfig& cfg,
                                       const SlicerPreset& preset);

/// Export pre-built meshes to an in-memory 3MF buffer with given palette for object naming.
/// Mesh order must match: channel 0, channel 1, ..., channel N-1, [base].
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette,
                                         int base_channel_idx = -1, int base_layers = 0);

/// Export pre-built meshes with preset-compatible signature.
/// When preset_json_path is available, private slicer metadata attachments are injected;
/// otherwise the export safely falls back to standard 3MF.
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette, int base_channel_idx,
                                         int base_layers, const SlicerPreset& preset);

/// Export pre-built meshes with explicit per-mesh name and slot assignment.
/// \param palette Slot-ordered palette (slot 1..N) used to map display colors.
/// \param names Per-mesh display name.
/// \param slots Per-mesh 1-based slot assignment.
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette,
                                         const std::vector<std::string>& names,
                                         const std::vector<int>& slots, const SlicerPreset& preset);

} // namespace ChromaPrint3D
