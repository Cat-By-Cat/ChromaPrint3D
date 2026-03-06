#include "three_mf_writer.h"
#include "bambu_metadata.h"

#include "chromaprint3d/error.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <spdlog/spdlog.h>

namespace ChromaPrint3D::detail {

namespace {

bool IsDegenerateTriangleByArea(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    Vec3f ab = b - a;
    Vec3f ac = c - a;
    float cx = ab.y * ac.z - ab.z * ac.y;
    float cy = ab.z * ac.x - ab.x * ac.z;
    float cz = ab.x * ac.y - ab.y * ac.x;
    return (cx * cx + cy * cy + cz * cz) <= 1e-12f;
}

uint32_t ToIndex(int idx, std::size_t vertex_count) {
    if (idx < 0) { throw InputError("Mesh index is negative"); }
    std::size_t uidx = static_cast<std::size_t>(idx);
    if (uidx >= vertex_count) { throw InputError("Mesh index out of range"); }
    if (uidx > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
        throw InputError("Mesh index exceeds 3MF uint32 limit");
    }
    return static_cast<uint32_t>(uidx);
}

std::string NormalizeHexColor(const std::string& hex) {
    auto is_hex = [](unsigned char c) { return std::isxdigit(c) != 0; };
    if ((hex.size() == 7 || hex.size() == 9) && hex[0] == '#') {
        for (std::size_t i = 1; i < hex.size(); ++i) {
            if (!is_hex(static_cast<unsigned char>(hex[i]))) { return "#FFFFFF"; }
        }
        std::string out = hex;
        std::transform(out.begin(), out.end(), out.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return out;
    }
    return "#FFFFFF";
}

bool IsFiniteTransform(const ThreeMfTransform& transform) {
    for (float v : transform.values) {
        if (!std::isfinite(v)) { return false; }
    }
    return true;
}

bool HasMetadataKey(const std::vector<ThreeMfMetadataEntry>& metadata, const std::string& key) {
    return std::any_of(metadata.begin(), metadata.end(),
                       [&](const ThreeMfMetadataEntry& entry) { return entry.name == key; });
}

int ResolveFilamentSlot(const std::vector<int>& slots, std::size_t source_input_index) {
    if (slots.empty()) { return static_cast<int>(source_input_index) + 1; }
    if (source_input_index >= slots.size()) {
        throw InputError("Bambu metadata slot mapping index out of range");
    }
    if (slots[source_input_index] <= 0) {
        throw InputError("Bambu metadata filament slot must be > 0");
    }
    return slots[source_input_index];
}

void AddExtraMetadataPart(ThreeMfDocument& document, std::string path_in_zip,
                          std::string content_type, std::string payload) {
    document.extra_parts.push_back(OpcPart{
        .path_in_zip  = std::move(path_in_zip),
        .content_type = std::move(content_type),
        .data         = std::move(payload),
    });
}

class UnitAndMetadataExtension final : public IThreeMfExtension {
public:
    int Priority() const override { return 10; }

    void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& /*objects*/,
               const ThreeMfExportOptions& options) const override {
        document.unit     = options.unit;
        document.metadata = options.metadata;
    }
};

class BaseMaterialExtension final : public IThreeMfExtension {
public:
    int Priority() const override { return 20; }

    void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& objects,
               const ThreeMfExportOptions& /*options*/) const override {
        if (objects.empty()) { return; }

        document.base_material_group_id = 1;
        document.base_materials.clear();
        document.base_materials.reserve(objects.size());
        for (const auto& object : objects) {
            ThreeMfBaseMaterial material;
            material.name              = object.name;
            material.display_color_hex = NormalizeHexColor(object.display_color_hex);
            document.base_materials.push_back(std::move(material));
        }
    }
};

class CoreModelExtension final : public IThreeMfExtension {
public:
    int Priority() const override { return 30; }

    void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& objects,
               const ThreeMfExportOptions& /*options*/) const override {
        uint32_t next_object_id = 1;
        if (document.base_material_group_id.has_value()) {
            next_object_id =
                std::max<uint32_t>(next_object_id, document.base_material_group_id.value() + 1);
        }

        document.mesh_resources.clear();
        document.build_items.clear();

        std::size_t dropped_degenerate = 0;
        for (std::size_t i = 0; i < objects.size(); ++i) {
            const auto& in                 = objects[i];
            const std::size_t vertex_count = in.mesh.vertices.size();
            if (vertex_count == 0 || in.mesh.indices.empty()) { continue; }
            if (vertex_count > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())) {
                throw InputError("Mesh vertex count exceeds 3MF uint32 limit");
            }

            ThreeMfMeshResource mesh_resource;
            mesh_resource.object_id          = next_object_id++;
            mesh_resource.source_input_index = i;
            mesh_resource.name               = in.name;
            mesh_resource.vertices_xyz.reserve(vertex_count * 3);
            for (const Vec3f& vertex : in.mesh.vertices) {
                if (!vertex.IsFinite()) { throw InputError("Mesh vertex is not finite"); }
                mesh_resource.vertices_xyz.push_back(vertex.x);
                mesh_resource.vertices_xyz.push_back(vertex.y);
                mesh_resource.vertices_xyz.push_back(vertex.z);
            }

            mesh_resource.triangles.reserve(in.mesh.indices.size() * 3);
            for (const Vec3i& tri : in.mesh.indices) {
                if (tri.x == tri.y || tri.y == tri.z || tri.x == tri.z) {
                    ++dropped_degenerate;
                    continue;
                }
                uint32_t i0     = ToIndex(tri.x, vertex_count);
                uint32_t i1     = ToIndex(tri.y, vertex_count);
                uint32_t i2     = ToIndex(tri.z, vertex_count);
                const Vec3f& v0 = in.mesh.vertices[static_cast<std::size_t>(i0)];
                const Vec3f& v1 = in.mesh.vertices[static_cast<std::size_t>(i1)];
                const Vec3f& v2 = in.mesh.vertices[static_cast<std::size_t>(i2)];
                if (IsDegenerateTriangleByArea(v0, v1, v2)) {
                    ++dropped_degenerate;
                    continue;
                }
                mesh_resource.triangles.push_back(i0);
                mesh_resource.triangles.push_back(i1);
                mesh_resource.triangles.push_back(i2);
            }

            if (mesh_resource.triangles.empty()) { continue; }

            if (document.base_material_group_id.has_value() && i < document.base_materials.size()) {
                mesh_resource.object_property = ThreeMfObjectProperty{
                    .pid    = document.base_material_group_id.value(),
                    .pindex = static_cast<uint32_t>(i),
                };
            }
            document.mesh_resources.push_back(std::move(mesh_resource));
            document.build_items.push_back(ThreeMfBuildItem{
                .object_id = document.mesh_resources.back().object_id,
                .transform = in.transform,
            });
        }

        if (dropped_degenerate > 0) {
            spdlog::warn("3MF export dropped {} degenerate triangle(s)", dropped_degenerate);
        }
    }
};

class BuildTransformExtension final : public IThreeMfExtension {
public:
    int Priority() const override { return 40; }

    void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& /*objects*/,
               const ThreeMfExportOptions& /*options*/) const override {
        for (const ThreeMfBuildItem& item : document.build_items) {
            if (!IsFiniteTransform(item.transform)) {
                throw InputError("Build item transform contains non-finite value");
            }
        }
    }
};

class BambuMetadataExtension final : public IThreeMfExtension {
public:
    BambuMetadataExtension(SlicerPreset preset, std::vector<int> input_slots)
        : preset_(std::move(preset)), input_slots_(std::move(input_slots)) {}

    int Priority() const override { return 50; }

    void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& objects,
               const ThreeMfExportOptions& /*options*/) const override {
        if (preset_.preset_json_path.empty()) { return; }
        if (document.mesh_resources.empty()) { return; }
        if (!input_slots_.empty() && input_slots_.size() != objects.size()) {
            throw InputError("Bambu metadata slot mapping size must match writer input objects");
        }

        constexpr const char* kExternalObjectPath    = "/3D/Objects/object_1.model";
        constexpr const char* kExternalObjectZipPath = "3D/Objects/object_1.model";

        // --- Step 1: Renumber mesh objects starting from 1 and gather part info ---
        std::vector<ThreeMfMeshResource> external_resources;
        external_resources.reserve(document.mesh_resources.size());

        ExportedGroup group;
        group.assembly_name = "ChromaPrint3D-Model";
        group.objects.reserve(document.mesh_resources.size());

        uint32_t part_id = 1;
        for (auto& mesh_resource : document.mesh_resources) {
            const std::size_t source_idx = mesh_resource.source_input_index;
            if (source_idx >= objects.size()) {
                throw InputError("Bambu metadata source object index out of range");
            }

            int face_count = static_cast<int>(mesh_resource.triangles.size() / 3);
            group.total_face_count += face_count;

            std::string name =
                mesh_resource.name.empty() ? objects[source_idx].name : mesh_resource.name;
            if (name.empty()) { name = "Part " + std::to_string(part_id); }

            group.objects.push_back(ExportedObject{
                .name          = name,
                .filament_slot = ResolveFilamentSlot(input_slots_, source_idx),
                .part_id       = static_cast<int>(part_id),
                .face_count    = face_count,
            });

            mesh_resource.object_id       = part_id;
            mesh_resource.object_property = std::nullopt;
            external_resources.push_back(std::move(mesh_resource));
            ++part_id;
        }

        uint32_t assembly_id     = part_id; // next available ID after all parts
        group.assembly_object_id = static_cast<int>(assembly_id);

        // --- Step 2: Generate external objects model XML ---
        std::string objects_xml = BuildObjectsModelXml(external_resources);
        AddExtraMetadataPart(document, kExternalObjectZipPath,
                             "application/vnd.ms-package.3dmanufacturing-3dmodel+xml", objects_xml);

        // --- Step 3: Set up assembly structure on the document ---
        document.assembly_object_id = assembly_id;
        document.assembly_components.clear();
        document.assembly_components.reserve(external_resources.size());
        for (const auto& res : external_resources) {
            document.assembly_components.push_back(ThreeMfAssemblyComponent{
                .objectid      = res.object_id,
                .external_path = kExternalObjectPath,
                .transform     = ThreeMfTransform::Identity(),
            });
        }
        document.assembly_build_transform = ThreeMfTransform::Identity();

        // Clear flat model data (meshes live in external file now)
        document.mesh_resources.clear();
        document.base_materials.clear();
        document.base_material_group_id = std::nullopt;
        document.build_items.clear();
        document.build_items.push_back(ThreeMfBuildItem{
            .object_id = assembly_id,
            .transform = ThreeMfTransform::Identity(),
        });

        // --- Step 4: Set BambuStudio metadata ---
        auto remove_key = [&](const std::string& key) {
            document.metadata.erase(
                std::remove_if(document.metadata.begin(), document.metadata.end(),
                               [&](const ThreeMfMetadataEntry& e) { return e.name == key; }),
                document.metadata.end());
        };
        remove_key("3mfVersion");
        document.metadata.push_back({"BambuStudio:3mfVersion", "1"});

        // --- Step 5: Generate Bambu metadata files ---
        AddExtraMetadataPart(document, "Metadata/project_settings.config",
                             "application/octet-stream", BuildProjectSettings(preset_));
        AddExtraMetadataPart(document, "Metadata/model_settings.config", "application/octet-stream",
                             BuildModelSettings(group));
        AddExtraMetadataPart(document, "Metadata/slice_info.config", "application/octet-stream",
                             BuildSliceInfo());
        AddExtraMetadataPart(document, "Metadata/cut_information.xml", "application/xml",
                             BuildCutInformation(group));
        AddExtraMetadataPart(document, "Metadata/filament_sequence.json", "application/json",
                             BuildFilamentSequence());

        // --- Step 6: OPC relationships ---
        constexpr const char* kBambuSettingsRelType =
            "http://schemas.bambulab.com/package/2021/settings";
        constexpr const char* k3dModelRelType =
            "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";

        std::vector<OpcRelationship> model_rels;
        model_rels.reserve(6);

        // Reference to external objects file
        model_rels.push_back(OpcRelationship{
            .id     = "rel-1",
            .type   = k3dModelRelType,
            .target = kExternalObjectPath,
        });

        // Bambu metadata file references
        const std::array<const char*, 5> metadata_targets = {
            "/Metadata/project_settings.config", "/Metadata/model_settings.config",
            "/Metadata/slice_info.config",       "/Metadata/cut_information.xml",
            "/Metadata/filament_sequence.json",
        };
        for (std::size_t i = 0; i < metadata_targets.size(); ++i) {
            model_rels.push_back(OpcRelationship{
                .id     = "bambuRel" + std::to_string(i),
                .type   = kBambuSettingsRelType,
                .target = metadata_targets[i],
            });
        }

        document.relationship_sets.push_back(OpcRelationshipSet{
            .source_part_path = "3D/3dmodel.model",
            .relationships    = std::move(model_rels),
        });

        // --- Step 7: Content type registrations ---
        document.content_type_defaults.push_back(OpcContentTypeDefault{
            .extension = "config", .content_type = "application/octet-stream"});
        document.content_type_defaults.push_back(
            OpcContentTypeDefault{.extension = "xml", .content_type = "application/xml"});
        document.content_type_defaults.push_back(
            OpcContentTypeDefault{.extension = "json", .content_type = "application/json"});

        spdlog::info(
            "Bambu assembly extension: {} parts, assembly_id={}, {} filaments, {} metadata files",
            group.objects.size(), assembly_id, preset_.filaments.size(), metadata_targets.size());
    }

private:
    SlicerPreset preset_;
    std::vector<int> input_slots_;
};

} // namespace

std::unique_ptr<IThreeMfExtension> MakeCoreModelExtension() {
    return std::make_unique<CoreModelExtension>();
}

std::unique_ptr<IThreeMfExtension> MakeBaseMaterialExtension() {
    return std::make_unique<BaseMaterialExtension>();
}

std::unique_ptr<IThreeMfExtension> MakeUnitAndMetadataExtension() {
    return std::make_unique<UnitAndMetadataExtension>();
}

std::unique_ptr<IThreeMfExtension> MakeBuildTransformExtension() {
    return std::make_unique<BuildTransformExtension>();
}

std::unique_ptr<IThreeMfExtension> MakeBambuMetadataExtension(const SlicerPreset& preset,
                                                              std::vector<int> input_slots) {
    return std::make_unique<BambuMetadataExtension>(preset, std::move(input_slots));
}

} // namespace ChromaPrint3D::detail
