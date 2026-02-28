#pragma once

typedef unsigned char uuid_t[16];

#ifdef __cplusplus
#    include <fstream>
#    include <random>

extern "C" inline void uuid_generate(uuid_t out) {
    std::ifstream urandom("/dev/urandom", std::ios::in | std::ios::binary);
    if (urandom.good()) {
        urandom.read(reinterpret_cast<char*>(out), 16);
        if (urandom.gcount() == 16) return;
    }

    std::random_device rd;
    for (int i = 0; i < 16; ++i) { out[i] = static_cast<unsigned char>(rd() & 0xFF); }
}

#else
void uuid_generate(uuid_t out);
#endif
