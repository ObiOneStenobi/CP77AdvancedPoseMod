#include "Pose/EyelidFollow.hpp"

#include "Math/Vec3.hpp"

#include <RED4ext/CName.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;

struct LidBone
{
    const char* name;
    uint8_t side;  // 0 left, 1 right
    uint8_t group; // 0 upper lid, 1 lower lid
    float coef[6][3];
};

#include "Pose/EyelidFollowData.inc"

constexpr float PitchMin = -30.f; // how far the game turns the eyes (degrees)
constexpr float PitchMax = 20.8f;
constexpr float YawMax = 40.f;
constexpr float DegToRad = 0.0174532925f;
constexpr float RadToDeg = 57.2957795f;

// Photo Mode facial expression of V (attribute 28). The added characters' expression attribute is not known yet: they
// follow like the neutral expression.
std::atomic<int32_t> g_expression{0};

struct RigBones
{
    uintptr_t rig = 0;
    int32_t eyes[2] = {-1, -1};
    std::vector<int32_t> lids; // index of each LidBones entry in the rig, -1 if missing
};
std::mutex g_rigMutex;
RigBones g_rig;

int32_t IndexOf(const APM::Pose::Target& aTarget, const char* aName)
{
    const uint64_t hash = RED4ext::CName(aName).hash;
    const auto it = std::find(aTarget.boneNames.begin(), aTarget.boneNames.end(), hash);
    return it == aTarget.boneNames.end() ? -1 : static_cast<int32_t>(it - aTarget.boneNames.begin());
}

RigBones Resolve(const APM::Pose::Target& aTarget)
{
    std::scoped_lock lock(g_rigMutex);
    if (g_rig.rig != aTarget.rig)
    {
        g_rig = {};
        g_rig.rig = aTarget.rig;
        g_rig.eyes[0] = IndexOf(aTarget, "l_J_eye_JNT");
        g_rig.eyes[1] = IndexOf(aTarget, "r_J_eye_JNT");
        for (const auto& bone : LidBones)
        {
            g_rig.lids.push_back(IndexOf(aTarget, bone.name));
        }
    }
    return g_rig;
}

Quat Rotation(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

// Turn of an eye from its reference pose in the parent's space, as the measurements: x = sideways, z = up / down.
Vec3 TurnDegrees(const Quat& aCurrent, const Quat& aReference)
{
    Quat d = APM::Math::Mul(APM::Math::Normalize(aCurrent), APM::Math::Conjugate(APM::Math::Normalize(aReference)));
    if (d.r < 0.f)
    {
        d = {-d.i, -d.j, -d.k, -d.r};
    }
    const float s = std::sqrt(d.i * d.i + d.j * d.j + d.k * d.k);
    if (s < 1e-6f)
    {
        return {};
    }
    const float angle = 2.f * std::atan2(s, d.r) * RadToDeg;
    return {d.i / s * angle, d.j / s * angle, d.k / s * angle};
}

struct Gaze
{
    float down = 0.f; // min(pitch, 0)
    float up = 0.f;   // max(pitch, 0)
    float yaw = 0.f;
};

Gaze ToGaze(const Vec3& aTurn)
{
    const float pitch = std::clamp(aTurn.z, PitchMin, PitchMax);
    return {std::min(pitch, 0.f), std::max(pitch, 0.f), std::clamp(aTurn.x, -YawMax, YawMax)};
}
} // namespace

namespace APM::Pose::EyelidFollow
{
void SetExpression(int32_t aExpression)
{
    g_expression = aExpression;
}

void ResetSession()
{
    g_expression = 0;
}

bool ReadEyes(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount,
              std::array<Math::Quat, 2>& aOut)
{
    const RigBones rig = Resolve(aTarget);
    for (int e = 0; e < 2; ++e)
    {
        if (rig.eyes[e] < 0 || static_cast<uint32_t>(rig.eyes[e]) >= aCount)
        {
            return false;
        }
        aOut[e] = Rotation(aBones[rig.eyes[e]]);
    }
    return true;
}

void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
           const std::array<Math::Quat, 2>& aGameEyes)
{
    if (aTarget.refRotationsLS.size() != aCount)
    {
        return;
    }
    const RigBones rig = Resolve(aTarget);
    std::array<Gaze, 2> game{};
    std::array<Gaze, 2> ours{};
    bool changed = false;
    for (int e = 0; e < 2; ++e)
    {
        const int32_t eye = rig.eyes[e];
        if (eye < 0 || static_cast<uint32_t>(eye) >= aCount)
        {
            return;
        }
        const Quat& reference = aTarget.refRotationsLS[eye];
        game[e] = ToGaze(TurnDegrees(aGameEyes[e], reference));
        ours[e] = ToGaze(TurnDegrees(Rotation(aBones[eye]), reference));
        changed |= std::fabs(ours[e].down - game[e].down) + std::fabs(ours[e].up - game[e].up) +
                       std::fabs(ours[e].yaw - game[e].yaw) > 0.1f;
    }
    if (!changed)
    {
        return;
    }

    const int32_t expression = aTarget.photoModeSlot == 0 ? g_expression.load(std::memory_order_relaxed) : 0;
    const bool known = expression >= 0 && expression < static_cast<int32_t>(std::size(ExpressionFactors));
    for (size_t b = 0; b < std::size(LidBones); ++b)
    {
        const int32_t index = b < rig.lids.size() ? rig.lids[b] : -1;
        if (index < 0 || static_cast<uint32_t>(index) >= aCount)
        {
            continue;
        }
        const LidBone& bone = LidBones[b];
        const float shareDown = known ? ExpressionFactors[expression][bone.side][bone.group][0] : 1.f;
        const float shareUp = known ? ExpressionFactors[expression][bone.side][bone.group][1] : 1.f;
        const float shareYaw = 0.5f * (shareDown + shareUp);
        const Gaze& from = game[bone.side];
        const Gaze& to = ours[bone.side];
        float change[6];
        for (int c = 0; c < 6; ++c)
        {
            change[c] = shareDown * bone.coef[c][0] * (to.down - from.down) +
                        shareUp * bone.coef[c][1] * (to.up - from.up) + shareYaw * bone.coef[c][2] * (to.yaw - from.yaw);
        }
        auto& transform = aBones[index];
        // The turn was measured as current * inverse(reference): it applies on the parent's side.
        const Vec3 turn{change[0], change[1], change[2]};
        const float angle = std::sqrt(turn.x * turn.x + turn.y * turn.y + turn.z * turn.z);
        if (angle > 1e-3f)
        {
            const Quat delta = APM::Math::FromAxisAngle(turn, angle * DegToRad);
            const Quat rotated = APM::Math::Normalize(APM::Math::Mul(delta, Rotation(transform)));
            transform.Rotation.i = rotated.i;
            transform.Rotation.j = rotated.j;
            transform.Rotation.k = rotated.k;
            transform.Rotation.r = rotated.r;
        }
        transform.Translation.X += change[3] * 0.01f;
        transform.Translation.Y += change[4] * 0.01f;
        transform.Translation.Z += change[5] * 0.01f;
    }
}
} // namespace APM::Pose::EyelidFollow
