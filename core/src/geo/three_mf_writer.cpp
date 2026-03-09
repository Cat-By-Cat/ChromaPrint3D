#include "three_mf_writer.h"

#include "chromaprint3d/error.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace ChromaPrint3D::detail {

namespace {

constexpr const char* kDefaultApplication = "ChromaPrint3D";

bool HasMetadataKey(const std::vector<ThreeMfMetadataEntry>& metadata, const std::string& key) {
    return std::any_of(metadata.begin(), metadata.end(),
                       [&](const ThreeMfMetadataEntry& entry) { return entry.name == key; });
}

} // namespace

bool ThreeMfTransform::IsIdentity(float eps) const {
    const auto& m                        = values;
    const std::array<float, 12> identity = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
                                            0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < m.size(); ++i) {
        if (std::abs(m[i] - identity[i]) > eps) { return false; }
    }
    return true;
}

ThreeMfWriter::ThreeMfWriter(ThreeMfExportOptions options) : options_(std::move(options)) {
    if (!HasMetadataKey(options_.metadata, "Application")) {
        options_.metadata.push_back({"Application", kDefaultApplication});
    }
    RegisterExtension(MakeUnitAndMetadataExtension());
    RegisterExtension(MakeBaseMaterialExtension());
    RegisterExtension(MakeCoreModelExtension());
    RegisterExtension(MakeBuildTransformExtension());
}

void ThreeMfWriter::RegisterExtension(std::unique_ptr<IThreeMfExtension> extension) {
    if (!extension) { return; }
    extensions_.push_back(std::move(extension));
    std::stable_sort(
        extensions_.begin(), extensions_.end(),
        [](const std::unique_ptr<IThreeMfExtension>& a,
           const std::unique_ptr<IThreeMfExtension>& b) { return a->Priority() < b->Priority(); });
}

ThreeMfDocument ThreeMfWriter::BuildDocument(const std::vector<ThreeMfInputObject>& objects) const {
    if (objects.empty()) { throw InputError("No objects to export"); }

    ThreeMfDocument document;
    for (const auto& extension : extensions_) { extension->Apply(document, objects, options_); }

    bool has_geometry = !document.mesh_resources.empty() ||
                        document.assembly_object_id.has_value() ||
                        document.deferred_mesh_part.has_value();
    if (!has_geometry || document.build_items.empty()) {
        throw InputError("No geometry to export");
    }
    return document;
}

namespace {

void WriteAllEntries(StreamingZipWriter& zip, const OpcPartSet& part_set,
                     const ThreeMfDocument& document) {
    for (const auto& part : part_set.parts) { zip.WriteWholeEntry(part.path_in_zip, part.data); }

    if (part_set.deferred) {
        auto format            = part_set.deferred->path_in_zip.empty() ? MeshXmlFormat::FlatModel
                                                                        : MeshXmlFormat::ObjectsModel;
        std::string entry_path = part_set.deferred->path_in_zip.empty()
                                     ? std::string("3D/3dmodel.model")
                                     : part_set.deferred->path_in_zip;
        zip.BeginDeflateEntry(entry_path);
        StreamMeshXml(part_set.deferred->resources, format, document,
                      [&](std::string_view chunk) { zip.WriteChunk(chunk.data(), chunk.size()); });
        zip.EndEntry();
    }

    zip.Finalize();
}

} // namespace

std::vector<uint8_t>
ThreeMfWriter::WriteToBuffer(const std::vector<ThreeMfInputObject>& objects) const {
    ThreeMfDocument document = BuildDocument(objects);
    OpcPartSet part_set      = BuildOpcPartSet(document);

    std::vector<uint8_t> zip_bytes;
    StreamingZipWriter zip(zip_bytes, options_);
    WriteAllEntries(zip, part_set, document);
    return zip_bytes;
}

void ThreeMfWriter::WriteToFile(const std::string& path,
                                const std::vector<ThreeMfInputObject>& objects) const {
    if (path.empty()) { throw InputError("3MF output path is empty"); }

    ThreeMfDocument document = BuildDocument(objects);
    OpcPartSet part_set      = BuildOpcPartSet(document);

    StreamingZipWriter zip(path, options_);
    WriteAllEntries(zip, part_set, document);
}

} // namespace ChromaPrint3D::detail
