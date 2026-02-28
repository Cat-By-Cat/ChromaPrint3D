#include "infrastructure/board_runtime_cache.h"

#include <fstream>
#include <random>

namespace chromaprint3d::backend {
namespace {

std::string ToHex(const unsigned char* data, std::size_t len) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        auto b = data[i];
        out.push_back(kHex[(b >> 4U) & 0x0F]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

} // namespace

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

std::string BoardRuntimeCache::NewBoardId() {
    unsigned char bytes[16] = {};
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if (urandom.good()) {
        urandom.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
        if (urandom.gcount() == static_cast<std::streamsize>(sizeof(bytes))) {
            return ToHex(bytes, sizeof(bytes));
        }
    }

    std::random_device rd;
    for (auto& b : bytes) b = static_cast<unsigned char>(rd() & 0xFF);
    return ToHex(bytes, sizeof(bytes));
}

} // namespace chromaprint3d::backend
