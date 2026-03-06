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
    const std::array<float, 12> identity = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                                            0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
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

    bool has_geometry = !document.mesh_resources.empty() || document.assembly_object_id.has_value();
    if (!has_geometry || document.build_items.empty()) {
        throw InputError("No geometry to export");
    }
    return document;
}

std::vector<uint8_t>
ThreeMfWriter::WriteToBuffer(const std::vector<ThreeMfInputObject>& objects) const {
    ThreeMfDocument document   = BuildDocument(objects);
    std::vector<OpcPart> parts = BuildOpcParts(document);
    return BuildZipPackage(parts, options_);
}

void ThreeMfWriter::WriteToFile(const std::string& path,
                                const std::vector<ThreeMfInputObject>& objects) const {
    if (path.empty()) { throw InputError("3MF output path is empty"); }
    std::vector<uint8_t> buffer = WriteToBuffer(objects);

    std::ofstream ofs(path, std::ios::binary);
    if (!ofs.is_open()) { throw IOError("Cannot open file for writing: " + path); }
    ofs.write(reinterpret_cast<const char*>(buffer.data()),
              static_cast<std::streamsize>(buffer.size()));
    if (!ofs.good()) { throw IOError("Failed to write 3MF file: " + path); }
}

} // namespace ChromaPrint3D::detail
