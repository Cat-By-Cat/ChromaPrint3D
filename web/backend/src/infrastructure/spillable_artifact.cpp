#include "infrastructure/spillable_artifact.h"

#include <spdlog/spdlog.h>

#include <system_error>
#include <utility>

namespace chromaprint3d::backend {

SpillableArtifact::~SpillableArtifact() { Cleanup(); }

SpillableArtifact::SpillableArtifact(SpillableArtifact&& other) noexcept
    : path_(std::move(other.path_)), size_(other.size_) {
    other.path_.clear();
    other.size_ = 0;
}

SpillableArtifact& SpillableArtifact::operator=(SpillableArtifact&& other) noexcept {
    if (this != &other) {
        Cleanup();
        path_ = std::move(other.path_);
        size_ = other.size_;
        other.path_.clear();
        other.size_ = 0;
    }
    return *this;
}

SpillableArtifact SpillableArtifact::FromFile(std::filesystem::path path, std::size_t size) {
    SpillableArtifact a;
    a.path_ = std::move(path);
    a.size_ = size;
    return a;
}

void SpillableArtifact::Cleanup() noexcept {
    if (path_.empty()) return;
    std::error_code ec;
    if (std::filesystem::remove(path_, ec)) {
        spdlog::debug("SpillableArtifact: removed temp file {}", path_.string());
    } else if (ec) {
        spdlog::warn("SpillableArtifact: failed to remove {}: {}", path_.string(), ec.message());
    }
    path_.clear();
    size_ = 0;
}

} // namespace chromaprint3d::backend
