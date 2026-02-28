#include "infrastructure/session_store.h"

#include <fstream>
#include <random>
#include <regex>
#include <sstream>

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

SessionStore::SessionStore(std::int64_t ttl_seconds, std::int64_t max_colordbs)
    : ttl_seconds_(ttl_seconds), max_colordbs_(max_colordbs) {}

std::string SessionStore::EnsureSession(const std::optional<std::string>& existing_token,
                                        bool* created) {
    CleanupExpired();

    std::string token = existing_token.value_or("");
    if (token.empty()) token = NewSecureToken();

    std::lock_guard<std::mutex> lock(mtx_);
    auto& s       = GetOrCreateLocked(token);
    s.last_access = std::chrono::steady_clock::now();
    if (created) *created = !existing_token.has_value() || existing_token->empty();
    return token;
}

std::optional<SessionSnapshot> SessionStore::Snapshot(const std::string& token) {
    if (token.empty()) return std::nullopt;
    CleanupExpired();

    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return std::nullopt;
    it->second.last_access = std::chrono::steady_clock::now();
    return it->second;
}

bool SessionStore::WithSession(const std::string& token,
                               const std::function<void(SessionSnapshot&)>& mutator) {
    if (token.empty()) return false;
    CleanupExpired();
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) return false;
    it->second.last_access = std::chrono::steady_clock::now();
    mutator(it->second);
    return true;
}

bool SessionStore::RemoveSessionDb(const std::string& token, const std::string& db_name) {
    return WithSession(token, [&](SessionSnapshot& s) { s.color_dbs.erase(db_name); });
}

std::optional<ChromaPrint3D::ColorDB> SessionStore::GetSessionDb(const std::string& token,
                                                                 const std::string& db_name) {
    auto s = Snapshot(token);
    if (!s) return std::nullopt;
    auto it = s->color_dbs.find(db_name);
    if (it == s->color_dbs.end()) return std::nullopt;
    return it->second;
}

std::vector<ChromaPrint3D::ColorDB> SessionStore::ListSessionDbs(const std::string& token) {
    auto s = Snapshot(token);
    std::vector<ChromaPrint3D::ColorDB> out;
    if (!s) return out;
    out.reserve(s->color_dbs.size());
    for (const auto& [_, db] : s->color_dbs) out.push_back(db);
    return out;
}

bool SessionStore::PutSessionDb(const std::string& token, ChromaPrint3D::ColorDB db,
                                std::string* error) {
    if (!IsValidDbName(db.name)) {
        if (error) *error = "Invalid ColorDB name";
        return false;
    }
    const std::string db_name = db.name;
    std::lock_guard<std::mutex> lock(mtx_);
    auto it = sessions_.find(token);
    if (it == sessions_.end()) {
        if (error) *error = "Session not found";
        return false;
    }
    auto& s       = it->second;
    s.last_access = std::chrono::steady_clock::now();
    auto existing = s.color_dbs.find(db_name);
    if (existing == s.color_dbs.end() &&
        static_cast<std::int64_t>(s.color_dbs.size()) >= max_colordbs_) {
        if (error) *error = "Session ColorDB limit reached";
        return false;
    }
    s.color_dbs[db_name] = std::move(db);
    return true;
}

void SessionStore::CleanupExpired() {
    std::lock_guard<std::mutex> lock(mtx_);
    auto now = std::chrono::steady_clock::now();
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        auto elapsed =
            std::chrono::duration_cast<std::chrono::seconds>(now - it->second.last_access).count();
        if (elapsed > ttl_seconds_) {
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

bool SessionStore::IsValidDbName(const std::string& name) {
    if (name.empty() || name.size() > 64) return false;
    static const std::regex kPattern("^[a-zA-Z0-9_]+$");
    return std::regex_match(name, kPattern);
}

std::string SessionStore::NewSecureToken() {
    unsigned char bytes[32] = {};
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

SessionSnapshot& SessionStore::GetOrCreateLocked(const std::string& token) {
    auto& s       = sessions_[token];
    s.token       = token;
    s.last_access = std::chrono::steady_clock::now();
    return s;
}

} // namespace chromaprint3d::backend
