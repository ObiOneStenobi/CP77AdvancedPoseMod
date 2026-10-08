#include "Pose/PoseProcessor.hpp"

#include "Math/Vec3.hpp"
#include "Pose/TwoBoneIk.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <mutex>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;
using APM::Pose::IkChain;
using APM::Pose::IkChainCount;

constexpr const char* MainSkeletonName = "root";

struct ChainBones
{
    const char* upper;
    const char* middle;
    const char* end;
};

constexpr ChainBones ChainDefinitions[IkChainCount] = {
    {"LeftArm", "LeftForeArm", "LeftHand"},
    {"RightArm", "RightForeArm", "RightHand"},
    {"LeftUpLeg", "LeftLeg", "LeftFoot"},
    {"RightUpLeg", "RightLeg", "RightFoot"},
};

// ---------------------------------------------------------------------------------------------------------------------
// IK result shared between the skeletons of the same character (main skeleton writes, others read).
// ---------------------------------------------------------------------------------------------------------------------
struct IkDelta
{
    uint64_t bone;
    Quat delta; // newLocal = oldLocal * delta
};

std::mutex g_ikMutex;
uintptr_t g_ikEntity = 0;
std::vector<IkDelta> g_ikDeltas;

void PublishIkDeltas(uintptr_t aEntity, const std::vector<IkDelta>& aDeltas)
{
    std::scoped_lock lock(g_ikMutex);
    g_ikEntity = aEntity;
    g_ikDeltas = aDeltas;
}

void ReadIkDeltas(uintptr_t aEntity, std::vector<IkDelta>& aOut)
{
    std::scoped_lock lock(g_ikMutex);
    if (g_ikEntity == aEntity)
    {
        aOut = g_ikDeltas;
    }
    else
    {
        aOut.clear();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------------------------------
Quat GetRotation(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

void SetRotation(RED4ext::QsTransform& aTransform, const Quat& aRotation)
{
    const auto q = APM::Math::Normalize(aRotation);
    aTransform.Rotation.i = q.i;
    aTransform.Rotation.j = q.j;
    aTransform.Rotation.k = q.k;
    aTransform.Rotation.r = q.r;
}

int32_t IndexOf(const APM::Pose::Target& aTarget, uint64_t aHash)
{
    const auto it = std::find(aTarget.boneNames.begin(), aTarget.boneNames.end(), aHash);
    return it == aTarget.boneNames.end() ? -1 : static_cast<int32_t>(std::distance(aTarget.boneNames.begin(), it));
}

uint64_t Hash(const char* aName)
{
    return RED4ext::CName(aName).hash;
}

// Model-space pose computed from local transforms (scale is ignored: CDPR rigs use unit scale).
struct ModelPose
{
    std::vector<Vec3> positions;
    std::vector<Quat> rotations;
};

void ComputeModelPose(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount,
                      ModelPose& aOut)
{
    aOut.positions.resize(aCount);
    aOut.rotations.resize(aCount);
    for (uint32_t i = 0; i < aCount; ++i)
    {
        const Vec3 localPos{aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
        const Quat localRot = GetRotation(aBones[i]);
        const int16_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : int16_t(-1);
        if (parent >= 0 && static_cast<uint32_t>(parent) < i)
        {
            aOut.positions[i] = aOut.positions[parent] + APM::Math::Rotate(aOut.rotations[parent], localPos);
            aOut.rotations[i] = APM::Math::Normalize(APM::Math::Mul(aOut.rotations[parent], localRot));
        }
        else
        {
            aOut.positions[i] = localPos;
            aOut.rotations[i] = localRot;
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// FK overrides (rotation / translation per bone, plus linked bones such as the face eyes)
// ---------------------------------------------------------------------------------------------------------------------
void ApplyForwardOverrides(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                           const APM::Pose::OverrideSnapshot& aPose)
{
    for (const auto& bone : aPose.bones)
    {
        if (bone.hasRotation)
        {
            for (const auto& link : aTarget.linkedBones)
            {
                if (link.sourceBone == bone.boneHash && link.targetIndex < aCount)
                {
                    const Quat converted = APM::Math::Mul(APM::Math::Mul(link.pre, bone.rotation), link.post);
                    SetRotation(aBones[link.targetIndex],
                                APM::Math::Mul(GetRotation(aBones[link.targetIndex]), converted));
                }
            }
        }

        const int32_t index = IndexOf(aTarget, bone.boneHash);
        if (index < 0 || static_cast<uint32_t>(index) >= aCount)
        {
            continue;
        }
        auto& transform = aBones[index];
        if (bone.hasRotation)
        {
            SetRotation(transform, APM::Math::Mul(GetRotation(transform), bone.rotation));
        }
        if (bone.hasTranslation)
        {
            transform.Translation.X += bone.translation[0];
            transform.Translation.Y += bone.translation[1];
            transform.Translation.Z += bone.translation[2];
        }
    }
}

bool AnyIkEnabled(const APM::Pose::OverrideSnapshot& aPose)
{
    return std::any_of(aPose.ik.begin(), aPose.ik.end(), [](const auto& c) { return c.enabled; });
}

// ---------------------------------------------------------------------------------------------------------------------
// IK on one skeleton. Every skeleton that has the three bones of a chain solves it on its own pose: skeletons of the
// same character do not always start from the same pose (the game adjusts the rendered one, e.g. a hand holding a
// weapon), so copying the main skeleton's result would put the limb somewhere else.
// Returns a bit mask of the chains solved; their local changes are appended to aDeltas.
// ---------------------------------------------------------------------------------------------------------------------
uint32_t SolveIkChains(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                       const APM::Pose::OverrideSnapshot& aPose, const ModelPose& aOriginalPose, ModelPose& aCurrentPose,
                       std::vector<IkDelta>& aDeltas)
{
    uint32_t solved = 0;
    for (size_t chainIndex = 0; chainIndex < IkChainCount; ++chainIndex)
    {
        const auto& settings = aPose.ik[chainIndex];
        if (!settings.enabled)
        {
            continue;
        }

        const auto& chain = ChainDefinitions[chainIndex];
        const int32_t a = IndexOf(aTarget, Hash(chain.upper));
        const int32_t b = IndexOf(aTarget, Hash(chain.middle));
        const int32_t c = IndexOf(aTarget, Hash(chain.end));
        if (a < 0 || b < 0 || c < 0 || static_cast<uint32_t>(a) >= aCount || static_cast<uint32_t>(b) >= aCount ||
            static_cast<uint32_t>(c) >= aCount)
        {
            continue;
        }

        const Vec3 offset{settings.offset[0], settings.offset[1], settings.offset[2]};

        APM::Pose::TwoBoneIkInput input;
        input.a = aCurrentPose.positions[a];
        input.b = aCurrentPose.positions[b];
        input.c = aCurrentPose.positions[c];
        input.aGlobal = aCurrentPose.rotations[a];
        input.bGlobal = aCurrentPose.rotations[b];
        input.aLocal = GetRotation(aBones[a]);
        input.bLocal = GetRotation(aBones[b]);
        input.target = aOriginalPose.positions[c] + offset;
        input.swivelRadians = APM::Math::DegToRad(settings.swivel);

        const auto result = APM::Pose::SolveTwoBoneIk(input);

        // The hand/foot keeps its current world orientation instead of following the new forearm/shin.
        const Quat cLocalOld = GetRotation(aBones[c]);
        const Quat cLocalNew =
            APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(result.bGlobal), aCurrentPose.rotations[c]));

        const Quat aLocalOld = input.aLocal;
        const Quat bLocalOld = input.bLocal;
        SetRotation(aBones[a], result.aLocal);
        SetRotation(aBones[b], result.bLocal);
        SetRotation(aBones[c], cLocalNew);

        aDeltas.push_back({aTarget.boneNames[a], APM::Math::Mul(APM::Math::Conjugate(aLocalOld), result.aLocal)});
        aDeltas.push_back({aTarget.boneNames[b], APM::Math::Mul(APM::Math::Conjugate(bLocalOld), result.bLocal)});
        aDeltas.push_back({aTarget.boneNames[c], APM::Math::Mul(APM::Math::Conjugate(cLocalOld), cLocalNew)});

        // Keep the cached model pose valid for chains processed later (they never share bones).
        aCurrentPose.rotations[a] = result.aGlobal;
        aCurrentPose.rotations[b] = result.bGlobal;
        solved |= 1u << chainIndex;
    }
    return solved;
}

bool BelongsToChain(uint64_t aBone, size_t aChain)
{
    const auto& chain = ChainDefinitions[aChain];
    return aBone == Hash(chain.upper) || aBone == Hash(chain.middle) || aBone == Hash(chain.end);
}

// ---------------------------------------------------------------------------------------------------------------------
// Main skeleton: FK + IK, publishes its IK deltas for skeletons without full chains
// ---------------------------------------------------------------------------------------------------------------------
void ProcessMainSkeleton(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                         const APM::Pose::OverrideSnapshot& aPose)
{
    thread_local ModelPose originalPose;
    thread_local ModelPose currentPose;
    thread_local std::vector<IkDelta> deltas;
    deltas.clear();

    const bool useIk = AnyIkEnabled(aPose);
    if (useIk)
    {
        // Where the animation (before any edit) puts the hands and feet: IK pins them there.
        ComputeModelPose(aTarget, aBones, aCount, originalPose);
    }

    ApplyForwardOverrides(aTarget, aBones, aCount, aPose);

    if (useIk)
    {
        ComputeModelPose(aTarget, aBones, aCount, currentPose);
        SolveIkChains(aTarget, aBones, aCount, aPose, originalPose, currentPose, deltas);
    }

    PublishIkDeltas(aTarget.entity, deltas);
}

// ---------------------------------------------------------------------------------------------------------------------
// Twist helper bones
//
// The "deformations" skeleton has CDPR's twist helpers (l_Wrist_0..2 along the forearm, l_SHL_0..1 along the upper
// arm, l_thigh_0..1, l_calf_0). The game computes them from the animation *before* our hook runs, so a twist we add
// to the hand or the upper arm would otherwise collapse the skin at a single joint ("candy wrapper").
// We spread the twist part of our own change over the helpers, by how far along the bone each one sits.
// ---------------------------------------------------------------------------------------------------------------------
enum class TwistMode
{
    // Helper is a sibling of the driver (both children of the same bone), e.g. forearm helpers driven by the hand:
    // the helper takes `weight` of the driver's added twist.
    FollowChild,
    // Helper is a child of the driver, e.g. upper arm helpers: it keeps only `weight` of the driver's added twist.
    DampenParent,
};

struct TwistHelper
{
    const char* helper;
    const char* driver;
    TwistMode mode;
    float weight;
};

constexpr TwistHelper TwistHelpers[] = {
    {"l_Wrist_0_JNT", "LeftHand", TwistMode::FollowChild, 0.33f},
    {"l_Wrist_1_JNT", "LeftHand", TwistMode::FollowChild, 0.67f},
    {"l_Wrist_2_JNT", "LeftHand", TwistMode::FollowChild, 0.95f},
    {"r_Wrist_0_JNT", "RightHand", TwistMode::FollowChild, 0.33f},
    {"r_Wrist_1_JNT", "RightHand", TwistMode::FollowChild, 0.67f},
    {"r_Wrist_2_JNT", "RightHand", TwistMode::FollowChild, 0.95f},
    {"l_SHL_0_JNT", "LeftArm", TwistMode::DampenParent, 0.33f},
    {"l_SHL_1_JNT", "LeftArm", TwistMode::DampenParent, 0.67f},
    {"r_SHL_0_JNT", "RightArm", TwistMode::DampenParent, 0.33f},
    {"r_SHL_1_JNT", "RightArm", TwistMode::DampenParent, 0.67f},
    {"l_thigh_0_JNT", "LeftUpLeg", TwistMode::DampenParent, 0.33f},
    {"l_thigh_1_JNT", "LeftUpLeg", TwistMode::DampenParent, 0.67f},
    {"r_thigh_0_JNT", "RightUpLeg", TwistMode::DampenParent, 0.33f},
    {"r_thigh_1_JNT", "RightUpLeg", TwistMode::DampenParent, 0.67f},
    {"l_calf_0_JNT", "LeftFoot", TwistMode::FollowChild, 0.5f},
    {"r_calf_0_JNT", "RightFoot", TwistMode::FollowChild, 0.5f},
};

constexpr size_t TwistHelperCount = std::size(TwistHelpers);

// Signed twist angle (radians) of `q` around the unit axis `aAxis` (swing-twist decomposition).
float TwistAngle(const Quat& q, const Vec3& aAxis)
{
    const float projection = q.i * aAxis.x + q.j * aAxis.y + q.k * aAxis.z;
    float angle = 2.f * std::atan2(projection, q.r);
    constexpr float Pi = 3.14159265358979f;
    if (angle > Pi)
    {
        angle -= 2.f * Pi;
    }
    else if (angle < -Pi)
    {
        angle += 2.f * Pi;
    }
    return angle;
}

struct ResolvedTwistHelper
{
    int32_t helper = -1;
    int32_t driver = -1;
    Vec3 axis{}; // twist axis in the helper's parent space
};

// Indices of the helpers present in this skeleton (empty for skeletons without helpers).
void ResolveTwistHelpers(const APM::Pose::Target& aTarget, uint32_t aCount,
                         std::array<ResolvedTwistHelper, TwistHelperCount>& aOut, bool& aAny)
{
    aAny = false;
    for (size_t i = 0; i < TwistHelperCount; ++i)
    {
        auto& resolved = aOut[i];
        resolved = {};
        const int32_t helper = IndexOf(aTarget, Hash(TwistHelpers[i].helper));
        const int32_t driver = IndexOf(aTarget, Hash(TwistHelpers[i].driver));
        if (helper < 0 || driver < 0 || static_cast<uint32_t>(helper) >= aCount ||
            static_cast<uint32_t>(driver) >= aCount || static_cast<size_t>(helper) >= aTarget.refTranslationsLS.size())
        {
            continue;
        }
        // Helpers sit on the bone line, so their offset from the parent is the bone (twist) axis.
        const Vec3 offset = aTarget.refTranslationsLS[helper];
        if (APM::Math::Length(offset) < 1e-4f)
        {
            continue;
        }
        resolved.helper = helper;
        resolved.driver = driver;
        resolved.axis = APM::Math::Normalize(offset);
        aAny = true;
    }
}

void ApplyTwistHelpers(const std::array<ResolvedTwistHelper, TwistHelperCount>& aHelpers,
                       const std::vector<Quat>& aOriginalLocal, RED4ext::QsTransform* aBones)
{
    for (size_t i = 0; i < TwistHelperCount; ++i)
    {
        const auto& resolved = aHelpers[i];
        if (resolved.helper < 0)
        {
            continue;
        }
        const Quat original = aOriginalLocal[resolved.driver];
        const Quat current = GetRotation(aBones[resolved.driver]);

        float factor = 0.f;
        Quat change{};
        if (TwistHelpers[i].mode == TwistMode::FollowChild)
        {
            // Our change of the driver, expressed in the shared parent's space.
            change = APM::Math::Mul(current, APM::Math::Conjugate(original));
            factor = TwistHelpers[i].weight;
        }
        else
        {
            // Our change of the driver, expressed in the driver's own space (= the helper's parent space).
            change = APM::Math::Mul(APM::Math::Conjugate(original), current);
            factor = TwistHelpers[i].weight - 1.f;
        }

        const float angle = TwistAngle(APM::Math::Normalize(change), resolved.axis) * factor;
        if (std::abs(angle) < 1e-5f)
        {
            continue;
        }
        const Quat twist = APM::Math::FromAxisAngle(resolved.axis, angle);
        SetRotation(aBones[resolved.helper], APM::Math::Mul(twist, GetRotation(aBones[resolved.helper])));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Secondary skeletons: FK + IK deltas from the main skeleton
// ---------------------------------------------------------------------------------------------------------------------
void ProcessSecondarySkeleton(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                              const APM::Pose::OverrideSnapshot& aPose)
{
    thread_local std::array<ResolvedTwistHelper, TwistHelperCount> helpers;
    thread_local std::vector<Quat> originalLocal;
    bool hasHelpers = false;
    ResolveTwistHelpers(aTarget, aCount, helpers, hasHelpers);
    if (hasHelpers)
    {
        originalLocal.resize(aCount);
        for (uint32_t i = 0; i < aCount; ++i)
        {
            originalLocal[i] = GetRotation(aBones[i]);
        }
    }

    const bool useIk = AnyIkEnabled(aPose);
    thread_local ModelPose originalPose;
    thread_local ModelPose currentPose;
    if (useIk)
    {
        ComputeModelPose(aTarget, aBones, aCount, originalPose);
    }

    ApplyForwardOverrides(aTarget, aBones, aCount, aPose);

    if (useIk)
    {
        // Chains this skeleton has are solved on its own pose; the main skeleton's deltas fill in the rest.
        thread_local std::vector<IkDelta> ownDeltas;
        ownDeltas.clear();
        ComputeModelPose(aTarget, aBones, aCount, currentPose);
        const uint32_t solved = SolveIkChains(aTarget, aBones, aCount, aPose, originalPose, currentPose, ownDeltas);

        thread_local std::vector<IkDelta> deltas;
        ReadIkDeltas(aTarget.entity, deltas);
        for (const auto& delta : deltas)
        {
            bool ownChain = false;
            for (size_t chain = 0; chain < IkChainCount; ++chain)
            {
                if ((solved & (1u << chain)) && BelongsToChain(delta.bone, chain))
                {
                    ownChain = true;
                }
            }
            const int32_t index = IndexOf(aTarget, delta.bone);
            if (!ownChain && index >= 0 && static_cast<uint32_t>(index) < aCount)
            {
                SetRotation(aBones[index], APM::Math::Mul(GetRotation(aBones[index]), delta.delta));
            }
        }
    }

    if (hasHelpers)
    {
        ApplyTwistHelpers(helpers, originalLocal, aBones);
    }
}
} // namespace

namespace APM::Pose::PoseProcessor
{
void Process(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aBoneCount, const OverrideSnapshot& aPose)
{
    if (aTarget.debugName == MainSkeletonName)
    {
        ProcessMainSkeleton(aTarget, aBones, aBoneCount, aPose);
    }
    else
    {
        ProcessSecondarySkeleton(aTarget, aBones, aBoneCount, aPose);
    }
}
} // namespace APM::Pose::PoseProcessor
