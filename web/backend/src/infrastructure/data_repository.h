#pragma once

#include "config/server_config.h"
#include "server/color_db_cache.h"
#include "server/recipe_store.h"

#include "chromaprint3d/matting.h"
#include "chromaprint3d/model_package.h"
#if defined(CHROMAPRINT3D_HAS_INFER) && CHROMAPRINT3D_HAS_INFER
#    include "chromaprint3d/infer/engine.h"
#endif

#include <memory>
#include <optional>
#include <string>

namespace chromaprint3d::backend {

class DataRepository {
public:
    explicit DataRepository(const ServerConfig& cfg);

    const ColorDBCache& ColorDbCache() const { return db_cache_; }

    const std::optional<ChromaPrint3D::ModelPackage>& ModelPack() const { return model_pack_; }

    const EightColorRecipeStore& RecipeStore() const { return recipe_store_; }

    const ChromaPrint3D::MattingRegistry& MattingRegistry() const { return matting_registry_; }

private:
    ColorDBCache db_cache_;
    std::optional<ChromaPrint3D::ModelPackage> model_pack_;
    EightColorRecipeStore recipe_store_;
    ChromaPrint3D::MattingRegistry matting_registry_;
#if defined(CHROMAPRINT3D_HAS_INFER) && CHROMAPRINT3D_HAS_INFER
    // Keep infer engine alive for the full DataRepository lifetime.
    std::unique_ptr<ChromaPrint3D::infer::InferenceEngine> infer_engine_;
#endif
};

} // namespace chromaprint3d::backend
