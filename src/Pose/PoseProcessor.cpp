#include "Pose/PoseProcessor.hpp"

#include "Core/Log.hpp"
#include "Math/Vec3.hpp"
#include "Pose/Characters.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/FaceRig.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/JointLimits.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Pose/TwoBoneIk.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;
using APM::Pose::IkChain;
using APM::Pose::IkChainCount;

constexpr const char* MainSkeletonName = "root";

std::mutex g_bodyRotationMutex;
// Per character (Pose/Characters.hpp).
constexpr size_t CharacterCount = APM::Pose::Characters::Count;
std::array<std::array<Quat, IkChainCount>, CharacterCount> g_bodyRotations{};
std::array<std::array<APM::Pose::PoseProcessor::IkFrame, IkChainCount>, CharacterCount> g_ikFrames{};
std::array<std::array<bool, IkChainCount>, CharacterCount> g_ikFrameValid{};

// Character of a target (0 = V, 1-3 = added characters).
size_t CharacterOf(const APM::Pose::Target& aTarget)
{
    return aTarget.photoModeSlot < CharacterCount ? aTarget.photoModeSlot : 0;
}

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
// Photo Mode's "Look at camera" is on: our edits of the eyes wait (the game has priority). The head and the chest are
// left to work as before: the game turns them on top of the pose, and pausing them mixed two poses.
bool IsLookAtBone(size_t aCharacter, uint64_t aHash)
{
    if (APM::Pose::LookAtCamera::Reach(static_cast<uint32_t>(aCharacter)) == 0)
    {
        return false;
    }
    static const uint64_t left = Hash("LeftEye");
    static const uint64_t right = Hash("RightEye");
    return aHash == left || aHash == right;
}

bool IsEyeBone(uint64_t aHash)
{
    static const uint64_t left = Hash("LeftEye");
    static const uint64_t right = Hash("RightEye");
    return aHash == left || aHash == right;
}

void ApplyForwardOverrides(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                           const APM::Pose::OverrideSnapshot& aPose, bool aSameNameBones)
{
    for (const auto& bone : aPose.bones)
    {
        if (IsLookAtBone(CharacterOf(aTarget), bone.boneHash))
        {
            continue;
        }
        if (bone.hasRotation)
        {
            for (const auto& link : aTarget.linkedBones)
            {
                if (link.sourceBone != bone.boneHash || link.targetIndex >= aCount)
                {
                    continue;
                }
                Quat animatedEye{};
                Vec3 unused{};
                if (IsEyeBone(bone.boneHash) && APM::Pose::GizmoState::GetAnimatedLocal(bone.boneHash, animatedEye, unused))
                {
                    // Edited eyes are absolute on the face rig: they point where the body's eye bone points (animation
                    // + edit), so a facial expression that rolls the eyeballs no longer moves them.
                    const Quat change = APM::Math::Mul(APM::Math::Conjugate(link.sourceReferenceLocal),
                                                       APM::Math::Mul(animatedEye, bone.rotation));
                    const Quat converted = APM::Math::Mul(APM::Math::Mul(link.pre, change), link.post);
                    SetRotation(aBones[link.targetIndex],
                                APM::Math::Normalize(APM::Math::Mul(link.targetReferenceLocal, converted)));
                    continue;
                }
                const Quat converted = APM::Math::Mul(APM::Math::Mul(link.pre, bone.rotation), link.post);
                SetRotation(aBones[link.targetIndex], APM::Math::Mul(GetRotation(aBones[link.targetIndex]), converted));
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

        // Pinned to the body: the pin moves and turns with the bone the limb hangs from.
        Quat body{0.f, 0.f, 0.f, 1.f};
        Vec3 target = aOriginalPose.positions[c] + offset;
        const int16_t p = static_cast<size_t>(a) < aTarget.parents.size() ? aTarget.parents[a] : int16_t(-1);
        if (p >= 0 && static_cast<uint32_t>(p) < aCount)
        {
            body = APM::Math::Normalize(
                APM::Math::Mul(aCurrentPose.rotations[p], APM::Math::Conjugate(aOriginalPose.rotations[p])));
            if (settings.followBody)
            {
                target = aCurrentPose.positions[p] +
                         APM::Math::Rotate(body, aOriginalPose.positions[c] + offset - aOriginalPose.positions[p]);
            }
        }
        {
            std::scoped_lock lock(g_bodyRotationMutex);
            g_bodyRotations[CharacterOf(aTarget)][chainIndex] = body;
            if (p >= 0 && static_cast<uint32_t>(p) < aCount)
            {
                g_ikFrames[CharacterOf(aTarget)][chainIndex] = {aOriginalPose.positions[c], aOriginalPose.rotations[c],
                                          aOriginalPose.positions[p], aCurrentPose.positions[p], body};
                g_ikFrameValid[CharacterOf(aTarget)][chainIndex] = true;
            }
        }

        APM::Pose::TwoBoneIkInput input;
        input.a = aCurrentPose.positions[a];
        input.b = aCurrentPose.positions[b];
        input.c = aCurrentPose.positions[c];
        input.aGlobal = aCurrentPose.rotations[a];
        input.bGlobal = aCurrentPose.rotations[b];
        input.aLocal = GetRotation(aBones[a]);
        input.bLocal = GetRotation(aBones[b]);
        input.target = target;
        input.swivelRadians = APM::Math::DegToRad(settings.swivel);
        {
            // Knees point forward, elbows back and a little down, following the hips / the chest.
            const bool leg = chainIndex >= 2;
            const int32_t frameBone = IndexOf(aTarget, Hash(leg ? "Hips" : "Spine3"));
            Quat frame{0.f, 0.f, 0.f, 1.f};
            if (frameBone >= 0 && static_cast<uint32_t>(frameBone) < aCount &&
                static_cast<size_t>(frameBone) < aTarget.refRotationsMS.size())
            {
                frame = APM::Math::Mul(aCurrentPose.rotations[frameBone], APM::Math::Conjugate(aTarget.refRotationsMS[frameBone]));
            }
            input.poleHint = APM::Math::Normalize(APM::Math::Rotate(frame, leg ? Vec3{0.f, 1.f, 0.f} : Vec3{0.f, -1.f, -0.4f}));
        }

        auto result = APM::Pose::SolveTwoBoneIk(input);

        // Joint limits: the thigh / upper arm and the knee / elbow stay within their natural range, so the hand /
        // foot stops where the limb can no longer follow the target.
        if (APM::Pose::JointLimits::IsEnabled())
        {
            const Quat parentGlobal = APM::Math::Mul(input.aGlobal, APM::Math::Conjugate(input.aLocal));
            result.aLocal = APM::Pose::JointLimits::LimitLocal(chain.upper, result.aLocal, input.aLocal, false);
            result.bLocal = APM::Pose::JointLimits::LimitLocal(chain.middle, result.bLocal, input.bLocal, false);
            result.aGlobal = APM::Math::Normalize(APM::Math::Mul(parentGlobal, result.aLocal));
            result.bGlobal = APM::Math::Normalize(APM::Math::Mul(result.aGlobal, result.bLocal));
        }

        // The pinned hand/foot keeps the orientation the animation gave it, plus the player's own rotation of that
        // hand/foot - it no longer turns with the torso or the hips above it.
        const Quat cLocalOriginal =
            APM::Math::Mul(APM::Math::Conjugate(aOriginalPose.rotations[b]), aOriginalPose.rotations[c]);
        const Quat cEdit = APM::Math::Mul(APM::Math::Conjugate(cLocalOriginal), GetRotation(aBones[c]));
        Quat cWorld = APM::Math::Mul(aOriginalPose.rotations[c], cEdit);
        if (settings.followBody)
        {
            cWorld = APM::Math::Mul(body, cWorld);
        }
        Quat cLocalNew = APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(result.bGlobal), cWorld));
        // The pinned hand / foot still bends only as far as a wrist / ankle can (joint limits).
        cLocalNew = APM::Pose::JointLimits::LimitLocal(chain.end, cLocalNew, cLocalOriginal, false);

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
// Photo Mode's "Look at camera" set to "custom" turns these bones towards a point the plugin does not know: a full
// saved pose leaves them as the game animates them (with "camera" the plugin turns them itself, see KeepLookAt).
bool IsCustomLookAtBone(size_t aCharacter, uint64_t aHash)
{
    const auto character = static_cast<uint32_t>(aCharacter);
    if (!APM::Pose::LookAtCamera::IsCustom(character))
    {
        return false;
    }
    static const uint64_t torso[] = {Hash("Spine"), Hash("Spine1"), Hash("Spine2"), Hash("Spine3")};
    static const uint64_t head[] = {Hash("Neck"), Hash("Neck1"), Hash("Head")};
    const int32_t reach = APM::Pose::LookAtCamera::Reach(character);
    if (reach >= 2 && std::find(std::begin(head), std::end(head), aHash) != std::end(head))
    {
        return true;
    }
    return reach >= 3 && std::find(std::begin(torso), std::end(torso), aHash) != std::end(torso);
}

// Full saved pose: replaces the animated local rotations (and the hips' position) of the bones it has.
void ApplyBasePose(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                   const APM::Pose::BasePose& aBase)
{
    thread_local std::unordered_map<uint64_t, uint32_t> indices;
    indices.clear();
    const uint32_t count = std::min<uint32_t>(aCount, static_cast<uint32_t>(aTarget.boneNames.size()));
    for (uint32_t i = 0; i < count; ++i)
    {
        indices.emplace(aTarget.boneNames[i], i);
    }
    for (const auto& bone : aBase.bones)
    {
        const auto it = indices.find(bone.boneHash);
        if (it == indices.end() || IsLookAtBone(CharacterOf(aTarget), bone.boneHash) || IsCustomLookAtBone(CharacterOf(aTarget), bone.boneHash))
        {
            continue;
        }
        auto& transform = aBones[it->second];
        SetRotation(transform, bone.rotation);
        if (bone.hasTranslation)
        {
            transform.Translation.X = bone.translation[0];
            transform.Translation.Y = bone.translation[1];
            transform.Translation.Z = bone.translation[2];
        }
    }
}

// Which way the face looks in the reference pose (model space): from the head towards the eyes, level.
Vec3 FaceForward(const APM::Pose::Target& aTarget)
{
    const size_t refCount = std::min(aTarget.refRotationsMS.size(), aTarget.refTranslationsLS.size());
    thread_local std::vector<Vec3> refPositions;
    refPositions.resize(refCount);
    for (size_t i = 0; i < refCount; ++i)
    {
        const int16_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : int16_t(-1);
        refPositions[i] = parent >= 0 && static_cast<size_t>(parent) < i
                              ? refPositions[parent] + APM::Math::Rotate(aTarget.refRotationsMS[parent], aTarget.refTranslationsLS[i])
                              : aTarget.refTranslationsLS[i];
    }
    const int32_t head = IndexOf(aTarget, Hash("Head"));
    const int32_t leftEye = IndexOf(aTarget, Hash("LeftEye"));
    const int32_t rightEye = IndexOf(aTarget, Hash("RightEye"));
    if (head >= 0 && leftEye >= 0 && rightEye >= 0 && static_cast<size_t>(head) < refCount &&
        static_cast<size_t>(leftEye) < refCount && static_cast<size_t>(rightEye) < refCount)
    {
        Vec3 toEyes = (refPositions[leftEye] + refPositions[rightEye]) * 0.5f - refPositions[head];
        toEyes.z = 0.f;
        if (APM::Math::Length(toEyes) > 1e-4f)
        {
            return APM::Math::Normalize(toEyes);
        }
    }
    return {0.f, 1.f, 0.f};
}

// Eyes aimed at the camera while Photo Mode's "Look at camera" turns the head or the torso (the game leaves them
// straight once the head reaches its limit). Final local rotations of LeftEye / RightEye, for the face rig.
std::mutex g_cameraEyeMutex;
std::array<std::array<Quat, 2>, CharacterCount> g_cameraEyeLocals{};
std::array<bool, CharacterCount> g_cameraEyesValid{};

void AimEyesAtCamera(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const auto camera = APM::Pose::LookAtCamera::GetCamera(static_cast<uint32_t>(CharacterOf(aTarget)));
    const int32_t head = IndexOf(aTarget, Hash("Head"));
    const size_t character = CharacterOf(aTarget);
    if (APM::Pose::LookAtCamera::Reach(static_cast<uint32_t>(character)) < 2 ||
        !APM::Pose::LookAtCamera::AimsAtCamera(static_cast<uint32_t>(character)) || !camera.valid || head < 0 ||
        static_cast<uint32_t>(head) >= aCount ||
        static_cast<size_t>(head) >= aTarget.refRotationsMS.size())
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        g_cameraEyesValid[character] = false;
        return;
    }
    constexpr float MaxEyeTurn = 0.52f; // ~30 degrees from where the face looks
    const Vec3 face = FaceForward(aTarget);
    thread_local ModelPose current;
    ComputeModelPose(aTarget, aBones, aCount, current);
    const Vec3 faceDirection =
        APM::Math::Rotate(current.rotations[head], APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[head]), face));

    std::array<Quat, 2> locals{};
    bool ok = true;
    static constexpr const char* Eyes[] = {"LeftEye", "RightEye"};
    for (size_t e = 0; e < 2; ++e)
    {
        const int32_t eye = IndexOf(aTarget, Hash(Eyes[e]));
        const int16_t parent = eye >= 0 && static_cast<size_t>(eye) < aTarget.parents.size() ? aTarget.parents[eye] : int16_t(-1);
        if (eye < 0 || static_cast<uint32_t>(eye) >= aCount || parent < 0 || static_cast<size_t>(eye) >= aTarget.refRotationsMS.size())
        {
            ok = false;
            break;
        }
        Vec3 wanted = camera.position - current.positions[eye];
        if (APM::Math::Length(wanted) < 1e-3f)
        {
            ok = false;
            break;
        }
        wanted = APM::Math::Normalize(wanted);
        const float fromFace = APM::Math::AngleBetween(faceDirection, wanted);
        const Vec3 clampAxis = APM::Math::Cross(faceDirection, wanted);
        if (fromFace > MaxEyeTurn && APM::Math::Length(clampAxis) > 1e-6f)
        {
            wanted = APM::Math::Rotate(APM::Math::FromAxisAngle(clampAxis, MaxEyeTurn), faceDirection);
        }
        const Vec3 eyeLocalForward = APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[eye]), face);
        const Vec3 looking = APM::Math::Rotate(current.rotations[eye], eyeLocalForward);
        const float angle = APM::Math::AngleBetween(looking, wanted);
        const Vec3 axis = APM::Math::Cross(looking, wanted);
        Quat model = current.rotations[eye];
        if (angle > 1e-4f && APM::Math::Length(axis) > 1e-6f)
        {
            model = APM::Math::Mul(APM::Math::FromAxisAngle(axis, angle), model);
        }
        locals[e] = APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model));
        SetRotation(aBones[eye], locals[e]);
    }
    std::scoped_lock lock(g_cameraEyeMutex);
    g_cameraEyeLocals[character] = locals;
    g_cameraEyesValid[character] = ok;
}

// The face rig's eyeballs follow the camera-aimed eyes of the main skeleton.
void ApplyCameraEyesToLinks(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    std::array<Quat, 2> locals{};
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        const size_t character = CharacterOf(aTarget);
        if (!g_cameraEyesValid[character] || APM::Pose::LookAtCamera::Reach(static_cast<uint32_t>(character)) < 2 ||
            !APM::Pose::LookAtCamera::AimsAtCamera(static_cast<uint32_t>(character)))
        {
            return;
        }
        locals = g_cameraEyeLocals[character];
    }
    static const uint64_t left = Hash("LeftEye");
    static const uint64_t right = Hash("RightEye");
    for (const auto& link : aTarget.linkedBones)
    {
        if ((link.sourceBone != left && link.sourceBone != right) || link.targetIndex >= aCount)
        {
            continue;
        }
        const Quat& local = locals[link.sourceBone == left ? 0 : 1];
        const Quat change = APM::Math::Mul(APM::Math::Conjugate(link.sourceReferenceLocal), local);
        const Quat converted = APM::Math::Mul(APM::Math::Mul(link.pre, change), link.post);
        SetRotation(aBones[link.targetIndex], APM::Math::Normalize(APM::Math::Mul(link.targetReferenceLocal, converted)));
    }
}

// Photo Mode's "Look at camera" (head or torso) is part of the animated pose, so a full base pose (T / A pose, saved
// pose) replaces it, and edits of the hips or the back turn the face away from it. Turn the pose (after the edits)
// so the face looks at the camera's position.
// The spine (torso only) turns around the vertical, a share per bone; the neck and the head take the rest.
void KeepLookAt(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const auto character = static_cast<uint32_t>(CharacterOf(aTarget));
    const int32_t reach = APM::Pose::LookAtCamera::Reach(character);
    const auto camera = APM::Pose::LookAtCamera::GetCamera(static_cast<uint32_t>(CharacterOf(aTarget)));
    if (reach < 2 || !camera.valid || !APM::Pose::LookAtCamera::AimsAtCamera(character))
    {
        return; // "custom": the game's own turn stays (the base pose leaves those bones alone)
    }
    struct Share
    {
        const char* bone;
        float weight; // part of the remaining turn
        bool yawOnly; // spine: turns around the vertical only
    };
    static constexpr Share TorsoChain[] = {{"Spine1", 0.2f, true}, {"Spine2", 0.25f, true}, {"Spine3", 0.33f, true},
                                           {"Neck", 0.35f, false}, {"Neck1", 0.5f, false},  {"Head", 1.f, false}};
    static constexpr Share HeadChain[] = {{"Neck", 0.35f, false}, {"Neck1", 0.5f, false}, {"Head", 1.f, false}};
    const Share* chain = reach >= 3 ? TorsoChain : HeadChain;
    const size_t chainSize = reach >= 3 ? std::size(TorsoChain) : std::size(HeadChain);

    const int32_t head = IndexOf(aTarget, Hash("Head"));
    const size_t refCount = std::min(aTarget.refRotationsMS.size(), aTarget.refTranslationsLS.size());
    if (head < 0 || static_cast<uint32_t>(head) >= aCount || static_cast<size_t>(head) >= refCount)
    {
        return;
    }

    const Vec3 face = FaceForward(aTarget);
    const Vec3 faceLocal = APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[head]), face);

    thread_local ModelPose current;
    ComputeModelPose(aTarget, aBones, aCount, current);

    // Looking at the camera: from between the eyes (the head if they are missing) towards the camera's position.
    Vec3 from = current.positions[head];
    const int32_t leftEye = IndexOf(aTarget, Hash("LeftEye"));
    const int32_t rightEye = IndexOf(aTarget, Hash("RightEye"));
    if (leftEye >= 0 && rightEye >= 0 && static_cast<uint32_t>(leftEye) < aCount && static_cast<uint32_t>(rightEye) < aCount)
    {
        from = (current.positions[leftEye] + current.positions[rightEye]) * 0.5f;
    }
    Vec3 wanted = camera.position - from;
    if (APM::Math::Length(wanted) < 1e-3f)
    {
        return;
    }
    wanted = APM::Math::Normalize(wanted);
    {
        // Never more than a natural turn away from the base pose.
        constexpr float MaxTurn = 1.75f; // ~100 degrees
        const Vec3 start = APM::Math::Rotate(current.rotations[head], faceLocal);
        const float angle = APM::Math::AngleBetween(start, wanted);
        const Vec3 axis = APM::Math::Cross(start, wanted);
        if (angle > MaxTurn && APM::Math::Length(axis) > 1e-6f)
        {
            wanted = APM::Math::Rotate(APM::Math::FromAxisAngle(axis, MaxTurn), start);
        }
    }
    for (size_t c = 0; c < chainSize; ++c)
    {
        const int32_t bone = IndexOf(aTarget, Hash(chain[c].bone));
        if (bone < 0 || static_cast<uint32_t>(bone) >= aCount)
        {
            continue;
        }
        const int16_t parent = static_cast<size_t>(bone) < aTarget.parents.size() ? aTarget.parents[bone] : int16_t(-1);
        if (parent < 0)
        {
            continue;
        }
        if (c > 0)
        {
            ComputeModelPose(aTarget, aBones, aCount, current);
        }
        Vec3 facing = APM::Math::Rotate(current.rotations[head], faceLocal);
        Vec3 target = wanted;
        if (chain[c].yawOnly)
        {
            facing.z = 0.f;
            target.z = 0.f;
            if (APM::Math::Length(facing) < 1e-4f || APM::Math::Length(target) < 1e-4f)
            {
                continue;
            }
        }
        const float angle = APM::Math::AngleBetween(facing, target);
        Vec3 axis = APM::Math::Cross(facing, target);
        if (chain[c].yawOnly)
        {
            axis = Vec3{0.f, 0.f, axis.z >= 0.f ? 1.f : -1.f};
        }
        if (angle < 1e-3f || APM::Math::Length(axis) < 1e-6f)
        {
            continue;
        }
        const Quat turn = APM::Math::FromAxisAngle(axis, angle * chain[c].weight);
        const Quat model = APM::Math::Mul(turn, current.rotations[bone]);
        SetRotation(aBones[bone], APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model));
    }
}

// The bones Photo Mode's "Look at camera" turns (head / torso). IK pins are measured on the pose without that turn,
// so a camera move does not drag a world-pinned hand along, and body-pinned hands follow the turn like a manual one.
constexpr const char* LookAtChain[] = {"Spine", "Spine1", "Spine2", "Spine3", "Neck", "Neck1", "Head"};
constexpr const char* PoseSignature[] = {"Hips", "LeftUpLeg", "RightUpLeg", "LeftLeg", "RightLeg"};

struct NoLookAtCache
{
    bool valid = false;
    std::array<Quat, std::size(LookAtChain)> chain{};
    std::array<Quat, std::size(PoseSignature)> signature{};
};
std::array<NoLookAtCache, CharacterCount> g_noLookAtCaches{};

bool SameRotation(const Quat& a, const Quat& b)
{
    return std::abs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r) > 0.9995f; // within ~3.6 degrees (breathing)
}

// Photo Mode pose (no full pose loaded): remembers the look-at bones while the game does not turn them; while it does,
// puts them back in aBones (a copy) if the pose underneath is still the same one.
bool RemoveGameLookAt(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    std::array<int32_t, std::size(LookAtChain)> chain{};
    std::array<int32_t, std::size(PoseSignature)> signature{};
    for (size_t i = 0; i < chain.size(); ++i)
    {
        chain[i] = IndexOf(aTarget, Hash(LookAtChain[i]));
        if (chain[i] < 0 || static_cast<uint32_t>(chain[i]) >= aCount)
        {
            return false;
        }
    }
    for (size_t i = 0; i < signature.size(); ++i)
    {
        signature[i] = IndexOf(aTarget, Hash(PoseSignature[i]));
        if (signature[i] < 0 || static_cast<uint32_t>(signature[i]) >= aCount)
        {
            return false;
        }
    }
    auto& g_noLookAt = g_noLookAtCaches[CharacterOf(aTarget)];
    if (APM::Pose::LookAtCamera::Reach(static_cast<uint32_t>(CharacterOf(aTarget))) < 2)
    {
        for (size_t i = 0; i < chain.size(); ++i)
        {
            g_noLookAt.chain[i] = GetRotation(aBones[chain[i]]);
        }
        for (size_t i = 0; i < signature.size(); ++i)
        {
            g_noLookAt.signature[i] = GetRotation(aBones[signature[i]]);
        }
        g_noLookAt.valid = true;
        return false;
    }
    if (!g_noLookAt.valid)
    {
        return false;
    }
    for (size_t i = 0; i < signature.size(); ++i)
    {
        if (!SameRotation(GetRotation(aBones[signature[i]]), g_noLookAt.signature[i]))
        {
            g_noLookAt.valid = false; // another Photo Mode pose: the remembered bones belong to the old one
            return false;
        }
    }
    for (size_t i = 0; i < chain.size(); ++i)
    {
        SetRotation(aBones[chain[i]], g_noLookAt.chain[i]);
    }
    return true;
}

void ProcessMainSkeleton(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount,
                         const APM::Pose::OverrideSnapshot& aPose)
{
    thread_local ModelPose originalPose;
    thread_local ModelPose currentPose;
    thread_local std::vector<RED4ext::QsTransform> withoutLookAt;

    // A full saved pose takes the place of the Photo Mode pose: edits and IK work on top of it.
    if (aPose.base)
    {
        ApplyBasePose(aTarget, aBones, aCount, *aPose.base);
    }
    APM::Pose::GizmoState::PublishAnimatedPose(aTarget, aBones, aCount);

    const bool useIk = AnyIkEnabled(aPose);
    if (!aPose.base)
    {
        withoutLookAt.assign(aBones, aBones + aCount);
        const bool removed = RemoveGameLookAt(aTarget, withoutLookAt.data(), aCount);
        if (useIk)
        {
            // Where the animation (before any edit, and before the game's look at the camera) puts the hands and feet.
            ComputeModelPose(aTarget, removed ? withoutLookAt.data() : aBones, aCount, originalPose);
        }
    }
    else if (useIk)
    {
        ComputeModelPose(aTarget, aBones, aCount, originalPose);
    }

    ApplyForwardOverrides(aTarget, aBones, aCount, aPose, true);

    // After the edits: an edited body (hips, back) still ends with the face towards the camera.
    KeepLookAt(aTarget, aBones, aCount);

    if (useIk)
    {
        ComputeModelPose(aTarget, aBones, aCount, currentPose);
        SolveIkChains(aTarget, aBones, aCount, aPose, originalPose, currentPose);
    }
    AimEyesAtCamera(aTarget, aBones, aCount);
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
    const bool face = aTarget.debugName == "face_rig";
    std::array<Quat, 2> gameEyes{};
    const bool eyes = face && APM::Pose::EyelidFollow::ReadEyes(aTarget, aBones, aCount, gameEyes);
    if (face)
    {
        APM::Pose::FaceRig::Apply(aTarget, aBones, aCount, aPose);
    }
    ApplyForwardOverrides(aTarget, aBones, aCount, aPose, false);
    ApplyCameraEyesToLinks(aTarget, aBones, aCount);
    if (eyes)
    {
        // The eyelids follow our gaze as the game would make them follow its own.
        APM::Pose::EyelidFollow::Apply(aTarget, aBones, aCount, gameEyes);
    }
}
} // namespace

namespace APM::Pose::PoseProcessor
{
Math::Vec3 ReferenceFaceForward(const Target& aTarget)
{
    return FaceForward(aTarget);
}

void ResetCharacter(uint32_t aCharacter)
{
    if (aCharacter >= CharacterCount)
    {
        return;
    }
    {
        std::scoped_lock lock(g_bodyRotationMutex);
        g_ikFrameValid[aCharacter] = {};
        g_bodyRotations[aCharacter] = {};
    }
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        g_cameraEyesValid[aCharacter] = false;
    }
    g_noLookAtCaches[aCharacter].valid = false;
}

bool GetIkFrame(size_t aChain, IkFrame& aOut)
{
    std::scoped_lock lock(g_bodyRotationMutex);
    const uint32_t character = APM::Pose::Characters::Selected();
    if (aChain >= IkChainCount || !g_ikFrameValid[character][aChain])
    {
        return false;
    }
    aOut = g_ikFrames[character][aChain];
    return true;
}

Math::Quat GetIkBodyRotation(size_t aChain)
{
    std::scoped_lock lock(g_bodyRotationMutex);
    return aChain < IkChainCount ? g_bodyRotations[APM::Pose::Characters::Selected()][aChain] : Math::Quat{0.f, 0.f, 0.f, 1.f};
}

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
