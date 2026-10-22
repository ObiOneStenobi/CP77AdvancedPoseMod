#include "Pose/JointLimits.hpp"

#include "Math/Vec3.hpp"
#include "Pose/TargetRegistry.hpp"

#include <RED4ext/CName.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using namespace APM;

std::atomic<bool> g_enabled{true};

struct JointRange
{
    float forwardMin;
    float forwardMax;
    float side;
    float twist;
};

// Bone name without its side ("LeftForeArm" -> "ForeArm").
std::string_view Unsided(std::string_view aBone)
{
    if (aBone.rfind("Left", 0) == 0)
    {
        aBone.remove_prefix(4);
    }
    else if (aBone.rfind("Right", 0) == 0)
    {
        aBone.remove_prefix(5);
    }
    return aBone;
}

bool IsFingerBone(std::string_view aBone)
{
    const auto name = Unsided(aBone);
    return (name.rfind("Hand", 0) == 0 && name.size() > 4) || name.rfind("InHand", 0) == 0;
}

bool FindRange(std::string_view aBone, JointRange& aOut)
{
    const auto name = Unsided(aBone);
    struct Entry
    {
        std::string_view name;
        JointRange range;
    };
    static constexpr Entry Table[] = {
        {"Spine", {-30.f, 40.f, 30.f, 35.f}},   {"Spine1", {-30.f, 40.f, 30.f, 35.f}},
        {"Spine2", {-30.f, 40.f, 30.f, 35.f}},  {"Spine3", {-30.f, 40.f, 30.f, 35.f}},
        {"Neck", {-35.f, 40.f, 30.f, 45.f}},    {"Neck1", {-35.f, 40.f, 30.f, 45.f}},
        {"Head", {-40.f, 45.f, 35.f, 70.f}},    {"Shoulder", {-20.f, 20.f, 20.f, 15.f}},
        {"Arm", {-70.f, 130.f, 100.f, 90.f}},   {"ForeArm", {-5.f, 150.f, 15.f, 90.f}},
        {"Hand", {-80.f, 80.f, 40.f, 30.f}},    {"UpLeg", {-35.f, 125.f, 55.f, 45.f}},
        {"Leg", {-150.f, 5.f, 8.f, 30.f}},      {"Foot", {-50.f, 45.f, 30.f, 25.f}},
        {"ToeBase", {-40.f, 60.f, 10.f, 10.f}},   {"Heel", {-30.f, 30.f, 15.f, 15.f}},
        // Palm (metacarpals): small movements; the thumb's base is much freer.
        {"InHandIndex", {-15.f, 25.f, 10.f, 10.f}}, {"InHandMiddle", {-15.f, 25.f, 10.f, 10.f}},
        {"InHandRing", {-15.f, 25.f, 10.f, 10.f}},  {"InHandPinky", {-15.f, 30.f, 12.f, 12.f}},
        {"InHandThumb", {-40.f, 60.f, 45.f, 40.f}},
    };
    for (const auto& entry : Table)
    {
        if (name == entry.name)
        {
            aOut = entry.range;
            return true;
        }
    }
    // Fingers: curl towards the palm, little bending back; only the knuckle spreads.
    if (name.rfind("HandThumb", 0) == 0)
    {
        aOut = {-30.f, 80.f, 40.f, 30.f};
        return true;
    }
    if (name.rfind("HandIndex", 0) == 0 || name.rfind("HandMiddle", 0) == 0 || name.rfind("HandRing", 0) == 0 ||
        name.rfind("HandPinky", 0) == 0)
    {
        const bool knuckle = !name.empty() && name.back() == '1';
        aOut = {-25.f, 100.f, knuckle ? 20.f : 5.f, 10.f};
        return true;
    }
    return false;
}

// Euler decomposition q = Rx(a) * Ry(b) * Rz(c) for a right-handed orthonormal basis (x, y, z); degrees.
std::array<float, 3> DecomposeXYZ(const Math::Quat& q, const Math::Vec3& x, const Math::Vec3& y, const Math::Vec3& z)
{
    const Math::Vec3 rx = Math::Rotate(q, x);
    const Math::Vec3 ry = Math::Rotate(q, y);
    const Math::Vec3 rz = Math::Rotate(q, z);
    const float m00 = Math::Dot(x, rx);
    const float m01 = Math::Dot(x, ry);
    const float m02 = Math::Dot(x, rz);
    const float m12 = Math::Dot(y, rz);
    const float m22 = Math::Dot(z, rz);
    constexpr float RadToDeg = 57.29577951308232f;
    return {std::atan2(-m12, m22) * RadToDeg, std::asin(std::clamp(m02, -1.f, 1.f)) * RadToDeg,
            std::atan2(-m01, m00) * RadToDeg};
}

struct Axes
{
    Math::Vec3 bend1;
    Math::Vec3 bend2;
    Math::Vec3 twist;
};

bool GetAxes(uint64_t aHash, bool aSwapBends, Axes& aOut)
{
    if (!Pose::TargetRegistry::GetBoneAxes(aHash, aOut.bend1, aOut.bend2, aOut.twist))
    {
        return false;
    }
    if (aSwapBends)
    {
        std::swap(aOut.bend1, aOut.bend2);
    }
    return true;
}

std::array<float, 3> ToAngles(const Axes& aAxes, const Math::Quat& q)
{
    if (Math::Dot(Math::Cross(aAxes.bend1, aAxes.bend2), aAxes.twist) >= 0.f)
    {
        return DecomposeXYZ(q, aAxes.bend1, aAxes.bend2, aAxes.twist);
    }
    const auto angles = DecomposeXYZ(q, aAxes.bend1, aAxes.bend2, aAxes.twist * -1.f);
    return {angles[0], angles[1], -angles[2]};
}

Math::Quat FromAngles(const Axes& aAxes, const std::array<float, 3>& aAngles)
{
    const auto q1 = Math::FromAxisAngle(aAxes.bend1, Math::DegToRad(aAngles[0]));
    const auto q2 = Math::FromAxisAngle(aAxes.bend2, Math::DegToRad(aAngles[1]));
    const auto q3 = Math::FromAxisAngle(aAxes.twist, Math::DegToRad(aAngles[2]));
    return Math::Normalize(Math::Mul(Math::Mul(q1, q2), q3));
}

struct JointFrame
{
    Math::Quat referenceLocal{};
    float forwardSign = 1.f; // +1 when a positive bend1 moves the bone's end forward (fingers: towards the palm)
};

std::mutex g_frameMutex;

// Reference local rotation and bend direction of a bone, worked out once from the reference pose.
bool GetJointFrame(std::string_view aBone, uint64_t aHash, bool aSwapBends, JointFrame& aOut)
{
    std::scoped_lock lock(g_frameMutex);
    static std::unordered_map<uint64_t, JointFrame> cache[2];
    auto& frames = cache[aSwapBends ? 1 : 0];
    if (const auto it = frames.find(aHash); it != frames.end())
    {
        aOut = it->second;
        return true;
    }
    Pose::TargetRegistry::ReferencePose reference;
    if (!Pose::TargetRegistry::GetReferencePose(reference))
    {
        return false;
    }
    const size_t count = reference.boneNames.size();
    std::vector<Math::Vec3> positions(count);
    for (size_t i = 0; i < count; ++i)
    {
        const int16_t parent = reference.parents[i];
        positions[i] = parent >= 0 ? positions[parent] + Math::Rotate(reference.modelRotations[parent], reference.localTranslations[i])
                                   : reference.localTranslations[i];
    }
    const auto indexOf = [&](uint64_t aName) -> int32_t
    {
        const auto it = std::find(reference.boneNames.begin(), reference.boneNames.end(), aName);
        return it == reference.boneNames.end() ? -1 : static_cast<int32_t>(std::distance(reference.boneNames.begin(), it));
    };
    const int32_t index = indexOf(aHash);
    if (index < 0)
    {
        return false;
    }
    JointFrame frame;
    frame.referenceLocal = reference.localRotations[index];

    // Which way "forward" is: model +Y for the body; for fingers and palm, towards the palm.
    Math::Vec3 forward{0.f, 1.f, 0.f};
    if (IsFingerBone(aBone))
    {
        const bool left = aBone.rfind("Left", 0) == 0;
        const int32_t hand = indexOf(RED4ext::CName(left ? "LeftHand" : "RightHand").hash);
        const int32_t thumb = indexOf(RED4ext::CName(left ? "LeftHandThumb1" : "RightHandThumb1").hash);
        if (hand >= 0 && thumb >= 0)
        {
            // T pose with the palms down: the thumb points forward; palms forward: the thumb points up.
            const Math::Vec3 t = positions[thumb] - positions[hand];
            if (std::abs(t.y) >= std::abs(t.z))
            {
                forward = Math::Vec3{0.f, 0.f, t.y >= 0.f ? -1.f : 1.f};
            }
            else
            {
                forward = Math::Vec3{0.f, t.z >= 0.f ? 1.f : -1.f, 0.f};
            }
        }
    }

    Math::Vec3 tip{};
    int32_t children = 0;
    for (size_t i = static_cast<size_t>(index) + 1; i < count; ++i)
    {
        if (reference.parents[i] == static_cast<int16_t>(index) && Math::Length(positions[i] - positions[index]) > 0.005f)
        {
            tip = tip + positions[i];
            ++children;
        }
    }
    Math::Vec3 direction{};
    if (children > 0)
    {
        direction = tip * (1.f / static_cast<float>(children)) - positions[index];
    }
    else if (reference.parents[index] >= 0)
    {
        direction = positions[index] - positions[reference.parents[index]]; // leaf: continue the parent's direction
    }
    Axes axes;
    if (Math::Length(direction) > 1e-4f && GetAxes(aHash, aSwapBends, axes))
    {
        const Math::Vec3 axis = Math::Rotate(reference.modelRotations[index], axes.bend1);
        frame.forwardSign = Math::Dot(Math::Cross(axis, direction), forward) >= 0.f ? 1.f : -1.f;
    }
    frames[aHash] = frame;
    aOut = frame;
    return true;
}
} // namespace

namespace APM::Pose::JointLimits
{
void SetEnabled(bool aEnabled)
{
    g_enabled = aEnabled;
}

bool IsEnabled()
{
    return g_enabled;
}

bool GetReferenceFrame(std::string_view aBone, bool aSwapBends, Math::Quat& aReferenceLocal, float& aForwardSign)
{
    JointFrame frame;
    if (!GetJointFrame(aBone, RED4ext::CName(std::string(aBone).c_str()).hash, aSwapBends, frame))
    {
        return false;
    }
    aReferenceLocal = frame.referenceLocal;
    aForwardSign = frame.forwardSign;
    return true;
}

bool HasRange(std::string_view aBone)
{
    JointRange range{};
    return g_enabled && FindRange(aBone, range);
}

Math::Quat LimitLocal(std::string_view aBone, const Math::Quat& aLocal, const Math::Quat& aAnimatedLocal, bool aSwapBends)
{
    JointRange range{};
    if (!g_enabled || !FindRange(aBone, range))
    {
        return aLocal;
    }
    const uint64_t hash = RED4ext::CName(std::string(aBone).c_str()).hash;
    JointFrame frame;
    Axes axes;
    if (!GetJointFrame(aBone, hash, aSwapBends, frame) || !GetAxes(hash, aSwapBends, axes))
    {
        return aLocal;
    }
    const Math::Quat toReference = Math::Conjugate(frame.referenceLocal);
    const auto wanted = ToAngles(axes, Math::Mul(toReference, aLocal));
    const auto animation = ToAngles(axes, Math::Mul(toReference, aAnimatedLocal));
    const float animForward = animation[0] * frame.forwardSign;
    const float forwardMin = std::min(range.forwardMin, animForward);
    const float forwardMax = std::max(range.forwardMax, animForward);
    const float side = std::max(range.side, std::abs(animation[1]));
    const float twist = std::max(range.twist, std::abs(animation[2]));
    std::array<float, 3> limited = wanted;
    limited[0] = std::clamp(wanted[0] * frame.forwardSign, forwardMin, forwardMax) * frame.forwardSign;
    limited[1] = std::clamp(wanted[1], -side, side);
    limited[2] = std::clamp(wanted[2], -twist, twist);
    if (limited == wanted)
    {
        return aLocal;
    }
    return Math::Normalize(Math::Mul(frame.referenceLocal, FromAngles(axes, limited)));
}
} // namespace APM::Pose::JointLimits
