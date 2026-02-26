#pragma once

/// \file vec2.h
/// \brief 2D floating-point vector.

namespace ChromaPrint3D {

struct Vec2f {
    float x = 0.0f;
    float y = 0.0f;

    Vec2f() = default;

    constexpr Vec2f(float x, float y) : x(x), y(y) {}

    Vec2f operator+(const Vec2f& o) const { return {x + o.x, y + o.y}; }

    Vec2f operator-(const Vec2f& o) const { return {x - o.x, y - o.y}; }

    Vec2f operator*(float s) const { return {x * s, y * s}; }

    bool operator==(const Vec2f& o) const { return x == o.x && y == o.y; }

    bool operator!=(const Vec2f& o) const { return !(*this == o); }
};

} // namespace ChromaPrint3D
