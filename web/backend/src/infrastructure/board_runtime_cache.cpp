#include "infrastructure/board_runtime_cache.h"
#include "infrastructure/random_utils.h"

namespace chromaprint3d::backend {

std::size_t BoardGeometryKeyHash::operator()(const BoardGeometryKey& k) const {
    std::size_t h = std::hash<int>{}(k.num_channels);
    h ^= std::hash<int>{}(k.color_layers) + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<int>{}(k.board_variant) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

BoardRuntimeCache::BoardRuntimeCache(std::int64_t ttl_seconds) : ttl_seconds_(ttl_seconds) {}

std::string BoardRuntimeCache::StoreBoard(ChromaPrint3D::CalibrationBoardResult&& result) {
    std::lock_guard<std::mutex> lock(mtx_);
    CleanupExpiredLocked(std::chrono::steady_clock::now());

    std::string id = NewBoardId();
    boards_[id]    = BoardRecord{std::move(result.model_3mf), std::move(result.meta),
                              std::chrono::steady_clock::now()};
    return id;
}

std::optional<BoardRecord> BoardRuntimeCache::FindBoard(const std::string& id) {
    std::lock_guard<std::mutex> lock(mtx_);
    CleanupExpiredLocked(std::chrono::steady_clock::now());
    auto it = boards_.find(id);
    if (it == boards_.end()) return std::nullopt;
    return it->second;
}

std::optional<ChromaPrint3D::CalibrationBoardMeshes>
BoardRuntimeCache::FindGeometry(int num_channels, int color_layers, int board_variant) {
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = geometry_.find(BoardGeometryKey{num_channels, color_layers, board_variant});
    if (it == geometry_.end()) return std::nullopt;
    return it->second;
}

void BoardRuntimeCache::StoreGeometry(int num_channels, int color_layers,
                                      ChromaPrint3D::CalibrationBoardMeshes&& data,
                                      int board_variant) {
    std::lock_guard<std::mutex> lock(mtx_);
    geometry_[BoardGeometryKey{num_channels, color_layers, board_variant}] = std::move(data);
}

void BoardRuntimeCache::CleanupExpiredLocked(const std::chrono::steady_clock::time_point& now) {
    for (auto it = boards_.begin(); it != boards_.end();) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - it->second.created_at).count();
        if (elapsed > ttl_seconds_) {
            it = boards_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string BoardRuntimeCache::NewBoardId() { return detail::RandomHex(16); }

} // namespace chromaprint3d::backend
