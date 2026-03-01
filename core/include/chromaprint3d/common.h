/// \file common.h
/// \brief Common enumerations and types used throughout ChromaPrint3D.

#pragma once

#include <cstdint>
#include <string>

// Compiler detection
#if defined(_MSC_VER)
#    define CHROMAPRINT3D_COMPILER_MSVC 1
#else
#    define CHROMAPRINT3D_COMPILER_MSVC 0
#endif

#if defined(__clang__)
#    define CHROMAPRINT3D_COMPILER_CLANG 1
#else
#    define CHROMAPRINT3D_COMPILER_CLANG 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#    define CHROMAPRINT3D_COMPILER_GCC 1
#else
#    define CHROMAPRINT3D_COMPILER_GCC 0
#endif

// Platform detection
#if defined(_WIN32)
#    define CHROMAPRINT3D_PLATFORM_WINDOWS 1
#    define CHROMAPRINT3D_PLATFORM_POSIX   0
#else
#    define CHROMAPRINT3D_PLATFORM_WINDOWS 0
#    define CHROMAPRINT3D_PLATFORM_POSIX   1
#endif

// Loop unroll hint (MSVC treats unknown pragma as warning/error under /WX).
#if CHROMAPRINT3D_COMPILER_CLANG || CHROMAPRINT3D_COMPILER_GCC
#    define CHROMAPRINT3D_PRAGMA_UNROLL _Pragma("unroll")
#else
#    define CHROMAPRINT3D_PRAGMA_UNROLL
#endif

namespace ChromaPrint3D {

/// Image resizing algorithm.
enum class ResizeMethod : uint8_t {
    Nearest = 0, ///< Nearest neighbor interpolation (fastest, lowest quality).
    Area    = 1, ///< Area-based resampling (good for downscaling).
    Linear  = 2, ///< Bilinear interpolation (balanced quality/speed).
    Cubic   = 3, ///< Bicubic interpolation (highest quality, slower).
};

/// Image denoising algorithm.
enum class DenoiseMethod : uint8_t {
    None      = 0, ///< No denoising applied.
    Bilateral = 1, ///< Bilateral filter (preserves edges).
    Median    = 2, ///< Median filter (removes salt-and-pepper noise).
};

/// Layer printing order for multi-layer color printing.
enum class LayerOrder : uint8_t {
    Top2Bottom = 0, ///< Print from top layer to bottom layer.
    Bottom2Top = 1, ///< Print from bottom layer to top layer.
};

/// Convert LayerOrder to its string representation.
std::string ToLayerOrderString(LayerOrder order);

/// Parse LayerOrder from a string ("Top2Bottom" / "Bottom2Top").
LayerOrder FromLayerOrderString(const std::string& str);

/// Color space used for color matching.
enum class ColorSpace : uint8_t {
    Lab = 0, ///< CIE L*a*b* color space (perceptually uniform).
    Rgb = 1, ///< RGB color space (linear sRGB).
};

/// Dithering method for color matching.
enum class DitherMethod : uint8_t {
    None           = 0, ///< No dithering (default, existing behavior).
    BlueNoise      = 1, ///< Blue noise ordered dither (parallelizable).
    FloydSteinberg = 2, ///< Floyd-Steinberg error diffusion (serial, higher quality).
};

} // namespace ChromaPrint3D