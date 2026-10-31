#include "Pose/PoseProcessor.hpp"

#include "Core/Log.hpp"
#include "Math/Vec3.hpp"
#include "Pose/Characters.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/FacePoses.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/JointLimits.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Pose/TwoBoneIk.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <utility>
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

// Body eyes (LeftEye / RightEye) as the game's pose turns them, before the edits, per character. Edited eyes of a
// character start from its own body eyes (1.2.0 took the selected character's for every character).
struct AnimatedEyes
{
    std::array<Quat, 2> local{};
    uint64_t time = 0;
    bool logged = false;       // diagnostics written for this character
    std::array<bool, 2> odd{}; // body eye too far from the reference (logged once)
};
std::mutex g_animatedEyeMutex;
std::array<AnimatedEyes, CharacterCount> g_animatedEyes{};
constexpr uint64_t AnimatedEyesMaxAgeMs = 1000;
// Beyond this turn from the reference the body eye is not a gaze (the eyes turn at most ~40 degrees): the pose of some
// characters (e.g. Jackie's) turns it oddly, and the edited eye starts from the reference instead.
constexpr float MaxAnimatedEyeTurn = 0.7854f; // 45 degrees

bool IsFinite(const Quat& aQuat)
{
    return std::isfinite(aQuat.i) && std::isfinite(aQuat.j) && std::isfinite(aQuat.k) && std::isfinite(aQuat.r);
}

// Angle (radians) between two rotations.
float AngleBetween(const Quat& aA, const Quat& aB)
{
    const Quat a = APM::Math::Normalize(aA);
    const Quat b = APM::Math::Normalize(aB);
    const float dot = std::fabs(a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r);
    return 2.f * std::acos(std::min(dot, 1.f));
}

void StoreAnimatedEyes(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const int32_t left = IndexOf(aTarget, Hash("LeftEye"));
    const int32_t right = IndexOf(aTarget, Hash("RightEye"));
    if (left < 0 || right < 0 || static_cast<uint32_t>(left) >= aCount || static_cast<uint32_t>(right) >= aCount)
    {
        return;
    }
    std::scoped_lock lock(g_animatedEyeMutex);
    auto& eyes = g_animatedEyes[CharacterOf(aTarget)];
    eyes.local = {GetRotation(aBones[left]), GetRotation(aBones[right])};
    eyes.time = GetTickCount64();
}

// Body eye of the character (0 left, 1 right) before the edits; false when unknown or stale.
bool GetAnimatedEye(size_t aCharacter, size_t aEye, Quat& aOut)
{
    std::scoped_lock lock(g_animatedEyeMutex);
    const auto& eyes = g_animatedEyes[aCharacter];
    if (eyes.time == 0 || GetTickCount64() - eyes.time > AnimatedEyesMaxAgeMs)
    {
        return false;
    }
    aOut = eyes.local[aEye];
    return true;
}

// Body eye usable as the start of an edit: finite and near the reference. Otherwise the reference (logged once).
Quat CheckedAnimatedEye(size_t aCharacter, size_t aEye, const Quat& aAnimated, const Quat& aReference)
{
    const bool finite = IsFinite(aAnimated);
    const float angle = finite ? AngleBetween(aAnimated, aReference) : 0.f;
    if (finite && angle <= MaxAnimatedEyeTurn)
    {
        return aAnimated;
    }
    bool log = false;
    {
        std::scoped_lock lock(g_animatedEyeMutex);
        log = !std::exchange(g_animatedEyes[aCharacter].odd[aEye], true);
    }
    if (log)
    {
        APM::Log::Verbose("Character %zu: %s body eye %s (%.1f degrees from the reference); edited eye starts from the "
                          "reference",
                          aCharacter, aEye == 0 ? "left" : "right", finite ? "turned too far" : "not valid",
                          angle * 57.2958f);
    }
    return aReference;
}

// Diagnostics ([Debug] Log=1), once per character: how far the game's pose turns the body eyes and the face rig's
// eyeballs from their reference.
void LogEyesOnce(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const size_t character = CharacterOf(aTarget);
    std::array<Quat, 2> body{};
    {
        std::scoped_lock lock(g_animatedEyeMutex);
        const auto& eyes = g_animatedEyes[character];
        if (eyes.logged || eyes.time == 0)
        {
            return;
        }
        body = eyes.local;
    }
    std::array<float, 2> bodyAngle{-1.f, -1.f};
    std::array<float, 2> faceAngle{-1.f, -1.f};
    static const uint64_t left = Hash("LeftEye");
    static const uint64_t right = Hash("RightEye");
    for (const auto& link : aTarget.linkedBones)
    {
        if ((link.sourceBone != left && link.sourceBone != right) || link.targetIndex >= aCount)
        {
            continue;
        }
        const size_t e = link.sourceBone == left ? 0 : 1;
        bodyAngle[e] = AngleBetween(body[e], link.sourceReferenceLocal) * 57.2958f;
        faceAngle[e] = AngleBetween(GetRotation(aBones[link.targetIndex]), link.targetReferenceLocal) * 57.2958f;
    }
    if (bodyAngle[0] < 0.f && bodyAngle[1] < 0.f)
    {
        return;
    }
    {
        std::scoped_lock lock(g_animatedEyeMutex);
        g_animatedEyes[character].logged = true;
    }
    APM::Log::Verbose("Character %zu eyes from the reference (degrees): body left %.1f right %.1f, face rig left %.1f "
                      "right %.1f",
                      character, bodyAngle[0], bodyAngle[1], faceAngle[0], faceAngle[1]);
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
                static const uint64_t leftEyeHash = Hash("LeftEye");
                const size_t eye = link.sourceBone == leftEyeHash ? 0 : 1;
                if (IsEyeBone(bone.boneHash) && GetAnimatedEye(CharacterOf(aTarget), eye, animatedEye))
                {
                    animatedEye =
                        CheckedAnimatedEye(CharacterOf(aTarget), eye, animatedEye, link.sourceReferenceLocal);
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
    const auto character = static_cast<uint32_t>(CharacterOf(aTarget));
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
            result.aLocal = APM::Pose::JointLimits::LimitLocal(character, chain.upper, result.aLocal, input.aLocal, false);
            result.bLocal = APM::Pose::JointLimits::LimitLocal(character, chain.middle, result.bLocal, input.bLocal, false);
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
        cLocalNew = APM::Pose::JointLimits::LimitLocal(character, chain.end, cLocalNew, cLocalOriginal, false);

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

// The neck and head of the main skeleton after the camera turn (model space), for the face rig (see FollowMainNeck).
constexpr const char* NeckChain[] = {"Spine3", "Neck", "Neck1", "Head"};
struct MainNeck
{
    std::array<Vec3, std::size(NeckChain)> positions{};
    std::array<Quat, std::size(NeckChain)> rotations{};
    bool valid = false;
};
std::array<MainNeck, CharacterCount> g_mainNecks{}; // under g_cameraEyeMutex

// Where the face looks in a pose, and its own axes: up as the head's, left across (the reference face looks level, so
// they are square).
void FaceAxes(const APM::Pose::Target& aTarget, const ModelPose& aPose, int32_t aHead, const Vec3& aFace, Vec3& aForward,
              Vec3& aLeft, Vec3& aUp)
{
    const Quat headTurn = APM::Math::Mul(aPose.rotations[aHead], APM::Math::Conjugate(aTarget.refRotationsMS[aHead]));
    aForward = APM::Math::Rotate(headTurn, aFace);
    aUp = APM::Math::Normalize(APM::Math::Rotate(headTurn, Vec3{0.f, 0.f, 1.f}));
    aLeft = APM::Math::Normalize(APM::Math::Cross(aUp, aForward));
}

// The chest or the hips seen from their shape (the spine's direction and the line across the arms / thighs) rather
// than from their bones' rotations: custom poses turn those bones around their length and make it up further up the
// chain (a PM pack pose had its head "151 degrees" from Spine3, test 30/10). The face's reference direction carried by
// that shape, with its up and left.
bool BodyAxes(const APM::Pose::Target& aTarget, const ModelPose& aPose, uint32_t aCount, bool aHips, const Vec3& aFace,
              Vec3& aForward, Vec3& aLeft, Vec3& aUp)
{
    const int32_t lower = IndexOf(aTarget, Hash(aHips ? "Hips" : "Spine2"));
    const int32_t upper = IndexOf(aTarget, Hash(aHips ? "Spine1" : "Neck"));
    const int32_t left = IndexOf(aTarget, Hash(aHips ? "LeftUpLeg" : "LeftArm"));
    const int32_t right = IndexOf(aTarget, Hash(aHips ? "RightUpLeg" : "RightArm"));
    const size_t refCount = std::min(aTarget.refRotationsMS.size(), aTarget.refTranslationsLS.size());
    for (const int32_t bone : {lower, upper, left, right})
    {
        if (bone < 0 || static_cast<uint32_t>(bone) >= aCount || static_cast<size_t>(bone) >= refCount)
        {
            return false;
        }
    }
    thread_local std::vector<Vec3> refPositions;
    refPositions.resize(refCount);
    for (size_t i = 0; i < refCount; ++i)
    {
        const int16_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : int16_t(-1);
        refPositions[i] = parent >= 0 && static_cast<size_t>(parent) < i
                              ? refPositions[parent] + APM::Math::Rotate(aTarget.refRotationsMS[parent], aTarget.refTranslationsLS[i])
                              : aTarget.refTranslationsLS[i];
    }
    // An orthonormal frame from the spine (first axis) and the line across (second).
    const auto frame = [](const Vec3& aAlong, const Vec3& aAcross, std::array<Vec3, 3>& aOut) {
        if (APM::Math::Length(aAlong) < 1e-4f)
        {
            return false;
        }
        aOut[0] = APM::Math::Normalize(aAlong);
        const Vec3 across = aAcross - aOut[0] * APM::Math::Dot(aAcross, aOut[0]);
        if (APM::Math::Length(across) < 1e-4f)
        {
            return false;
        }
        aOut[1] = APM::Math::Normalize(across);
        aOut[2] = APM::Math::Cross(aOut[0], aOut[1]);
        return true;
    };
    std::array<Vec3, 3> reference{};
    std::array<Vec3, 3> now{};
    if (!frame(refPositions[upper] - refPositions[lower], refPositions[left] - refPositions[right], reference) ||
        !frame(aPose.positions[upper] - aPose.positions[lower], aPose.positions[left] - aPose.positions[right], now))
    {
        return false;
    }
    const auto carry = [&](const Vec3& aVector) {
        return now[0] * APM::Math::Dot(aVector, reference[0]) + now[1] * APM::Math::Dot(aVector, reference[1]) +
               now[2] * APM::Math::Dot(aVector, reference[2]);
    };
    aForward = APM::Math::Normalize(carry(aFace));
    aUp = APM::Math::Normalize(carry(Vec3{0.f, 0.f, 1.f}));
    aLeft = APM::Math::Normalize(APM::Math::Cross(aUp, aForward));
    return true;
}

// Sideways (+ to the left) and up / down angles (radians) of a direction around aForward, and back.
void ToAngles(const Vec3& aDirection, const Vec3& aForward, const Vec3& aLeft, const Vec3& aUp, float& aSide, float& aPitch)
{
    aSide = std::atan2(APM::Math::Dot(aDirection, aLeft), APM::Math::Dot(aDirection, aForward));
    aPitch = std::asin(std::clamp(APM::Math::Dot(aDirection, aUp), -1.f, 1.f));
}

Vec3 FromAngles(float aSide, float aPitch, const Vec3& aForward, const Vec3& aLeft, const Vec3& aUp)
{
    const float level = std::cos(aPitch);
    return APM::Math::Normalize(aForward * (level * std::cos(aSide)) + aLeft * (level * std::sin(aSide)) +
                                aUp * std::sin(aPitch));
}

// Look at camera moves towards its target gradually, easing in and out (a critically damped spring), as the game's
// does: per character, the head (sideways / up-down from the body) and each eye (from the face).
struct LookSmoothing
{
    float side = 0.f;
    float pitch = 0.f;
    float sideSpeed = 0.f;
    float pitchSpeed = 0.f;
    uint64_t lastUs = 0;
    bool valid = false;
};
std::mutex g_lookSmoothingMutex;
std::array<std::array<LookSmoothing, 3>, CharacterCount> g_lookSmoothing{}; // head, left eye, right eye

uint64_t NowUs()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

float SmoothDamp(float aValue, float aTarget, float& aSpeed, float aSmoothTime, float aDt)
{
    const float omega = 2.f / aSmoothTime;
    const float x = omega * aDt;
    const float decay = 1.f / (1.f + x + 0.48f * x * x + 0.235f * x * x * x);
    const float change = aValue - aTarget;
    const float temp = (aSpeed + omega * change) * aDt;
    aSpeed = (aSpeed - omega * temp) * decay;
    return aTarget + (change + temp) * decay;
}

// How long the head and the torso take to follow the camera (smooth time of the spring, seconds; the eyes 0.12).
constexpr float LookTurnSeconds = 0.6f;

// The target angles eased from the last ones (starting at the target after a pause, e.g. Look at camera just on).
// aBehind: sideways angles all around (the camera behind the character): the turn stays on its side until the camera
// is 25 degrees past straight behind, then goes across the front, instead of flipping as the camera crosses behind.
void SmoothLook(size_t aCharacter, size_t aPart, float aSmoothTime, float& aSide, float& aPitch, bool aBehind = false)
{
    if (aCharacter >= CharacterCount)
    {
        return;
    }
    std::scoped_lock lock(g_lookSmoothingMutex);
    auto& state = g_lookSmoothing[aCharacter][aPart];
    const uint64_t now = NowUs();
    const float dt = static_cast<float>(now - state.lastUs) * 1e-6f;
    if (!state.valid || dt > 0.3f)
    {
        state = {aSide, aPitch, 0.f, 0.f, now, true};
        return;
    }
    if (aBehind)
    {
        constexpr float Pi = 3.14159265f;
        constexpr float Past = 25.f / 57.2958f;
        if (state.side > Pi / 2.f && aSide < -(Pi - Past))
        {
            aSide += 2.f * Pi;
        }
        else if (state.side < -Pi / 2.f && aSide > Pi - Past)
        {
            aSide -= 2.f * Pi;
        }
    }
    state.lastUs = now;
    if (dt <= 0.f)
    {
        aSide = state.side;
        aPitch = state.pitch;
        return;
    }
    state.side = SmoothDamp(state.side, aSide, state.sideSpeed, aSmoothTime, dt);
    state.pitch = SmoothDamp(state.pitch, aPitch, state.pitchSpeed, aSmoothTime, dt);
    aSide = state.side;
    aPitch = state.pitch;
}

// A share of a rotation (the same axis, aShare of the angle).
Quat ScaleRotation(const Quat& aRotation, float aShare)
{
    Quat q = APM::Math::Normalize(aRotation);
    if (q.r < 0.f)
    {
        q = {-q.i, -q.j, -q.k, -q.r};
    }
    const float s = std::sqrt(q.i * q.i + q.j * q.j + q.k * q.k);
    if (s < 1e-6f)
    {
        return {0.f, 0.f, 0.f, 1.f};
    }
    return APM::Math::FromAxisAngle(Vec3{q.i / s, q.j / s, q.k / s}, 2.f * std::atan2(s, q.r) * aShare);
}

// From aFrom towards aTo, aShare of the way.
Quat Blend(const Quat& aFrom, const Quat& aTo, float aShare)
{
    if (aShare >= 1.f)
    {
        return aTo;
    }
    return APM::Math::Normalize(
        APM::Math::Mul(aFrom, ScaleRotation(APM::Math::Mul(APM::Math::Conjugate(aFrom), aTo), aShare)));
}

// Look at camera turns on and off gradually: per character, how much of the plugin's turn its pose takes (0-1, eased
// in and out) and the reach it had, kept while it fades out (the hook keeps posing the character until then).
struct LookBlend
{
    float weight = 0.f;
    float torso = 0.f; // 1 with the torso turning too, 0 the head only: switching between them eases as well
    int32_t reach = 0;
    uint64_t lastUs = 0;
};
std::mutex g_lookBlendMutex;
std::array<LookBlend, CharacterCount> g_lookBlends{};
constexpr float LookFadeSeconds = 0.4f;

// Once per frame of the main skeleton: towards 1 while the plugin aims at the camera, towards 0 otherwise.
void UpdateLookBlend(size_t aCharacter, bool aOn, int32_t aReach)
{
    if (aCharacter >= CharacterCount)
    {
        return;
    }
    std::scoped_lock lock(g_lookBlendMutex);
    auto& blend = g_lookBlends[aCharacter];
    const uint64_t now = NowUs();
    // At most a frame's step: the hook may not have posed the character for a while (turned on without edits).
    const float dt = blend.lastUs == 0 ? 0.f : std::min(static_cast<float>(now - blend.lastUs) * 1e-6f, 0.05f);
    blend.lastUs = now;
    if (aOn)
    {
        if (blend.weight <= 0.f)
        {
            blend.torso = aReach >= 3 ? 1.f : 0.f; // turned on: no switch to ease
        }
        blend.reach = aReach;
        blend.torso = std::clamp(blend.torso + (aReach >= 3 ? dt : -dt) / LookFadeSeconds, 0.f, 1.f);
    }
    blend.weight = std::clamp(blend.weight + (aOn ? dt : -dt) / LookFadeSeconds, 0.f, 1.f);
}

// How much the torso takes part in the plugin's look (eased, 0 the head only).
float TorsoLookShare(size_t aCharacter)
{
    if (aCharacter >= CharacterCount)
    {
        return 0.f;
    }
    std::scoped_lock lock(g_lookBlendMutex);
    const float torso = g_lookBlends[aCharacter].torso;
    return torso * torso * (3.f - 2.f * torso);
}

// The plugin's share of the look at the camera (eased, 0 when off) and the reach it has or had (aReach).
float LookShare(size_t aCharacter, int32_t& aReach)
{
    aReach = 0;
    if (aCharacter >= CharacterCount)
    {
        return 0.f;
    }
    std::scoped_lock lock(g_lookBlendMutex);
    const auto& blend = g_lookBlends[aCharacter];
    if (blend.weight <= 0.f || NowUs() - blend.lastUs > 500000)
    {
        return 0.f; // off, or the character is no longer posed
    }
    aReach = blend.reach;
    return blend.weight * blend.weight * (3.f - 2.f * blend.weight);
}

// The share for the head and the eyes (0 when Look at camera does not reach the head).
float HeadLookShare(size_t aCharacter)
{
    int32_t reach = 0;
    const float share = LookShare(aCharacter, reach);
    return reach >= 2 ? share : 0.f;
}

// A direction kept within a range around aForward, sideways and up / down separately (degrees). A single angle would
// turn around an axis that is arbitrary when the direction is behind (the head then bent sideways, test 30/10).
Vec3 KeepInRange(const Vec3& aWanted, const Vec3& aForward, const Vec3& aLeft, const Vec3& aUp, float aMaxSide,
                 float aMaxUp, float aMaxDown)
{
    const float side = std::atan2(APM::Math::Dot(aWanted, aLeft), APM::Math::Dot(aWanted, aForward));
    const float pitch = std::asin(std::clamp(APM::Math::Dot(aWanted, aUp), -1.f, 1.f));
    const float clampedSide = std::clamp(side, -aMaxSide / 57.2958f, aMaxSide / 57.2958f);
    const float clampedPitch = std::clamp(pitch, -aMaxDown / 57.2958f, aMaxUp / 57.2958f);
    if (clampedSide == side && clampedPitch == pitch)
    {
        return aWanted;
    }
    const float level = std::cos(clampedPitch);
    return APM::Math::Normalize(aForward * (level * std::cos(clampedSide)) + aLeft * (level * std::sin(clampedSide)) +
                                aUp * std::sin(clampedPitch));
}

// A gaze direction kept within how far the game turns the eyes from where the face looks (its gaze tracks at 1, see
// docs/RESEARCH.md).
Vec3 KeepInEyeRange(const Vec3& aWanted, const Vec3& aForward, const Vec3& aLeft, const Vec3& aUp)
{
    return KeepInRange(aWanted, aForward, aLeft, aUp, 40.f, 21.f, 30.f);
}

void AimEyesAtCamera(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const auto camera = APM::Pose::LookAtCamera::GetCamera(static_cast<uint32_t>(CharacterOf(aTarget)));
    const int32_t head = IndexOf(aTarget, Hash("Head"));
    const size_t character = CharacterOf(aTarget);
    const float share = HeadLookShare(character);
    if (share <= 0.f || !camera.valid || head < 0 || static_cast<uint32_t>(head) >= aCount ||
        static_cast<size_t>(head) >= aTarget.refRotationsMS.size())
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        g_cameraEyesValid[character] = false;
        g_mainNecks[character].valid = false;
        return;
    }
    const Vec3 face = FaceForward(aTarget);
    thread_local ModelPose current;
    ComputeModelPose(aTarget, aBones, aCount, current);
    Vec3 faceDirection{};
    Vec3 faceLeft{};
    Vec3 faceUp{};
    FaceAxes(aTarget, current, head, face, faceDirection, faceLeft, faceUp);

    std::array<Quat, 2> locals{};
    bool ok = true;
    static constexpr const char* Eyes[] = {"LeftEye", "RightEye"};
    for (size_t e = 0; e < 2; ++e)
    {
        const int32_t eye = IndexOf(aTarget, Hash(Eyes[e]));
        const int16_t parent = eye >= 0 && static_cast<size_t>(eye) < aTarget.parents.size() ? aTarget.parents[eye] : int16_t(-1);
        if (eye < 0 || static_cast<uint32_t>(eye) >= aCount || parent < 0 || static_cast<uint32_t>(parent) >= aCount ||
            static_cast<size_t>(eye) >= aTarget.refRotationsMS.size())
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
        wanted = KeepInEyeRange(wanted, faceDirection, faceLeft, faceUp);
        const Vec3 eyeLocalForward = APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[eye]), face);
        const Vec3 looking = APM::Math::Rotate(current.rotations[eye], eyeLocalForward);
        const float angle = APM::Math::AngleBetween(looking, wanted);
        const Vec3 axis = APM::Math::Cross(looking, wanted);
        Quat model = current.rotations[eye];
        if (angle > 1e-4f && APM::Math::Length(axis) > 1e-6f)
        {
            model = APM::Math::Mul(APM::Math::FromAxisAngle(axis, angle), model);
        }
        locals[e] = Blend(GetRotation(aBones[eye]),
                          APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model)), share);
        SetRotation(aBones[eye], locals[e]);
    }
    std::scoped_lock lock(g_cameraEyeMutex);
    g_cameraEyeLocals[character] = locals;
    g_cameraEyesValid[character] = ok;
    auto& neck = g_mainNecks[character];
    neck.valid = true;
    for (size_t i = 0; i < std::size(NeckChain); ++i)
    {
        const int32_t bone = IndexOf(aTarget, Hash(NeckChain[i]));
        if (bone < 0 || static_cast<uint32_t>(bone) >= aCount)
        {
            neck.valid = false;
            break;
        }
        neck.positions[i] = current.positions[bone];
        neck.rotations[i] = current.rotations[bone];
    }
}

// The face rig (it carries the visible head and the eyes) is turned by the game's Look at camera on its own: on a saved
// pose its head stays up to 40-65 degrees away from the main skeleton's, which KeepLookAt turned (test 30/10). While
// the plugin aims at the camera, its neck and head take the main skeleton's.
void FollowMainNeck(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const size_t character = CharacterOf(aTarget);
    const float share = HeadLookShare(character);
    if (share <= 0.f)
    {
        return;
    }
    MainNeck main{};
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        main = g_mainNecks[character];
    }
    if (!main.valid)
    {
        return;
    }
    thread_local ModelPose current;
    ComputeModelPose(aTarget, aBones, aCount, current);
    for (size_t i = 0; i < std::size(NeckChain); ++i)
    {
        const int32_t bone = IndexOf(aTarget, Hash(NeckChain[i]));
        const int16_t parent = bone >= 0 && static_cast<size_t>(bone) < aTarget.parents.size() ? aTarget.parents[bone] : int16_t(-1);
        if (bone < 0 || parent < 0 || static_cast<uint32_t>(bone) >= aCount || static_cast<uint32_t>(parent) >= aCount)
        {
            return;
        }
        const Quat parentRotation = current.rotations[parent];
        const Vec3 parentPosition = current.positions[parent];
        const Quat local = Blend(GetRotation(aBones[bone]),
                                 APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(parentRotation), main.rotations[i])),
                                 share);
        SetRotation(aBones[bone], local);
        if (i == 0)
        {
            // The chain's root also takes the main skeleton's position (the rest keep their lengths).
            const Vec3 wanted = APM::Math::Rotate(APM::Math::Conjugate(parentRotation), main.positions[i] - parentPosition);
            auto& t = aBones[bone].Translation;
            t.X += (wanted.x - t.X) * share;
            t.Y += (wanted.y - t.Y) * share;
            t.Z += (wanted.z - t.Z) * share;
        }
        const auto& t = aBones[bone].Translation;
        current.positions[bone] = parentPosition + APM::Math::Rotate(parentRotation, Vec3{t.X, t.Y, t.Z});
        current.rotations[bone] = APM::Math::Mul(parentRotation, local);
    }
}

// Eyes turned by the editor rather than by the game: an eye edit (not paused by the game's Look at camera), or the eyes
// aimed at the camera over a full pose.
bool EyesEditedByUs(const APM::Pose::Target& aTarget, const APM::Pose::OverrideSnapshot& aPose)
{
    const size_t character = CharacterOf(aTarget);
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        if (g_cameraEyesValid[character] && HeadLookShare(character) > 0.f)
        {
            return true;
        }
    }
    for (const auto& bone : aPose.bones)
    {
        if (bone.hasRotation && IsEyeBone(bone.boneHash) && !IsLookAtBone(character, bone.boneHash))
        {
            return true;
        }
    }
    return false;
}

// The eye meshes are skinned to the face rig, whose head the game turns on its own for Look at camera (30-65 degrees
// away from the main skeleton's head on a saved pose, test 30/10): its eyeballs are aimed from its own head, each
// along the way it looks at rest (eyeball -> pupil). False when the rig has no pupils.
bool AimFaceRigEyesAtCamera(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, float aShare)
{
    const auto camera = APM::Pose::LookAtCamera::GetCamera(static_cast<uint32_t>(CharacterOf(aTarget)));
    const int32_t head = IndexOf(aTarget, Hash("Head"));
    if (!camera.valid || head < 0 || static_cast<uint32_t>(head) >= aCount ||
        static_cast<size_t>(head) >= aTarget.refRotationsMS.size())
    {
        return false;
    }
    static constexpr const char* Parts[2][2] = {{"l_J_eye_JNT", "l_J_eye_pupil_JNT"}, {"r_J_eye_JNT", "r_J_eye_pupil_JNT"}};
    std::array<int32_t, 2> eyes{};
    std::array<int32_t, 2> pupils{};
    std::array<int16_t, 2> parents{};
    for (size_t e = 0; e < 2; ++e)
    {
        eyes[e] = IndexOf(aTarget, Hash(Parts[e][0]));
        pupils[e] = IndexOf(aTarget, Hash(Parts[e][1]));
        parents[e] = eyes[e] >= 0 && static_cast<size_t>(eyes[e]) < aTarget.parents.size() ? aTarget.parents[eyes[e]] : int16_t(-1);
        if (eyes[e] < 0 || pupils[e] < 0 || parents[e] < 0 || static_cast<uint32_t>(eyes[e]) >= aCount ||
            static_cast<uint32_t>(parents[e]) >= aCount || static_cast<size_t>(pupils[e]) >= aTarget.refTranslationsLS.size() ||
            APM::Math::Length(aTarget.refTranslationsLS[pupils[e]]) < 1e-5f)
        {
            return false;
        }
    }
    thread_local ModelPose current;
    ComputeModelPose(aTarget, aBones, aCount, current);
    Vec3 faceDirection{};
    Vec3 faceLeft{};
    Vec3 faceUp{};
    FaceAxes(aTarget, current, head, FaceForward(aTarget), faceDirection, faceLeft, faceUp);
    for (size_t e = 0; e < 2; ++e)
    {
        Vec3 wanted = camera.position - current.positions[eyes[e]];
        if (APM::Math::Length(wanted) < 1e-3f)
        {
            return false;
        }
        // Within the game's range (40 degrees sideways, 21 up, 30 down from the face), eased faster than the head.
        float side = 0.f;
        float pitch = 0.f;
        ToAngles(APM::Math::Normalize(wanted), faceDirection, faceLeft, faceUp, side, pitch);
        side = std::clamp(side, -40.f / 57.2958f, 40.f / 57.2958f);
        pitch = std::clamp(pitch, -30.f / 57.2958f, 21.f / 57.2958f);
        SmoothLook(CharacterOf(aTarget), 1 + e, 0.12f, side, pitch);
        wanted = FromAngles(side, pitch, faceDirection, faceLeft, faceUp);
        const Vec3 localForward = APM::Math::Normalize(aTarget.refTranslationsLS[pupils[e]]);
        const Vec3 looking = APM::Math::Rotate(current.rotations[eyes[e]], localForward);
        const float angle = APM::Math::AngleBetween(looking, wanted);
        const Vec3 axis = APM::Math::Cross(looking, wanted);
        Quat model = current.rotations[eyes[e]];
        if (angle > 1e-4f && APM::Math::Length(axis) > 1e-6f)
        {
            model = APM::Math::Mul(APM::Math::FromAxisAngle(axis, angle), model);
        }
        SetRotation(aBones[eyes[e]],
                    Blend(GetRotation(aBones[eyes[e]]),
                          APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(current.rotations[parents[e]]), model)),
                          aShare));
    }
    return true;
}

// The face rig's eyeballs follow the camera-aimed eyes of the main skeleton (or are aimed from the face rig's own
// head, see AimFaceRigEyesAtCamera).
void ApplyCameraEyesToLinks(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    std::array<Quat, 2> locals{};
    const float share = HeadLookShare(CharacterOf(aTarget));
    {
        std::scoped_lock lock(g_cameraEyeMutex);
        const size_t character = CharacterOf(aTarget);
        if (!g_cameraEyesValid[character] || share <= 0.f)
        {
            return;
        }
        locals = g_cameraEyeLocals[character];
    }
    if (AimFaceRigEyesAtCamera(aTarget, aBones, aCount, share))
    {
        return;
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

// Model-space turn of the head so that its face looks along aWanted with its top towards the chest's top (no tilt
// onto the shoulder): the shortest turn of the face, then a roll around aWanted.
Quat TurnFaceTo(const ModelPose& aPose, int32_t aHead, const Vec3& aChestUp, const Vec3& aFaceLocal, const Vec3& aUpLocal,
                const Vec3& aWanted)
{
    const Vec3 facing = APM::Math::Rotate(aPose.rotations[aHead], aFaceLocal);
    Quat turn{0.f, 0.f, 0.f, 1.f};
    const float angle = APM::Math::AngleBetween(facing, aWanted);
    const Vec3 axis = APM::Math::Cross(facing, aWanted);
    if (angle > 1e-4f && APM::Math::Length(axis) > 1e-6f)
    {
        turn = APM::Math::FromAxisAngle(axis, angle);
    }
    const Vec3 wantedUp = aChestUp - aWanted * APM::Math::Dot(aChestUp, aWanted);
    const Vec3 up = APM::Math::Rotate(turn, APM::Math::Rotate(aPose.rotations[aHead], aUpLocal));
    const Vec3 upNow = up - aWanted * APM::Math::Dot(up, aWanted);
    if (APM::Math::Length(wantedUp) < 1e-3f || APM::Math::Length(upNow) < 1e-3f)
    {
        return turn; // looking along the chest's top: no roll to take from it
    }
    const Vec3 a = APM::Math::Normalize(upNow);
    const Vec3 b = APM::Math::Normalize(wantedUp);
    const float roll = std::atan2(APM::Math::Dot(APM::Math::Cross(a, b), aWanted), APM::Math::Dot(a, b));
    return APM::Math::Mul(APM::Math::FromAxisAngle(aWanted, roll), turn);
}

// Whether KeepLookAt turns this character towards the camera (head or torso, "camera" target).
bool TakesOverLookAt(const APM::Pose::Target& aTarget)
{
    const auto character = static_cast<uint32_t>(CharacterOf(aTarget));
    return APM::Pose::LookAtCamera::Reach(character) >= 2 && APM::Pose::LookAtCamera::GetCamera(character).valid &&
           APM::Pose::LookAtCamera::AimsAtCamera(character);
}

// Photo Mode's "Look at camera" (head or torso) is part of the animated pose, so a full base pose (T / A pose, saved
// pose) replaces it, and edits of the hips or the back turn the face away from it. Turn the pose (after the edits)
// so the face looks at the camera's position.
// The spine (torso only) turns around the vertical, a share per bone; the neck and the head take the rest.
void KeepLookAt(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const auto character = static_cast<uint32_t>(CharacterOf(aTarget));
    const auto camera = APM::Pose::LookAtCamera::GetCamera(character);
    UpdateLookBlend(character, TakesOverLookAt(aTarget), APM::Pose::LookAtCamera::Reach(character));
    // Off (or "custom": the game's own turn stays) once the turn has faded out; while it fades in or out, the pose takes
    // that share of it.
    int32_t reach = 0;
    const float share = LookShare(character, reach);
    if (share <= 0.f || reach < 2 || !camera.valid)
    {
        return;
    }
    // The torso (Torso, as far as it takes part while switching) turns the back from the lower back up, each bone a
    // fixed share of the chest's turn around the back's own axis; the neck and the head take the rest, each a part of
    // what remains.
    struct BackShare
    {
        const char* bone;
        float weight; // part of the chest's turn
    };
    static constexpr BackShare Back[] = {{"Spine", 0.3f}, {"Spine1", 0.25f}, {"Spine2", 0.25f}, {"Spine3", 0.2f}};
    struct NeckShare
    {
        const char* bone;
        float weight; // part of the remaining turn
    };
    static constexpr NeckShare Neck[] = {{"Neck", 0.35f}, {"Neck1", 0.5f}, {"Head", 1.f}};
    const float torso = TorsoLookShare(character);

    const int32_t head = IndexOf(aTarget, Hash("Head"));
    const size_t refCount = std::min(aTarget.refRotationsMS.size(), aTarget.refTranslationsLS.size());
    if (head < 0 || static_cast<uint32_t>(head) >= aCount || static_cast<size_t>(head) >= refCount)
    {
        return;
    }

    const Vec3 face = FaceForward(aTarget);
    const Vec3 faceLocal = APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[head]), face);
    const Vec3 headUpLocal = APM::Math::Rotate(APM::Math::Conjugate(aTarget.refRotationsMS[head]), Vec3{0.f, 0.f, 1.f});

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
    Vec3 chestForward{};
    Vec3 chestLeft{};
    Vec3 chestUp{};
    const bool hasChest = BodyAxes(aTarget, current, aCount, false, face, chestForward, chestLeft, chestUp);
    // Eased towards the camera as sideways / up-down angles from the chest as the pose comes (Look at camera turns
    // gradually, as the game's). The limits come after, from these same angles shifted by how the chest sits on the
    // hips and by its turn: measuring the direction again in another frame put "just behind" on the other side there,
    // and the torso and head jumped across with the camera behind (test 31/10).
    float lookSide = 0.f;
    float lookPitch = 0.f;
    if (hasChest)
    {
        ToAngles(wanted, chestForward, chestLeft, chestUp, lookSide, lookPitch);
        SmoothLook(character, 0, LookTurnSeconds, lookSide, lookPitch, true);
        wanted = FromAngles(lookSide, lookPitch, chestForward, chestLeft, chestUp);
        // A camera nearly straight above or below: its sideways angle comes from a tiny horizontal offset (a few
        // centimeters made tens of degrees, or "behind"), and with the up / down angle cut at the limits it turned the
        // torso and the head to one side (test 31/10). The sideways turn shrinks as far as the limits cut the up / down
        // angle (to nothing straight above).
        const float maxUp = (60.f + 15.f * torso) / 57.2958f;
        const float maxDown = (55.f + 20.f * torso) / 57.2958f;
        const float reachable = std::cos(std::clamp(lookPitch, -maxDown, maxUp));
        lookSide *= std::min(1.f, std::cos(lookPitch) / std::max(reachable, 0.05f));
    }

    // The bones as they come, for a turn fading in or out.
    struct Turned
    {
        int32_t bone = -1;
        Quat before{};
    };
    std::array<Turned, std::size(Back) + std::size(Neck)> turned{};
    for (size_t i = 0; i < turned.size(); ++i)
    {
        const char* name = i < std::size(Back) ? Back[i].bone : Neck[i - std::size(Back)].bone;
        const int32_t bone = IndexOf(aTarget, Hash(name));
        const int16_t parent = bone >= 0 && static_cast<size_t>(bone) < aTarget.parents.size() ? aTarget.parents[bone] : int16_t(-1);
        if (bone >= 0 && static_cast<uint32_t>(bone) < aCount && parent >= 0 && static_cast<uint32_t>(parent) < aCount)
        {
            turned[i] = {bone, GetRotation(aBones[bone])};
        }
    }

    // The torso: the chest turns towards the camera, at most 45 degrees sideways from the hips (around the back's axis)
    // and bends towards it, at most 15 degrees up / 20 down (around the hips' side axis: CDPR's moves much more with
    // the camera below, test 31/10), never back from where the pose already has it. From the chest, not from the head:
    // a pose with the head already turned (CDPR's "Action") twisted the middle back far past its range (test 31/10).
    // Each back bone within its natural range (as the edits).
    Vec3 hipsForward{};
    Vec3 hipsLeft{};
    Vec3 hipsUp{};
    float chestTurned = 0.f;  // how far the back turned the chest (radians, + to the left)
    float chestPitched = 0.f; // how far it bent it (radians, + up)
    if (torso > 0.f && hasChest && BodyAxes(aTarget, current, aCount, true, face, hipsForward, hipsLeft, hipsUp))
    {
        float chestSide = 0.f;
        float chestPitch = 0.f;
        ToAngles(chestForward, hipsForward, hipsLeft, hipsUp, chestSide, chestPitch);
        const float wantedSide = lookSide + chestSide;   // from the hips
        const float wantedPitch = lookPitch + chestPitch;
        const float maxChest = 45.f / 57.2958f;
        const float maxChestUp = 15.f / 57.2958f;
        const float maxChestDown = 20.f / 57.2958f;
        const float chestTurn =
            (std::clamp(wantedSide, std::min(-maxChest, chestSide), std::max(maxChest, chestSide)) - chestSide) * torso;
        const float chestBend =
            (std::clamp(wantedPitch, std::min(-maxChestDown, chestPitch), std::max(maxChestUp, chestPitch)) - chestPitch) *
            torso;
        for (size_t i = 0; i < std::size(Back); ++i)
        {
            const int32_t bone = turned[i].bone;
            if (bone < 0 || (std::abs(chestTurn) < 1e-4f && std::abs(chestBend) < 1e-4f))
            {
                continue;
            }
            const int16_t parent = aTarget.parents[bone];
            ComputeModelPose(aTarget, aBones, aCount, current);
            // A positive turn around the side axis tips the front down, so bending up turns the other way.
            const Quat turn = APM::Math::Mul(APM::Math::FromAxisAngle(hipsUp, chestTurn * Back[i].weight),
                                             APM::Math::FromAxisAngle(hipsLeft, -chestBend * Back[i].weight));
            const Quat model = APM::Math::Mul(turn, current.rotations[bone]);
            const Quat local = APM::Math::Normalize(APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model));
            SetRotation(aBones[bone], APM::Pose::JointLimits::LimitLocal(character, Back[i].bone, local, turned[i].before, false));
        }
        ComputeModelPose(aTarget, aBones, aCount, current);
        // The turn the chest really took (the joint limits may have kept part of it).
        Vec3 nowForward{};
        Vec3 nowLeft{};
        Vec3 nowUp{};
        if (BodyAxes(aTarget, current, aCount, false, face, nowForward, nowLeft, nowUp))
        {
            float nowSide = 0.f;
            float nowPitch = 0.f;
            ToAngles(nowForward, hipsForward, hipsLeft, hipsUp, nowSide, nowPitch);
            chestTurned = std::remainder(nowSide - chestSide, 2.f * 3.14159265f);
            chestPitched = nowPitch - chestPitch;
        }
    }

    // The head: never more than a natural turn away from the chest as it now is (~60 degrees sideways, past which the
    // neck mesh twists, test 30/10; upwards as far as the game's, test 31/10); the eyes do the rest.
    Vec3 neckForward{};
    Vec3 neckLeft{};
    Vec3 neckUp{};
    if (hasChest && BodyAxes(aTarget, current, aCount, false, face, neckForward, neckLeft, neckUp))
    {
        const float side = std::clamp(lookSide - chestTurned, -60.f / 57.2958f, 60.f / 57.2958f);
        const float pitch = std::clamp(lookPitch - chestPitched, -55.f / 57.2958f, 60.f / 57.2958f);
        wanted = FromAngles(side, pitch, neckForward, neckLeft, neckUp);
    }
    for (size_t n = 0; n < std::size(Neck); ++n)
    {
        const int32_t bone = turned[std::size(Back) + n].bone;
        if (bone < 0)
        {
            continue;
        }
        const int16_t parent = aTarget.parents[bone];
        ComputeModelPose(aTarget, aBones, aCount, current);
        const Vec3 facing = APM::Math::Rotate(current.rotations[head], faceLocal);
        if (BodyAxes(aTarget, current, aCount, false, face, neckForward, neckLeft, neckUp))
        {
            // The face towards the camera with its top towards the chest's (the head upright on the neck). The shortest
            // turn alone tilted the head onto the shoulder on a pose bent forward (test 30/10).
            // A head turned far to one side and a camera on the other: the shortest turn passes behind the neck, so
            // this bone takes its share of the way across the front instead (at most ~160 degrees at once).
            float faceSide = 0.f;
            float facePitch = 0.f;
            float wantedSide = 0.f;
            float wantedPitch = 0.f;
            ToAngles(facing, neckForward, neckLeft, neckUp, faceSide, facePitch);
            ToAngles(wanted, neckForward, neckLeft, neckUp, wantedSide, wantedPitch);
            Quat turn{};
            if (std::abs(wantedSide - faceSide) > 3.14159265f)
            {
                const float step = std::clamp((wantedSide - faceSide) * Neck[n].weight, -2.8f, 2.8f);
                const Vec3 across = FromAngles(faceSide + step, facePitch + (wantedPitch - facePitch) * Neck[n].weight,
                                               neckForward, neckLeft, neckUp);
                turn = TurnFaceTo(current, head, neckUp, faceLocal, headUpLocal, across);
            }
            else
            {
                turn = ScaleRotation(TurnFaceTo(current, head, neckUp, faceLocal, headUpLocal, wanted), Neck[n].weight);
            }
            const Quat model = APM::Math::Mul(turn, current.rotations[bone]);
            SetRotation(aBones[bone], APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model));
            continue;
        }
        // No chest shape (missing bones): the shortest turn of the face.
        const float angle = APM::Math::AngleBetween(facing, wanted);
        const Vec3 axis = APM::Math::Cross(facing, wanted);
        if (angle < 1e-3f || APM::Math::Length(axis) < 1e-6f)
        {
            continue;
        }
        const Quat model = APM::Math::Mul(APM::Math::FromAxisAngle(axis, angle * Neck[n].weight), current.rotations[bone]);
        SetRotation(aBones[bone], APM::Math::Mul(APM::Math::Conjugate(current.rotations[parent]), model));
    }
    if (share < 1.f)
    {
        for (const auto& t : turned)
        {
            if (t.bone >= 0)
            {
                SetRotation(aBones[t.bone], Blend(t.before, GetRotation(aBones[t.bone]), share));
            }
        }
    }
}

// The bones Photo Mode's "Look at camera" turns (head / torso; on the torso it also turns the hips and thighs a little,
// ~4-10 degrees depending on the camera). IK pins are measured on the pose without that turn, so a camera move does not
// drag a world-pinned hand along, and body-pinned hands follow the turn like a manual one.
constexpr const char* LookAtChain[] = {"Hips", "LeftUpLeg", "RightUpLeg", "Spine", "Spine1", "Spine2",
                                       "Spine3", "Neck", "Neck1", "Head"};

struct NoLookAtCache
{
    bool valid = false;
    uint32_t poseGeneration = 0; // Photo Mode pose the bones belong to (LookAtCamera::PoseGeneration)
    std::array<Quat, std::size(LookAtChain)> chain{};
};
std::mutex g_noLookAtMutex; // anim threads remember and restore, the game thread resets
std::array<NoLookAtCache, CharacterCount> g_noLookAtCaches{};

// Photo Mode pose (no full pose loaded): remembers the look-at bones while the game does not turn them; while it does,
// puts them back in aBones (a copy) if the Photo Mode pose is still the same one (its pose selector did not change).
bool RemoveGameLookAt(const APM::Pose::Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount)
{
    std::array<int32_t, std::size(LookAtChain)> chain{};
    for (size_t i = 0; i < chain.size(); ++i)
    {
        chain[i] = IndexOf(aTarget, Hash(LookAtChain[i]));
        if (chain[i] < 0 || static_cast<uint32_t>(chain[i]) >= aCount)
        {
            return false;
        }
    }
    const auto character = static_cast<uint32_t>(CharacterOf(aTarget));
    const uint32_t generation = APM::Pose::LookAtCamera::PoseGeneration(character);
    if (APM::Pose::LookAtCamera::Reach(character) < 2)
    {
        std::array<Quat, std::size(LookAtChain)> rotations{};
        for (size_t i = 0; i < chain.size(); ++i)
        {
            rotations[i] = GetRotation(aBones[chain[i]]);
        }
        std::scoped_lock lock(g_noLookAtMutex);
        auto& g_noLookAt = g_noLookAtCaches[character];
        g_noLookAt.chain = rotations;
        g_noLookAt.poseGeneration = generation;
        g_noLookAt.valid = true;
        return false;
    }
    std::array<Quat, std::size(LookAtChain)> rotations{};
    {
        std::scoped_lock lock(g_noLookAtMutex);
        auto& g_noLookAt = g_noLookAtCaches[character];
        if (!g_noLookAt.valid)
        {
            return false;
        }
        if (g_noLookAt.poseGeneration != generation)
        {
            g_noLookAt.valid = false; // another Photo Mode pose: the remembered bones belong to the old one
            APM::Log::Verbose("Character %u: Photo Mode pose changed while Look at camera is on; remembered pose dropped",
                              character);
            return false;
        }
        rotations = g_noLookAt.chain;
    }
    for (size_t i = 0; i < chain.size(); ++i)
    {
        SetRotation(aBones[chain[i]], rotations[i]);
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
    StoreAnimatedEyes(aTarget, aBones, aCount);

    const bool useIk = AnyIkEnabled(aPose);
    if (!aPose.base)
    {
        withoutLookAt.assign(aBones, aBones + aCount);
        const bool removed = RemoveGameLookAt(aTarget, withoutLookAt.data(), aCount);
        if (removed && TakesOverLookAt(aTarget))
        {
            // KeepLookAt turns the pose itself, not the game's turn: with the camera behind, the game's head (~140
            // degrees from the chest) was turned to the other side through the back, twisting the neck (test 30/10).
            for (const char* name : LookAtChain)
            {
                const int32_t bone = IndexOf(aTarget, Hash(name));
                SetRotation(aBones[bone], GetRotation(withoutLookAt[bone]));
            }
        }
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
        APM::Pose::FacePoses::OnFinalFace(aTarget, aBones, aCount);
        LogEyesOnce(aTarget, aBones, aCount);
        if (eyes)
        {
            APM::Pose::EyelidFollow::ProbeEyes(aTarget, gameEyes);
        }
        APM::Pose::FacePoses::Apply(aTarget, aBones, aCount, aPose);
    }
    ApplyForwardOverrides(aTarget, aBones, aCount, aPose, false);
    if (face)
    {
        FollowMainNeck(aTarget, aBones, aCount);
    }
    ApplyCameraEyesToLinks(aTarget, aBones, aCount);
    if (eyes)
    {
        // Eyes turned by the editor: their turn goes to the solver's gaze tracks for the next frame. Only a face with
        // eye bones sets it (Johnny has a second face without them, which would switch it off every frame).
        std::array<Vec3, 2> turns{};
        const bool edited = EyesEditedByUs(aTarget, aPose) && APM::Pose::EyelidFollow::EyeTurns(aTarget, aBones, aCount, turns);
        APM::Pose::FacePoses::SetGaze(aTarget.photoModeSlot, edited, turns);
    }
    if (eyes && !APM::Pose::FacePoses::SolverTakesGaze(aTarget))
    {
        // The eyelids follow our gaze as the game would make them follow its own (the solver does it when it takes
        // the gaze from the tracks).
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

bool ShowsLookAt(uint32_t aCharacter)
{
    int32_t reach = 0;
    return LookShare(aCharacter, reach) > 0.f;
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
    {
        std::scoped_lock lock(g_noLookAtMutex);
        g_noLookAtCaches[aCharacter].valid = false;
    }
    {
        std::scoped_lock lock(g_lookBlendMutex);
        g_lookBlends[aCharacter] = {};
    }
    {
        std::scoped_lock lock(g_animatedEyeMutex);
        g_animatedEyes[aCharacter] = {};
    }
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
