#pragma once

#include "async_task_manager.h"
#include "chromaprint3d/pipeline.h"

namespace ChromaPrint3D {

struct ConvertTaskInfo : TaskBase {
    std::string image_name;
    ConvertStage stage = ConvertStage::LoadingResources;
    float progress     = 0.0f;
    ConvertResult result;
};

inline const char* ConvertStageToString(ConvertStage s) {
    switch (s) {
    case ConvertStage::LoadingResources: return "loading_resources";
    case ConvertStage::ProcessingImage:  return "processing_image";
    case ConvertStage::Matching:         return "matching";
    case ConvertStage::BuildingModel:    return "building_model";
    case ConvertStage::Exporting:        return "exporting";
    }
    return "unknown";
}

using ConvertTaskManager = AsyncTaskManager<ConvertTaskInfo>;

// Submit a convert request. Returns the task ID.
inline std::string SubmitConvert(ConvertTaskManager& mgr, ConvertRequest request) {
    ConvertTaskInfo initial;
    initial.image_name = request.image_name;

    return mgr.Submit(std::move(initial),
        [req = std::move(request)](const std::string& /*id*/, auto update) mutable {
            ProgressCallback progress_cb = [&update](ConvertStage stage, float progress) {
                update([&](ConvertTaskInfo& task) {
                    task.stage    = stage;
                    task.progress = progress;
                });
            };

            ConvertResult result = Convert(req, progress_cb);
            update([&](ConvertTaskInfo& task) {
                task.result   = std::move(result);
                task.progress = 1.0f;
            });
        });
}

} // namespace ChromaPrint3D
