#pragma once

#include "Math/Quat.hpp"

#include <algorithm>
#include <cmath>

// Minimal 3D vector math for the IK solver.
namespace APM::Math
{
struct Vec3
{
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(const Vec3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

inline float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }

inline Vec3 Normalize(const Vec3& a)
{
    const float len = Length(a);
    return len > 1e-8f ? a * (1.f / len) : Vec3{};
}

// Angle between two vectors, in radians (0 if either is degenerate).
inline float AngleBetween(const Vec3& a, const Vec3& b)
{
    const float la = Length(a);
    const float lb = Length(b);
    if (la < 1e-8f || lb < 1e-8f)
    {
        return 0.f;
    }
    return std::acos(std::clamp(Dot(a, b) / (la * lb), -1.f, 1.f));
}

// Rotates v by the unit quaternion q (q * v * q^-1).
inline Vec3 Rotate(const Quat& q, const Vec3& v)
{
    const Vec3 u{q.i, q.j, q.k};
    const Vec3 t = Cross(u, v) * 2.f;
    return v + t * q.r + Cross(u, t);
}

inline Quat FromAxisAngle(const Vec3& aAxis, float aRadians)
{
    const Vec3 axis = Normalize(aAxis);
    return FromAxisAngle(axis.x, axis.y, axis.z, aRadians);
}
} // namespace APM::Math
