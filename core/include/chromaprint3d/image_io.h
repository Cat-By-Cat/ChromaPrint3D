#pragma once

/// \file image_io.h
/// \brief ICC-aware image loading helpers for public callers.

#include "export.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cv {
class Mat;
}

namespace ChromaPrint3D {

/// Decode image bytes with ICC-aware color handling.
/// Returns the decoded image in original channel layout (or converted from ICC profile when
/// needed).
CHROMAPRINT3D_API cv::Mat DecodeImageWithIcc(const uint8_t* data, size_t size);

/// Decode image bytes with ICC-aware color handling.
CHROMAPRINT3D_API cv::Mat DecodeImageWithIcc(const std::vector<uint8_t>& buffer);

/// Load image from file path with ICC-aware color handling.
CHROMAPRINT3D_API cv::Mat LoadImageWithIcc(const std::string& path);

/// Decode image bytes with ICC-aware color handling and normalize to BGR CV_8UC3.
CHROMAPRINT3D_API cv::Mat DecodeImageWithIccBgr(const uint8_t* data, size_t size);

/// Decode image bytes with ICC-aware color handling and normalize to BGR CV_8UC3.
CHROMAPRINT3D_API cv::Mat DecodeImageWithIccBgr(const std::vector<uint8_t>& buffer);

/// Load image from file path with ICC-aware color handling and normalize to BGR CV_8UC3.
CHROMAPRINT3D_API cv::Mat LoadImageWithIccBgr(const std::string& path);

} // namespace ChromaPrint3D
