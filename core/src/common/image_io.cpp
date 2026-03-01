#include "chromaprint3d/image_io.h"

#include "detail/cv_utils.h"
#include "detail/icc_utils.h"

#include <opencv2/core.hpp>

namespace ChromaPrint3D {

cv::Mat DecodeImageWithIcc(const uint8_t* data, size_t size) {
    return detail::LoadImageIcc(data, size);
}

cv::Mat DecodeImageWithIcc(const std::vector<uint8_t>& buffer) {
    return DecodeImageWithIcc(buffer.data(), buffer.size());
}

cv::Mat LoadImageWithIcc(const std::string& path) { return detail::LoadImageIcc(path); }

cv::Mat DecodeImageWithIccBgr(const uint8_t* data, size_t size) {
    return detail::EnsureBgr(DecodeImageWithIcc(data, size));
}

cv::Mat DecodeImageWithIccBgr(const std::vector<uint8_t>& buffer) {
    return DecodeImageWithIccBgr(buffer.data(), buffer.size());
}

cv::Mat LoadImageWithIccBgr(const std::string& path) {
    return detail::EnsureBgr(LoadImageWithIcc(path));
}

} // namespace ChromaPrint3D
