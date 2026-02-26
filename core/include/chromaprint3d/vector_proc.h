#pragma once

/// \file vector_proc.h
/// \brief Vector image (SVG) preprocessing: parsing, tessellation, occlusion clipping.

#include "vector_image.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ChromaPrint3D {

/// Result of vector image preprocessing.
struct VectorProcResult {
    std::string name;

    float width_mm  = 0.0f;
    float height_mm = 0.0f;

    /// Shapes after bezier flattening, scaling, and occlusion clipping.
    /// No two shapes overlap in XY; draw-order priority is preserved.
    std::vector<VectorShape> shapes;
};

/// Configuration for vector image preprocessing.
struct VectorProcConfig {
    float target_width_mm  = 0.0f; ///< Target physical width (0 = use SVG original).
    float target_height_mm = 0.0f; ///< Target physical height (0 = use SVG original).

    float tessellation_tolerance_mm = 0.02f; ///< Bezier flattening tolerance.
    bool flip_y                     = true;
};

/// Vector image preprocessor: parses SVG, flattens beziers, scales, clips occlusion.
class VectorProc {
public:
    explicit VectorProc(const VectorProcConfig& config = {});

    /// Process an SVG from a file path.
    VectorProcResult Run(const std::string& svg_path) const;

    /// Process an SVG from an in-memory buffer.
    VectorProcResult RunFromBuffer(const std::vector<uint8_t>& buffer,
                                   const std::string& name = "") const;

    const VectorProcConfig& config() const { return config_; }

private:
    VectorProcConfig config_;
};

} // namespace ChromaPrint3D
