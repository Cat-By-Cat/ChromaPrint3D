#include <gtest/gtest.h>

#include "chromaprint3d/export_3mf.h"
#include "chromaprint3d/slicer_preset.h"
#include "chromaprint3d/voxel.h"

#include "lib3mf_implicit.hpp"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ChromaPrint3D;

namespace {

std::string GetPresetDir() {
    const char* env = std::getenv("CHROMAPRINT3D_PRESET_DIR");
    if (env) return env;
    const char* pwd = std::getenv("PWD");
    return std::string(pwd ? pwd : ".") + "/data/presets";
}

bool PresetFileExists() {
    return std::filesystem::exists(GetPresetDir() + "/bambu_p2s_0.08mm.json");
}

SlicerPreset MakeTestPreset() {
    SlicerPreset preset;
    preset.preset_json_path = GetPresetDir() + "/bambu_p2s_0.08mm.json";
    preset.filaments        = {
        {.type = "PLA", .colour = "#C12E1F"},
        {.type = "PLA", .colour = "#00AE42"},
        {.type = "PLA", .colour = "#0A2989"},
    };
    return preset;
}

Mesh MakeBoxMesh() {
    VoxelGrid grid;
    grid.width      = 2;
    grid.height     = 2;
    grid.num_layers = 1;
    grid.ooc.assign(4, 0);
    grid.Set(0, 0, 0, true);
    grid.Set(1, 0, 0, true);
    grid.Set(0, 1, 0, true);
    grid.Set(1, 1, 0, true);
    return Mesh::Build(grid);
}

struct Lib3mfReadResult {
    int attachment_count      = 0;
    bool has_project_settings = false;
    bool has_model_settings   = false;
    bool has_slice_info       = false;
    bool has_cut_info         = false;
    bool has_filament_seq     = false;
    std::string project_settings_json;
    std::string model_settings_xml;
};

Lib3mfReadResult ReadBackBuffer(const std::vector<uint8_t>& buffer) {
    Lib3mfReadResult result;
    auto wrapper = Lib3MF::CWrapper::loadLibrary();
    auto model   = wrapper->CreateModel();
    auto reader  = model->QueryReader("3mf");
    reader->AddRelationToRead("http://schemas.bambulab.com/package/2021/settings");
    reader->ReadFromBuffer(buffer);

    result.attachment_count = static_cast<int>(model->GetAttachmentCount());
    for (Lib3MF_uint32 i = 0; i < model->GetAttachmentCount(); ++i) {
        auto att         = model->GetAttachment(i);
        std::string path = att->GetPath();

        std::vector<Lib3MF_uint8> data;
        att->WriteToBuffer(data);
        std::string content(data.begin(), data.end());

        if (path.find("project_settings.config") != std::string::npos) {
            result.has_project_settings  = true;
            result.project_settings_json = content;
        } else if (path.find("model_settings.config") != std::string::npos) {
            result.has_model_settings = true;
            result.model_settings_xml = content;
        } else if (path.find("slice_info.config") != std::string::npos) {
            result.has_slice_info = true;
        } else if (path.find("cut_information.xml") != std::string::npos) {
            result.has_cut_info = true;
        } else if (path.find("filament_sequence.json") != std::string::npos) {
            result.has_filament_seq = true;
        }
    }
    return result;
}

} // namespace

TEST(SlicerPreset, FindPresetFileFindsExisting) {
    std::string dir  = GetPresetDir();
    std::string path = FindPresetFile(dir, "Bambu Lab P2S", 0.08f);
    if (PresetFileExists()) {
        ASSERT_FALSE(path.empty());
        EXPECT_NE(path.find("bambu_p2s_0.08mm.json"), std::string::npos);
    }
}

TEST(SlicerPreset, FindPresetFileReturnsEmptyForMissing) {
    std::string dir  = GetPresetDir();
    std::string path = FindPresetFile(dir, "Bambu Lab P2S", 0.99f);
    EXPECT_TRUE(path.empty());
}

TEST(SlicerPreset, Export3mfContainsBambuMetadata) {
    if (!PresetFileExists()) { GTEST_SKIP() << "Preset file not found"; }
    SlicerPreset preset = MakeTestPreset();

    std::vector<Mesh> meshes     = {MakeBoxMesh(), MakeBoxMesh()};
    std::vector<Channel> palette = {{"Red", "PLA"}, {"Green", "PLA"}};

    auto buffer = Export3mfFromMeshes(meshes, palette, -1, 0, preset);
    ASSERT_FALSE(buffer.empty());

    auto result = ReadBackBuffer(buffer);
    EXPECT_GE(result.attachment_count, 5);
    EXPECT_TRUE(result.has_project_settings);
    EXPECT_TRUE(result.has_model_settings);
    EXPECT_TRUE(result.has_slice_info);
    EXPECT_TRUE(result.has_cut_info);
    EXPECT_TRUE(result.has_filament_seq);
}

TEST(SlicerPreset, ProjectSettingsContainsPatchedFilaments) {
    if (!PresetFileExists()) { GTEST_SKIP() << "Preset file not found"; }
    SlicerPreset preset = MakeTestPreset();

    std::vector<Mesh> meshes     = {MakeBoxMesh()};
    std::vector<Channel> palette = {{"Red", "PLA"}};

    auto buffer = Export3mfFromMeshes(meshes, palette, -1, 0, preset);
    auto result = ReadBackBuffer(buffer);
    ASSERT_FALSE(result.project_settings_json.empty());

    auto j = nlohmann::json::parse(result.project_settings_json);
    EXPECT_EQ(j["filament_colour"][0], "#C12E1F");
    EXPECT_EQ(j["filament_colour"][1], "#00AE42");
    EXPECT_EQ(j["filament_colour"][2], "#0A2989");
    EXPECT_EQ(j["filament_multi_colour"][0], "#C12E1F");
    EXPECT_EQ(j["filament_vendor"][0], "Bambu Lab");
    EXPECT_EQ(j["filament_ids"][0], "GFA00");
    EXPECT_EQ(j["from"], "project");
    EXPECT_TRUE(j.contains("layer_height"));
    EXPECT_TRUE(j.contains("printer_model"));
}

TEST(SlicerPreset, ModelSettingsXmlContainsObjectsAndPlate) {
    if (!PresetFileExists()) { GTEST_SKIP() << "Preset file not found"; }
    SlicerPreset preset = MakeTestPreset();

    std::vector<Mesh> meshes     = {MakeBoxMesh(), MakeBoxMesh()};
    std::vector<Channel> palette = {{"Red", "PLA"}, {"Green", "PLA"}};

    auto buffer = Export3mfFromMeshes(meshes, palette, -1, 0, preset);
    auto result = ReadBackBuffer(buffer);
    ASSERT_FALSE(result.model_settings_xml.empty());

    EXPECT_NE(result.model_settings_xml.find("<config>"), std::string::npos);
    EXPECT_NE(result.model_settings_xml.find("plater_id"), std::string::npos);
    EXPECT_NE(result.model_settings_xml.find("Red - PLA"), std::string::npos);
    EXPECT_NE(result.model_settings_xml.find("Green - PLA"), std::string::npos);
    EXPECT_NE(result.model_settings_xml.find("extruder\" value=\"1\""), std::string::npos);
    EXPECT_NE(result.model_settings_xml.find("extruder\" value=\"2\""), std::string::npos);
}

TEST(SlicerPreset, ExportWithoutPresetStillWorks) {
    std::vector<Mesh> meshes     = {MakeBoxMesh()};
    std::vector<Channel> palette = {{"Red", "PLA"}};

    auto buffer = Export3mfFromMeshes(meshes, palette, -1, 0);
    ASSERT_FALSE(buffer.empty());

    auto result = ReadBackBuffer(buffer);
    EXPECT_EQ(result.attachment_count, 0);
    EXPECT_FALSE(result.has_project_settings);
}

TEST(SlicerPreset, WriteToTempFile) {
    if (!PresetFileExists()) { GTEST_SKIP() << "Preset file not found"; }
    SlicerPreset preset = MakeTestPreset();

    std::vector<Mesh> meshes     = {MakeBoxMesh(), MakeBoxMesh()};
    std::vector<Channel> palette = {{"Red", "PLA"}, {"Green", "PLA"}};

    auto buffer = Export3mfFromMeshes(meshes, palette, -1, 0, preset);
    ASSERT_FALSE(buffer.empty());

    auto tmp = std::filesystem::temp_directory_path() / "chromaprint3d_test_preset.3mf";
    {
        std::ofstream ofs(tmp, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(buffer.data()),
                  static_cast<std::streamsize>(buffer.size()));
    }
    EXPECT_TRUE(std::filesystem::exists(tmp));
    std::cerr << "Wrote test 3MF to: " << tmp << " (" << buffer.size() << " bytes)" << std::endl;
}
