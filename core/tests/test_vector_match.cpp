#include <gtest/gtest.h>

#include "chromaprint3d/color.h"
#include "chromaprint3d/color_db.h"
#include "chromaprint3d/print_profile.h"
#include "chromaprint3d/vector_proc.h"
#include "chromaprint3d/vector_recipe_map.h"

#include <string>
#include <vector>

using namespace ChromaPrint3D;

namespace {

ColorDB MakePaletteReorderedDb() {
    ColorDB db;
    db.name             = "vector_match_test_db";
    db.max_color_layers = 5;
    db.base_layers      = 3;
    db.base_channel_idx = 0; // white in source DB.
    db.layer_height_mm  = 0.08f;
    db.line_width_mm    = 0.42f;
    db.layer_order      = LayerOrder::Top2Bottom;

    // Intentionally non-lexicographic order to trigger profile remap.
    db.palette = {Channel{"White", "PLA"}, Channel{"Black", "PLA"}};

    Entry e;
    e.lab    = Lab::FromRgb(Rgb::FromRgb255(255, 255, 255));
    e.recipe = {0, 0, 0, 0, 0}; // Source DB channel index 0 (White).
    db.entries.push_back(e);
    return db;
}

VectorProcResult MakeSingleColorVector(const Rgb& color) {
    VectorProcResult result;
    result.width_mm  = 10.0f;
    result.height_mm = 10.0f;

    VectorShape shape;
    shape.fill_type  = FillType::Solid;
    shape.fill_color = color;
    result.shapes.push_back(shape);
    return result;
}

int FindChannelIndexByName(const PrintProfile& profile, const std::string& color_name) {
    for (std::size_t i = 0; i < profile.palette.size(); ++i) {
        if (profile.palette[i].color == color_name) { return static_cast<int>(i); }
    }
    return -1;
}

} // namespace

TEST(VectorMatch, RemapsRecipeToProfilePaletteOrder) {
    ColorDB db = MakePaletteReorderedDb();
    std::vector<ColorDB> dbs{db};
    PrintProfile profile = PrintProfile::BuildFromColorDBs(dbs, PrintMode::Mode0p08x5);
    VectorProcResult vec = MakeSingleColorVector(Rgb::FromRgb255(255, 255, 255));

    MatchConfig cfg;
    cfg.color_space = ColorSpace::Lab;

    VectorRecipeMap map = VectorRecipeMap::Match(vec, dbs, profile, cfg);
    ASSERT_EQ(map.entries.size(), 1u);
    ASSERT_EQ(map.entries[0].recipe.size(), static_cast<std::size_t>(profile.color_layers));

    const int white_idx = FindChannelIndexByName(profile, "White");
    ASSERT_GE(white_idx, 0);

    for (uint8_t ch : map.entries[0].recipe) { EXPECT_EQ(ch, static_cast<uint8_t>(white_idx)); }
}
