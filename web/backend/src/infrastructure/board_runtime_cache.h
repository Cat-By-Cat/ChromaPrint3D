#pragma once

#include "chromaprint3d/calib.h"

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chromaprint3d::backend {

struct BoardRecord {
    std::vector<uint8_t> model_3mf;
    ChromaPrint3D::CalibrationBoardMeta meta;
    std::chrono::steady_clock::time_point created_at;
};

struct BoardGeometryKey {
    int num_channels  = 0;
    int color_layers  = 0;
    int board_variant = 0;

    bool operator==(const BoardGeometryKey& other) const {
        return num_channels == other.num_channels && color_layers == other.color_layers &&
               board_variant == other.board_variant;
    }
};

struct BoardGeometryKeyHash {
    std::size_t operator()(const BoardGeometryKey& k) const;
};

class BoardRuntimeCache {
public:
    explicit BoardRuntimeCache(std::int64_t ttl_seconds);

    std::string StoreBoard(ChromaPrint3D::CalibrationBoardResult&& result);
    std::optional<BoardRecord> FindBoard(const std::string& id);

    std::optional<ChromaPrint3D::CalibrationBoardMeshes>
    FindGeometry(int num_channels, int color_layers, int board_variant = 0);
    void StoreGeometry(int num_channels, int color_layers,
                       ChromaPrint3D::CalibrationBoardMeshes&& data, int board_variant = 0);

private:
    void CleanupExpiredLocked(const std::chrono::steady_clock::time_point& now);
    static std::string NewBoardId();

    std::int64_t ttl_seconds_;
    std::mutex mtx_;
    std::unordered_map<std::string, BoardRecord> boards_;
    std::unordered_map<BoardGeometryKey, ChromaPrint3D::CalibrationBoardMeshes,
                       BoardGeometryKeyHash>
        geometry_;
};

} // namespace chromaprint3d::backend
