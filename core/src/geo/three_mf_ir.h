#pragma once

#include "chromaprint3d/mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ChromaPrint3D::detail {

enum class ThreeMfUnit {
    Micron,
    Millimeter,
    Centimeter,
    Inch,
    Foot,
    Meter,
};

struct ThreeMfTransform {
    // 3MF transform attribute stores a 3x4 matrix in row-major order:
    // m00 m01 m02 m03 m10 m11 m12 m13 m20 m21 m22 m23
    std::array<float, 12> values = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                    0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};

    static ThreeMfTransform Identity() { return {}; }

    bool IsIdentity(float eps = 1e-6f) const;
};

struct ThreeMfMetadataEntry {
    std::string name;
    std::string value;
};

struct ThreeMfInputObject {
    std::string name;
    std::string display_color_hex;
    Mesh mesh;
    ThreeMfTransform transform = ThreeMfTransform::Identity();
};

enum class ThreeMfCompressionMode {
    StoreOnly,
    Deflate,
    AutoThreshold,
};

struct ThreeMfExportOptions {
    ThreeMfUnit unit = ThreeMfUnit::Millimeter;
    std::vector<ThreeMfMetadataEntry> metadata;
    ThreeMfCompressionMode compression_mode = ThreeMfCompressionMode::AutoThreshold;
    std::size_t compression_threshold_bytes = 16 * 1024;
    int deflate_level                       = 1;
    bool deterministic                      = true;
};

struct ThreeMfBaseMaterial {
    std::string name;
    std::string display_color_hex;
};

struct ThreeMfObjectProperty {
    uint32_t pid    = 0;
    uint32_t pindex = 0;
};

struct ThreeMfMeshResource {
    uint32_t object_id             = 0;
    std::size_t source_input_index = 0;
    std::string name;
    std::vector<float> vertices_xyz; // packed xyzxyz...
    std::vector<uint32_t> triangles; // packed i0i1i2...
    std::optional<ThreeMfObjectProperty> object_property;
};

struct ThreeMfBuildItem {
    uint32_t object_id = 0;
    ThreeMfTransform transform;
};

struct OpcPart {
    std::string path_in_zip;  // e.g. "3D/3dmodel.model"
    std::string content_type; // MIME content type for [Content_Types].xml
    std::string data;
};

struct OpcRelationship {
    std::string id;
    std::string type;
    std::string target;
};

struct OpcRelationshipSet {
    // Empty means package root and maps to "_rels/.rels".
    std::string source_part_path;
    std::vector<OpcRelationship> relationships;
};

struct OpcContentTypeDefault {
    // Extension without leading dot, e.g. "rels", "config".
    std::string extension;
    std::string content_type;
};

struct OpcContentTypeOverride {
    // Absolute part name with leading slash, e.g. "/Metadata/x.config".
    std::string part_name;
    std::string content_type;
};

struct ThreeMfDocument {
    ThreeMfUnit unit = ThreeMfUnit::Millimeter;
    std::vector<ThreeMfMetadataEntry> metadata;

    std::optional<uint32_t> base_material_group_id;
    std::vector<ThreeMfBaseMaterial> base_materials;

    std::vector<ThreeMfMeshResource> mesh_resources;
    std::vector<ThreeMfBuildItem> build_items;

    std::vector<OpcPart> extra_parts;
    std::vector<OpcRelationshipSet> relationship_sets;
    std::vector<OpcContentTypeDefault> content_type_defaults;
    std::vector<OpcContentTypeOverride> content_type_overrides;
};

} // namespace ChromaPrint3D::detail
