#pragma once

#include "Math/Vec3.hpp"

// Analytic two-bone IK (based on Daniel Holden's "Simple Two Joint IK").
// Works on one chain A (upper arm / thigh) -> B (forearm / shin) -> C (hand / foot).
namespace APM::Pose
{
struct TwoBoneIkInput
{
    Math::Vec3 a, b, c;     // model-space joint positions
    Math::Quat aGlobal;     // model-space rotation of A
    Math::Quat bGlobal;     // model-space rotation of B
    Math::Quat aLocal;      // local rotation of A
    Math::Quat bLocal;      // local rotation of B
    Math::Vec3 target;      // desired model-space position of C
    float swivelRadians = 0.f; // extra rotation of the chain around the A->target axis
    Math::Vec3 poleHint{};     // model-space direction the knee / elbow should point to when the limb is (nearly) straight
};

struct TwoBoneIkOutput
{
    Math::Quat aLocal;
    Math::Quat bLocal;
    Math::Quat aGlobal; // resulting model-space rotations (useful to fix up C)
    Math::Quat bGlobal;
};

inline TwoBoneIkOutput SolveTwoBoneIk(const TwoBoneIkInput& in)
{
    using namespace Math;
    constexpr float eps = 1e-4f;

    const float lab = Length(in.b - in.a);
    const float lcb = Length(in.b - in.c);
    const float lat = std::clamp(Length(in.target - in.a), eps, lab + lcb - eps);

    // 1) Bend B so that |A->C| == |A->target| (law of cosines), staying in the current bend plane.
    const float baBc0 = AngleBetween(in.a - in.b, in.c - in.b);
    const float baBc1 = std::acos(std::clamp((lat * lat - lab * lab - lcb * lcb) / (-2.f * lab * lcb), -1.f, 1.f));

    Vec3 bendAxis = Normalize(Cross(in.c - in.a, in.b - in.a));
    // A (nearly) straight limb has no reliable bend plane: a knee a hair past straight would bend backwards. Bend it
    // the natural way instead.
    constexpr float NearlyStraight = 0.26f; // ~15 degrees from straight
    if (Length(in.poleHint) > 0.5f && 3.14159265f - baBc0 < NearlyStraight)
    {
        const Vec3 hinted = Normalize(Cross(in.c - in.a, in.poleHint));
        if (Length(hinted) > 0.5f)
        {
            bendAxis = hinted;
        }
    }
    if (Length(bendAxis) < 0.5f)
    {
        // Perfectly straight limb: pick a stable axis from A's orientation.
        bendAxis = Normalize(Cross(in.c - in.a, Rotate(in.aGlobal, Vec3{0.f, 0.f, 1.f})));
    }

    const Quat bend = FromAxisAngle(bendAxis, (baBc1 - baBc0));
    Quat aGlobal = in.aGlobal;
    Quat bGlobal = Normalize(Mul(bend, in.bGlobal));
    Vec3 c = in.b + Rotate(bend, in.c - in.b);

    // 2) Swing A so that A->C points at the target.
    const Vec3 ac = c - in.a;
    const Vec3 at = in.target - in.a;
    const Vec3 swingAxis = Cross(ac, at);
    if (Length(swingAxis) > 1e-8f)
    {
        const Quat swing = FromAxisAngle(swingAxis, AngleBetween(ac, at));
        aGlobal = Normalize(Mul(swing, aGlobal));
        bGlobal = Normalize(Mul(swing, bGlobal));
    }

    // 3) Optional swivel of the whole chain around A->target (moves the elbow/knee, keeps C on target).
    if (std::abs(in.swivelRadians) > 1e-6f && Length(at) > 1e-6f)
    {
        const Quat swivel = FromAxisAngle(at, in.swivelRadians);
        aGlobal = Normalize(Mul(swivel, aGlobal));
        bGlobal = Normalize(Mul(swivel, bGlobal));
    }

    // Back to local space. aGlobal = parent * aLocal  =>  parent = aGlobal * aLocal^-1
    const Quat parent = Mul(in.aGlobal, Conjugate(in.aLocal));

    TwoBoneIkOutput out;
    out.aGlobal = aGlobal;
    out.bGlobal = bGlobal;
    out.aLocal = Normalize(Mul(Conjugate(parent), aGlobal));
    out.bLocal = Normalize(Mul(Conjugate(aGlobal), bGlobal));
    return out;
}
} // namespace APM::Pose
