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
// aSameNameBones: also edit the bones that carry the edited names (main skeleton only, see ProcessSecondarySkeleton).
void ApplyForwardOverrides(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                           const APM::Pose::OverrideSnapshot& aPose, bool aSameNameBones)
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

        if (!aSameNameBones)
        {
            continue;
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
// IK on the main skeleton (the other skeletons inherit the result). Returns a bit mask of the chains solved.
// ---------------------------------------------------------------------------------------------------------------------
uint32_t SolveIkChains(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                       const APM::Pose::OverrideSnapshot& aPose, const ModelPose& aOriginalPose, ModelPose& aCurrentPose)
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

        // The pinned hand/foot keeps the orientation the animation gave it, plus the player's own rotation of that
        // hand/foot - it no longer turns with the torso or the hips above it.
        const Quat cLocalOriginal =
            APM::Math::Mul(APM::Math::Conjugate(aOriginalPose.rotations[b]), aOriginalPose.rotations[c]);
        const Quat cEdit = APM::Math::Mul(APM::Math::Conjugate(cLocalOriginal), GetRotation(aBones[c]));
        const Quat cWorld = APM::Math::Mul(aOriginalPose.rotations[c], cEdit);
        const Quat cLocalNew = APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(result.bGlobal), cWorld));

        SetRotation(aBones[a], result.aLocal);
        SetRotation(aBones[b], result.bLocal);
        SetRotation(aBones[c], cLocalNew);

        // Keep the cached model pose valid for chains processed later (they never share bones).
        aCurrentPose.rotations[a] = result.aGlobal;
        aCurrentPose.rotations[b] = result.bGlobal;
        solved |= 1u << chainIndex;
    }
    return solved;
}

// ---------------------------------------------------------------------------------------------------------------------
// Main skeleton: FK + IK
// ---------------------------------------------------------------------------------------------------------------------
void ProcessMainSkeleton(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                         const APM::Pose::OverrideSnapshot& aPose)
{
    thread_local ModelPose originalPose;
    thread_local ModelPose currentPose;

    const bool useIk = AnyIkEnabled(aPose);
    if (useIk)
    {
        // Where the animation (before any edit) puts the hands and feet: IK pins them there.
        ComputeModelPose(aTarget, aBones, aCount, originalPose);
    }

    ApplyForwardOverrides(aTarget, aBones, aCount, aPose, true);

    if (useIk)
    {
        ComputeModelPose(aTarget, aBones, aCount, currentPose);
        SolveIkChains(aTarget, aBones, aCount, aPose, originalPose, currentPose);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Secondary skeletons: FK + IK deltas from the main skeleton
// ---------------------------------------------------------------------------------------------------------------------
void ProcessSecondarySkeleton(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                              const APM::Pose::OverrideSnapshot& aPose)
{
    // The other skeletons (shadow, face, hair, breasts, clothing) take the bones they share with the main skeleton
    // from the already edited main pose, one after the other. Editing those bones again multiplied every edit
    // (a chest bent by 8 degrees was drawn bent by about 55). Only bones that do not follow the main skeleton
    // are edited here: the linked ones, such as the face rig's eyeballs.
    ApplyForwardOverrides(aTarget, aBones, aCount, aPose, false);
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
