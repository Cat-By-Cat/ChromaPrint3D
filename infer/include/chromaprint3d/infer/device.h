#pragma once

/// \file device.h
/// \brief Device abstraction for inference execution.

#include <cstdint>
#include <string>

namespace ChromaPrint3D::infer {

enum class DeviceType : uint8_t {
    kCPU  = 0,
    kCUDA = 1,
};

struct Device {
    DeviceType type = DeviceType::kCPU;
    int index       = 0;

    static Device CPU();
    static Device CUDA(int index = 0);

    bool operator==(const Device& o) const;
    bool operator!=(const Device& o) const;
    std::string ToString() const;
};

} // namespace ChromaPrint3D::infer
