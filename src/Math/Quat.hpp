#pragma once

#include <cmath>

// Minimal quaternion math used by the pose override layer.
// Layout matches RED4ext::Quaternion (i, j, k, r) so values can be copied directly.
namespace APM::Math
{
struct Quat
{
    float i = 0.f;
    float j = 0.f;
    float k = 0.f;
    float r = 1.f;
};

inline Quat Mul(const Quat& a, const Quat& b)
{
    return {
        a.r * b.i + a.i * b.r + a.j * b.k - a.k * b.j,
        a.r * b.j - a.i * b.k + a.j * b.r + a.k * b.i,
        a.r * b.k + a.i * b.j - a.j * b.i + a.k * b.r,
        a.r * b.r - a.i * b.i - a.j * b.j - a.k * b.k,
    };
}

inline Quat Normalize(const Quat& q)
{
    const float len = std::sqrt(q.i * q.i + q.j * q.j + q.k * q.k + q.r * q.r);
    if (len < 1e-8f)
    {
        return {};
    }
    const float inv = 1.f / len;
    return {q.i * inv, q.j * inv, q.k * inv, q.r * inv};
}

inline Quat FromAxisAngle(float aAxisX, float aAxisY, float aAxisZ, float aRadians)
{
    const float half = aRadians * 0.5f;
    const float s = std::sin(half);
    return {aAxisX * s, aAxisY * s, aAxisZ * s, std::cos(half)};
}

constexpr float DegToRad(float aDegrees)
{
    return aDegrees * 0.017453292519943295f;
}

// Rotation applied as Z * Y * X around the bone's local axes (degrees).
inline Quat FromEulerXYZDegrees(float aX, float aY, float aZ)
{
    const Quat qx = FromAxisAngle(1.f, 0.f, 0.f, DegToRad(aX));
    const Quat qy = FromAxisAngle(0.f, 1.f, 0.f, DegToRad(aY));
    const Quat qz = FromAxisAngle(0.f, 0.f, 1.f, DegToRad(aZ));
    return Normalize(Mul(Mul(qz, qy), qx));
}
} // namespace APM::Math
