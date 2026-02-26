#pragma once

/// \file math_utils.h
/// \brief Shared math utilities for the imgproc module.

namespace ChromaPrint3D::detail {

/// Non-negative modulo: result is always in [0, n).
inline int Mod(int a, int n) {
    int r = a % n;
    return r < 0 ? r + n : r;
}

/// Cyclic difference: j - i (mod n), always in [0, n).
inline int CyclicDiff(int i, int j, int n) { return Mod(j - i, n); }

} // namespace ChromaPrint3D::detail
