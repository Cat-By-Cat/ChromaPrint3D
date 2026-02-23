#pragma once

#include "color_db_cache.h"
#include "session.h"
#include "board_cache.h"
#include "board_geometry_cache.h"
#include "recipe_store.h"
#include "task_manager.h"
#include "matting_task_manager.h"

#include "chromaprint3d/model_package.h"
#include "chromaprint3d/matting.h"

#include <httplib/httplib.h>

#include <string>

using namespace ChromaPrint3D;

struct ServerContext {
    httplib::Server& server;
    ConvertTaskManager& task_mgr;
    ColorDBCache& db_cache;
    const ModelPackage* model_pack;
    SessionManager& session_mgr;
    BoardCache& board_cache;
    BoardGeometryCache& geometry_cache;
    EightColorRecipeStore recipe_store;
    const MattingRegistry& matting_registry;
    MattingTaskMgr& matting_task_mgr;
};
