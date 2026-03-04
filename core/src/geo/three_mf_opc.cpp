#include "three_mf_writer.h"

#include "chromaprint3d/error.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <limits>
#include <map>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace ChromaPrint3D::detail {

namespace {

constexpr std::string_view kCoreNs = "http://schemas.microsoft.com/3dmanufacturing/core/2015/02";
constexpr std::string_view kModelRelationshipType =
    "http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel";
constexpr std::string_view kModelContentType =
    "application/vnd.ms-package.3dmanufacturing-3dmodel+xml";
constexpr std::string_view kRelationshipContentType =
    "application/vnd.openxmlformats-package.relationships+xml";

std::string NormalizeZipPath(const std::string& path) {
    if (path.empty()) { throw InputError("OPC part path is empty"); }
    std::string normalized = path;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (!normalized.empty() && normalized.front() == '/') { normalized.erase(0, 1); }
    if (normalized.empty()) { throw InputError("OPC part path is empty"); }
    if (normalized.find("..") != std::string::npos) {
        throw InputError("OPC part path contains invalid segment: " + path);
    }
    return normalized;
}

std::string NormalizePartName(const std::string& part_name) {
    return "/" + NormalizeZipPath(part_name);
}

std::string NormalizeExtension(std::string extension) {
    if (extension.empty()) { throw InputError("Content type extension is empty"); }
    while (!extension.empty() && extension.front() == '.') { extension.erase(0, 1); }
    if (extension.empty()) { throw InputError("Content type extension is empty"); }
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

std::string NormalizeRelationshipSourcePartPath(const std::string& source_part_path) {
    if (source_part_path.empty() || source_part_path == "/") { return {}; }
    return NormalizeZipPath(source_part_path);
}

std::string BuildRelationshipPartPathForSource(const std::string& source_part_path) {
    if (source_part_path.empty()) { return "_rels/.rels"; }
    const std::string source = NormalizeZipPath(source_part_path);
    const auto slash_pos     = source.rfind('/');
    if (slash_pos == std::string::npos) { return "_rels/" + source + ".rels"; }
    return source.substr(0, slash_pos) + "/_rels/" + source.substr(slash_pos + 1) + ".rels";
}

std::string UnitToString(ThreeMfUnit unit) {
    switch (unit) {
    case ThreeMfUnit::Micron:
        return "micron";
    case ThreeMfUnit::Millimeter:
        return "millimeter";
    case ThreeMfUnit::Centimeter:
        return "centimeter";
    case ThreeMfUnit::Inch:
        return "inch";
    case ThreeMfUnit::Foot:
        return "foot";
    case ThreeMfUnit::Meter:
        return "meter";
    }
    return "millimeter";
}

std::string EscapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 4);
    for (char c : s) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&apos;";
            break;
        default:
            out.push_back(c);
            break;
        }
    }
    return out;
}

std::string FormatFloat(float value) {
    if (!std::isfinite(value)) { throw InputError("Non-finite float in 3MF serialization"); }
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general,
                                std::numeric_limits<float>::max_digits10);
    if (result.ec != std::errc()) { throw IOError("Failed to format float value"); }
    return std::string(buffer, result.ptr);
}

std::string SerializeTransform(const ThreeMfTransform& transform) {
    std::string out;
    out.reserve(12 * 12);
    for (std::size_t i = 0; i < transform.values.size(); ++i) {
        if (i > 0) out.push_back(' ');
        out += FormatFloat(transform.values[i]);
    }
    return out;
}

std::string BuildModelXml(const ThreeMfDocument& document) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<model unit=\"" << UnitToString(document.unit) << "\" xml:lang=\"en-US\" xmlns=\""
        << kCoreNs << "\">\n";

    for (const auto& metadata : document.metadata) {
        if (metadata.name.empty()) { continue; }
        xml << "  <metadata name=\"" << EscapeXml(metadata.name) << "\">"
            << EscapeXml(metadata.value) << "</metadata>\n";
    }

    xml << "  <resources>\n";
    if (document.base_material_group_id.has_value() && !document.base_materials.empty()) {
        xml << "    <basematerials id=\"" << document.base_material_group_id.value() << "\">\n";
        for (const auto& material : document.base_materials) {
            xml << "      <base name=\"" << EscapeXml(material.name) << "\" displaycolor=\""
                << EscapeXml(material.display_color_hex) << "\"/>\n";
        }
        xml << "    </basematerials>\n";
    }

    for (const auto& object : document.mesh_resources) {
        xml << "    <object id=\"" << object.object_id << "\"";
        if (!object.name.empty()) { xml << " name=\"" << EscapeXml(object.name) << "\""; }
        xml << " type=\"model\"";
        if (object.object_property.has_value()) {
            xml << " pid=\"" << object.object_property->pid << "\" pindex=\""
                << object.object_property->pindex << "\"";
        }
        xml << ">\n";
        xml << "      <mesh>\n";
        xml << "        <vertices>\n";
        for (std::size_t i = 0; i + 2 < object.vertices_xyz.size(); i += 3) {
            xml << "          <vertex x=\"" << FormatFloat(object.vertices_xyz[i]) << "\" y=\""
                << FormatFloat(object.vertices_xyz[i + 1]) << "\" z=\""
                << FormatFloat(object.vertices_xyz[i + 2]) << "\"/>\n";
        }
        xml << "        </vertices>\n";
        xml << "        <triangles>\n";
        for (std::size_t i = 0; i + 2 < object.triangles.size(); i += 3) {
            xml << "          <triangle v1=\"" << object.triangles[i] << "\" v2=\""
                << object.triangles[i + 1] << "\" v3=\"" << object.triangles[i + 2] << "\"/>\n";
        }
        xml << "        </triangles>\n";
        xml << "      </mesh>\n";
        xml << "    </object>\n";
    }

    xml << "  </resources>\n";
    xml << "  <build>\n";
    for (const auto& item : document.build_items) {
        xml << "    <item objectid=\"" << item.object_id << "\"";
        if (!item.transform.IsIdentity()) {
            xml << " transform=\"" << SerializeTransform(item.transform) << "\"";
        }
        xml << "/>\n";
    }
    xml << "  </build>\n";
    xml << "</model>\n";
    return xml.str();
}

std::string BuildRelationshipsXml(const std::vector<OpcRelationship>& relationships) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<Relationships "
           "xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n";
    for (const auto& rel : relationships) {
        if (rel.id.empty()) { throw InputError("OPC relationship id is empty"); }
        if (rel.type.empty()) { throw InputError("OPC relationship type is empty"); }
        if (rel.target.empty()) { throw InputError("OPC relationship target is empty"); }
        xml << "  <Relationship Target=\"" << EscapeXml(rel.target) << "\" Id=\""
            << EscapeXml(rel.id) << "\" Type=\"" << EscapeXml(rel.type) << "\"/>\n";
    }
    xml << "</Relationships>\n";
    return xml.str();
}

template <typename MapType>
void InsertOrValidateType(MapType& entries, const std::string& key, const std::string& content_type,
                          const std::string& kind) {
    if (key.empty()) { throw InputError(kind + " key is empty"); }
    if (content_type.empty()) { throw InputError(kind + " content type is empty"); }
    auto [it, inserted] = entries.emplace(key, content_type);
    if (!inserted && it->second != content_type) {
        throw InputError("Conflicting OPC " + kind + " registration for " + key);
    }
}

std::string BuildContentTypesXml(const std::map<std::string, std::string>& defaults,
                                 const std::map<std::string, std::string>& overrides) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n";
    for (const auto& entry : defaults) {
        xml << "  <Default Extension=\"" << EscapeXml(entry.first) << "\" ContentType=\""
            << EscapeXml(entry.second) << "\"/>\n";
    }
    for (const auto& entry : overrides) {
        xml << "  <Override PartName=\"" << EscapeXml(entry.first) << "\" ContentType=\""
            << EscapeXml(entry.second) << "\"/>\n";
    }
    xml << "</Types>\n";
    return xml.str();
}

} // namespace

std::vector<OpcPart> BuildOpcParts(const ThreeMfDocument& document) {
    if (document.mesh_resources.empty() || document.build_items.empty()) {
        throw InputError("3MF document has no mesh/build items");
    }

    std::vector<OpcPart> parts;
    parts.reserve(3 + document.relationship_sets.size() + document.extra_parts.size());

    const std::string model_part_path = "3D/3dmodel.model";
    parts.push_back(OpcPart{
        .path_in_zip  = model_part_path,
        .content_type = std::string(kModelContentType),
        .data         = BuildModelXml(document),
    });

    std::unordered_map<std::string, std::vector<OpcRelationship>> relationships_by_source;
    relationships_by_source[""] = {
        OpcRelationship{
            .id     = "rel0",
            .type   = std::string(kModelRelationshipType),
            .target = "/" + model_part_path,
        },
    };

    std::unordered_map<std::string, std::unordered_map<std::string, OpcRelationship>>
        relationship_id_lookup;
    relationship_id_lookup[""] = {
        {"rel0", relationships_by_source[""].front()},
    };

    for (const auto& set : document.relationship_sets) {
        const std::string source_path = NormalizeRelationshipSourcePartPath(set.source_part_path);
        auto& rels_for_source         = relationships_by_source[source_path];
        auto& ids_for_source          = relationship_id_lookup[source_path];
        for (const auto& rel : set.relationships) {
            if (rel.id.empty()) { throw InputError("OPC relationship id is empty"); }
            auto [it, inserted] = ids_for_source.emplace(rel.id, rel);
            if (!inserted) {
                if (it->second.type != rel.type || it->second.target != rel.target) {
                    throw InputError("Conflicting OPC relationship id " + rel.id +
                                     " for source part " +
                                     (source_path.empty() ? std::string("<root>") : source_path));
                }
                continue;
            }
            rels_for_source.push_back(rel);
        }
    }

    std::unordered_map<std::string, std::string> part_content_types;
    std::unordered_set<std::string> package_part_paths;
    part_content_types.emplace(model_part_path, std::string(kModelContentType));
    package_part_paths.emplace(model_part_path);

    for (const auto& extra_part : document.extra_parts) {
        OpcPart normalized_part     = extra_part;
        normalized_part.path_in_zip = NormalizeZipPath(extra_part.path_in_zip);
        package_part_paths.emplace(normalized_part.path_in_zip);
        parts.push_back(std::move(normalized_part));

        if (!extra_part.content_type.empty()) {
            auto [it, inserted] = part_content_types.emplace(
                NormalizeZipPath(extra_part.path_in_zip), extra_part.content_type);
            if (!inserted && it->second != extra_part.content_type) {
                throw InputError("Conflicting content type for OPC part " +
                                 NormalizeZipPath(extra_part.path_in_zip));
            }
        }
    }

    for (const auto& entry : relationships_by_source) {
        const std::string& source_path = entry.first;
        if (!source_path.empty() &&
            package_part_paths.find(source_path) == package_part_paths.end()) {
            throw InputError("Relationship source part does not exist in package: " + source_path);
        }
    }

    std::vector<std::string> source_paths;
    source_paths.reserve(relationships_by_source.size());
    for (const auto& entry : relationships_by_source) { source_paths.push_back(entry.first); }
    std::sort(source_paths.begin(), source_paths.end());

    for (const std::string& source_path : source_paths) {
        auto rels = relationships_by_source[source_path];
        std::sort(rels.begin(), rels.end(), [](const OpcRelationship& a, const OpcRelationship& b) {
            if (a.id != b.id) { return a.id < b.id; }
            if (a.type != b.type) { return a.type < b.type; }
            return a.target < b.target;
        });

        const std::string rel_part_path = BuildRelationshipPartPathForSource(source_path);
        parts.push_back(OpcPart{
            .path_in_zip  = rel_part_path,
            .content_type = std::string(kRelationshipContentType),
            .data         = BuildRelationshipsXml(rels),
        });
    }

    std::map<std::string, std::string> defaults;
    std::map<std::string, std::string> overrides;
    InsertOrValidateType(defaults, "rels", std::string(kRelationshipContentType), "default");
    InsertOrValidateType(overrides, "/" + model_part_path, std::string(kModelContentType),
                         "override");

    for (const auto& entry : part_content_types) {
        InsertOrValidateType(overrides, NormalizePartName(entry.first), entry.second, "override");
    }
    for (const auto& def : document.content_type_defaults) {
        InsertOrValidateType(defaults, NormalizeExtension(def.extension), def.content_type,
                             "default");
    }
    for (const auto& ov : document.content_type_overrides) {
        InsertOrValidateType(overrides, NormalizePartName(ov.part_name), ov.content_type,
                             "override");
    }

    parts.push_back(OpcPart{
        .path_in_zip  = "[Content_Types].xml",
        .content_type = "application/xml",
        .data         = BuildContentTypesXml(defaults, overrides),
    });
    return parts;
}

} // namespace ChromaPrint3D::detail
