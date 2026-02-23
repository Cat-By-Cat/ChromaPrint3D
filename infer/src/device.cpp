#include "chromaprint3d/infer/device.h"

namespace ChromaPrint3D::infer {

Device Device::CPU() { return {DeviceType::kCPU, 0}; }

Device Device::CUDA(int index) { return {DeviceType::kCUDA, index}; }

bool Device::operator==(const Device& o) const { return type == o.type && index == o.index; }

bool Device::operator!=(const Device& o) const { return !(*this == o); }

std::string Device::ToString() const {
    switch (type) {
    case DeviceType::kCPU:
        return "cpu";
    case DeviceType::kCUDA:
        return "cuda:" + std::to_string(index);
    }
    return "unknown";
}

} // namespace ChromaPrint3D::infer
