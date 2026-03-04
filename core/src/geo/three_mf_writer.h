#pragma once

#include "three_mf_ir.h"

#include <memory>
#include <string>
#include <vector>

namespace ChromaPrint3D {
struct SlicerPreset;
}

namespace ChromaPrint3D::detail {

class IThreeMfExtension {
public:
    virtual ~IThreeMfExtension() = default;

    virtual int Priority() const = 0;

    virtual void Apply(ThreeMfDocument& document, const std::vector<ThreeMfInputObject>& objects,
                       const ThreeMfExportOptions& options) const = 0;
};

class ThreeMfWriter {
public:
    explicit ThreeMfWriter(ThreeMfExportOptions options = {});

    void RegisterExtension(std::unique_ptr<IThreeMfExtension> extension);

    std::vector<uint8_t> WriteToBuffer(const std::vector<ThreeMfInputObject>& objects) const;

    void WriteToFile(const std::string& path, const std::vector<ThreeMfInputObject>& objects) const;

private:
    ThreeMfDocument BuildDocument(const std::vector<ThreeMfInputObject>& objects) const;

    ThreeMfExportOptions options_;
    std::vector<std::unique_ptr<IThreeMfExtension>> extensions_;
};

// Extension factories (implemented in three_mf_extensions.cpp)
std::unique_ptr<IThreeMfExtension> MakeCoreModelExtension();
std::unique_ptr<IThreeMfExtension> MakeBaseMaterialExtension();
std::unique_ptr<IThreeMfExtension> MakeUnitAndMetadataExtension();
std::unique_ptr<IThreeMfExtension> MakeBuildTransformExtension();
std::unique_ptr<IThreeMfExtension> MakeBambuMetadataExtension(const SlicerPreset& preset,
                                                              std::vector<int> input_slots);

// OPC + serialization (implemented in three_mf_opc.cpp)
std::vector<OpcPart> BuildOpcParts(const ThreeMfDocument& document);

// ZIP package assembly (implemented in three_mf_zip.cpp)
std::vector<uint8_t> BuildZipPackage(const std::vector<OpcPart>& parts,
                                     const ThreeMfExportOptions& options);

} // namespace ChromaPrint3D::detail
