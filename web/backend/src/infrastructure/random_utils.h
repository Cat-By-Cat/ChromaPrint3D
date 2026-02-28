#pragma once

#include <cstddef>
#include <random>
#include <string>
#include <vector>

namespace chromaprint3d::backend::detail {

// Fills buf with n cryptographically suitable random bytes using std::random_device.
// On MSVC this calls BCryptGenRandom; on GCC/Clang it reads /dev/urandom.
// This is the single cross-platform implementation used across all infrastructure files.
inline void PortableRandomBytes(unsigned char* buf, std::size_t n) {
    std::random_device rd;
    for (std::size_t i = 0; i < n;) {
        auto v        = rd();
        const auto* p = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t j = 0; j < sizeof(v) && i < n; ++j, ++i) buf[i] = p[j];
    }
}

inline std::string BytesToHex(const unsigned char* data, std::size_t len) {
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

inline std::string RandomHex(std::size_t byte_count) {
    std::vector<unsigned char> buf(byte_count);
    PortableRandomBytes(buf.data(), byte_count);
    return BytesToHex(buf.data(), byte_count);
}

} // namespace chromaprint3d::backend::detail
