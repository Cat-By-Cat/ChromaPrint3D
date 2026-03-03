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

/// Export a ModelIR to an in-memory 3MF buffer with embedded Bambu Studio preset metadata.
std::vector<uint8_t> Export3mfToBuffer(const ModelIR& model_ir, const BuildMeshConfig& cfg,
                                       const SlicerPreset& preset);

/// Export pre-built meshes to an in-memory 3MF buffer with given palette for object naming.
/// Mesh order must match: channel 0, channel 1, ..., channel N-1, [base].
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette,
                                         int base_channel_idx = -1, int base_layers = 0);

/// Export pre-built meshes with embedded Bambu Studio preset metadata.
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette, int base_channel_idx,
                                         int base_layers, const SlicerPreset& preset);

/// Export pre-built meshes with explicit per-mesh name and AMS slot assignment.
/// \param palette AMS-ordered palette (slot 1..N); used for filament slot definitions.
/// \param names Per-mesh display name.
/// \param slots Per-mesh 1-based AMS filament slot assignment.
std::vector<uint8_t> Export3mfFromMeshes(const std::vector<Mesh>& meshes,
                                         const std::vector<Channel>& palette,
                                         const std::vector<std::string>& names,
                                         const std::vector<int>& slots, const SlicerPreset& preset);

} // namespace ChromaPrint3D
