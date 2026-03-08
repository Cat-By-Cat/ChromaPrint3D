#include <gtest/gtest.h>
#include "chromaprint3d/voxel.h"

using namespace ChromaPrint3D;

TEST(VoxelGrid, GetSetBasic) {
    VoxelGrid grid;
    grid.width      = 4;
    grid.height     = 4;
    grid.num_layers = 3;
    grid.ooc.assign(static_cast<size_t>(4 * 4 * 3), 0);

    EXPECT_FALSE(grid.Get(0, 0, 0));

    grid.Set(2, 1, 1, true);
    EXPECT_TRUE(grid.Get(2, 1, 1));
    EXPECT_FALSE(grid.Get(2, 1, 0));

    grid.Set(2, 1, 1, false);
    EXPECT_FALSE(grid.Get(2, 1, 1));
}

TEST(VoxelGrid, OutOfBoundsReturnsFlase) {
    VoxelGrid grid;
    grid.width      = 2;
    grid.height     = 2;
    grid.num_layers = 2;
    grid.ooc.assign(static_cast<size_t>(2 * 2 * 2), 0);

    EXPECT_FALSE(grid.Get(-1, 0, 0));
    EXPECT_FALSE(grid.Get(0, -1, 0));
    EXPECT_FALSE(grid.Get(0, 0, -1));
    EXPECT_FALSE(grid.Get(3, 0, 0));
}

TEST(VoxelGrid, SetAllVoxels) {
    VoxelGrid grid;
    grid.width      = 3;
    grid.height     = 3;
    grid.num_layers = 2;
    grid.ooc.assign(static_cast<size_t>(3 * 3 * 2), 0);

    for (int w = 0; w < 3; ++w) {
        for (int h = 0; h < 3; ++h) {
            for (int l = 0; l < 2; ++l) { grid.Set(w, h, l, true); }
        }
    }

    for (int w = 0; w < 3; ++w) {
        for (int h = 0; h < 3; ++h) {
            for (int l = 0; l < 2; ++l) { EXPECT_TRUE(grid.Get(w, h, l)); }
        }
    }
}

TEST(ModelIR, DoubleSidedPlacesBaseInMiddle) {
    RecipeMap map;
    map.width        = 1;
    map.height       = 1;
    map.color_layers = 2;
    map.num_channels = 1;
    map.layer_order  = LayerOrder::Top2Bottom;
    map.recipes      = {0, 0};
    map.mask         = {255};

    ColorDB db;
    db.base_layers      = 1;
    db.base_channel_idx = 0;
    db.palette.push_back(Channel{"White", "PLA"});

    BuildModelIRConfig cfg;
    cfg.flip_y       = false;
    cfg.base_layers  = 1;
    cfg.double_sided = true;

    ModelIR model = ModelIR::Build(map, db, cfg);
    ASSERT_EQ(model.voxel_grids.size(), 2u); // ch0 + base
    ASSERT_EQ(model.base_layers, 1);
    ASSERT_EQ(model.color_layers, 2);

    const VoxelGrid& color_grid = model.voxel_grids[0];
    const VoxelGrid& base_grid  = model.voxel_grids[1];
    EXPECT_EQ(color_grid.num_layers, 5);
    EXPECT_EQ(base_grid.num_layers, 5);

    // mirrored color side
    EXPECT_TRUE(color_grid.Get(0, 0, 0));
    EXPECT_TRUE(color_grid.Get(0, 0, 1));
    // base layer stays in the middle
    EXPECT_FALSE(color_grid.Get(0, 0, 2));
    EXPECT_TRUE(base_grid.Get(0, 0, 2));
    // front color side
    EXPECT_TRUE(color_grid.Get(0, 0, 3));
    EXPECT_TRUE(color_grid.Get(0, 0, 4));
}
