#include "Pose/GizmoState.hpp"

#include "Core/Log.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;

constexpr uint64_t LogIntervalMs = 5000;

// Final pose of one of V's skeletons.
struct Skeleton
{
    std::vector<uint64_t> boneNames;
    std::vector<int16_t> parents;
    std::vector<Vec3> positions; // model space
    std::vector<Quat> rotations; // model space
    uint64_t lastUpdate = 0;
};

std::mutex g_mutex;
uintptr_t g_entity = 0;
uintptr_t g_component = 0; // main skeleton's animated component
// The body mesh is skinned by the "deformations" skeleton, which the game can adjust after the main one
// (e.g. a hand holding a weapon), so markers follow it first and fall back to the main skeleton ("root").
Skeleton g_deformations;
Skeleton g_main;
uint64_t g_lastLog = 0;

// Main skeleton's local pose before the edits.
struct AnimatedPose
{
    std::vector<uint64_t> boneNames;
    std::vector<Quat> rotations;
    std::vector<Vec3> translations;
    uint64_t lastUpdate = 0;
};
AnimatedPose g_animated;

Quat GetRotation(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

int32_t IndexOf(const Skeleton& aSkeleton, uint64_t aBoneHash)
{
    const auto it = std::find(aSkeleton.boneNames.begin(), aSkeleton.boneNames.end(), aBoneHash);
    if (it == aSkeleton.boneNames.end())
    {
        return -1;
    }
    const auto index = static_cast<int32_t>(std::distance(aSkeleton.boneNames.begin(), it));
    return static_cast<size_t>(index) < aSkeleton.positions.size() ? index : -1;
}

// Skeleton and bone index to read a bone from. Positions prefer the rendered (deformations) skeleton; rotations
// prefer the main one, whose local frames the edits are expressed in.
const Skeleton* FindLocked(uint64_t aBoneHash, int32_t& aIndex, bool aPreferMain)
{
    const uint64_t now = GetTickCount64();
    const Skeleton* first = aPreferMain ? &g_main : &g_deformations;
    const Skeleton* second = aPreferMain ? &g_deformations : &g_main;
    for (const Skeleton* skeleton : {first, second})
    {
        if (now - skeleton->lastUpdate > 1000)
        {
            continue; // stale
        }
        aIndex = IndexOf(*skeleton, aBoneHash);
        if (aIndex >= 0)
        {
            return skeleton;
        }
    }
    return nullptr;
}

void LogLocked()
{
    const auto hand = RED4ext::CName("LeftHand").hash;
    for (const auto& [name, skeleton] : {std::pair{"deformations", &g_deformations}, std::pair{"root", &g_main}})
    {
        const auto index = IndexOf(*skeleton, hand);
        if (index >= 0)
        {
            const auto& p = skeleton->positions[index];
            APM::Log::Info("Gizmo: %s leftHand=(%.3f %.3f %.3f)", name, p.x, p.y, p.z);
        }
    }
}
} // namespace

namespace APM::Pose::GizmoState
{
void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, uintptr_t)
{
    if (aTarget.photoModeSlot != 0 || aCount != aTarget.boneNames.size())
    {
        return;
    }
    const bool isMain = aTarget.debugName == "root";
    if (!isMain && aTarget.debugName != "deformations")
    {
        return;
    }

    thread_local std::vector<Vec3> positions;
    thread_local std::vector<Quat> rotations;
    positions.resize(aCount);
    rotations.resize(aCount);
    for (uint32_t i = 0; i < aCount; ++i)
    {
        const Vec3 localPos{aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
        const Quat localRot = GetRotation(aBones[i]);
        const int16_t parent = i < aTarget.parents.size() ? aTarget.parents[i] : int16_t(-1);
        if (parent >= 0 && static_cast<uint32_t>(parent) < i)
        {
            positions[i] = positions[parent] + Math::Rotate(rotations[parent], localPos);
            rotations[i] = Math::Normalize(Math::Mul(rotations[parent], localRot));
        }
        else
        {
            positions[i] = localPos;
            rotations[i] = localRot;
        }
    }

    const uint64_t now = GetTickCount64();
    std::scoped_lock lock(g_mutex);
    auto& skeleton = isMain ? g_main : g_deformations;
    if (skeleton.boneNames != aTarget.boneNames)
    {
        skeleton.boneNames = aTarget.boneNames;
        skeleton.parents = aTarget.parents;
    }
    skeleton.positions = positions;
    skeleton.rotations = rotations;
    skeleton.lastUpdate = now;
    if (isMain)
    {
        g_entity = aTarget.entity;
        g_component = aTarget.animatedComponent;
    }

    if (now - g_lastLog >= LogIntervalMs)
    {
        g_lastLog = now;
        LogLocked();
    }
}

void Clear()
{
    std::scoped_lock lock(g_mutex);
    g_entity = 0;
    g_component = 0;
    g_deformations = {};
    g_main = {};
    g_animated = {};
}

bool GetBoneModelPosition(uint64_t aBoneHash, Math::Vec3& aOut)
{
    std::scoped_lock lock(g_mutex);
    int32_t index = -1;
    const auto* skeleton = FindLocked(aBoneHash, index, false);
    if (!skeleton)
    {
        return false;
    }
    aOut = skeleton->positions[index];
    return true;
}

bool GetBoneModelRotation(uint64_t aBoneHash, Math::Quat& aOut)
{
    std::scoped_lock lock(g_mutex);
    int32_t index = -1;
    const auto* skeleton = FindLocked(aBoneHash, index, true);
    if (!skeleton)
    {
        return false;
    }
    aOut = skeleton->rotations[index];
    return true;
}

bool GetParentModelRotation(uint64_t aBoneHash, Math::Quat& aOut)
{
    std::scoped_lock lock(g_mutex);
    const Skeleton& skeleton = g_main;
    const auto index = IndexOf(skeleton, aBoneHash);
    if (index < 0 || GetTickCount64() - skeleton.lastUpdate > 1000)
    {
        return false;
    }
    const int16_t parent = static_cast<size_t>(index) < skeleton.parents.size() ? skeleton.parents[index] : int16_t(-1);
    aOut = parent >= 0 && static_cast<size_t>(parent) < skeleton.rotations.size() ? skeleton.rotations[parent]
                                                                                   : Quat{0.f, 0.f, 0.f, 1.f};
    return true;
}

uintptr_t GetEntity()
{
    std::scoped_lock lock(g_mutex);
    return g_entity;
}

uintptr_t GetComponent()
{
    std::scoped_lock lock(g_mutex);
    return g_component;
}

void PublishAnimatedPose(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    if (aTarget.photoModeSlot != 0 || aCount != aTarget.boneNames.size())
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    if (g_animated.boneNames != aTarget.boneNames)
    {
        g_animated.boneNames = aTarget.boneNames;
    }
    g_animated.rotations.resize(aCount);
    g_animated.translations.resize(aCount);
    for (uint32_t i = 0; i < aCount; ++i)
    {
        g_animated.rotations[i] = GetRotation(aBones[i]);
        g_animated.translations[i] = {aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
    }
    g_animated.lastUpdate = GetTickCount64();
}

bool GetFinalLocal(uint64_t aBoneHash, Math::Quat& aRotation, Math::Vec3& aTranslation)
{
    std::scoped_lock lock(g_mutex);
    const Skeleton& skeleton = g_main;
    const auto index = IndexOf(skeleton, aBoneHash);
    if (index < 0 || GetTickCount64() - skeleton.lastUpdate > 1000 || static_cast<size_t>(index) >= skeleton.parents.size())
    {
        return false;
    }
    const int16_t parent = skeleton.parents[index];
    if (parent < 0 || parent >= index)
    {
        aRotation = skeleton.rotations[index];
        aTranslation = skeleton.positions[index];
        return true;
    }
    const Quat parentInverse = Math::Conjugate(skeleton.rotations[parent]);
    aRotation = Math::Normalize(Math::Mul(parentInverse, skeleton.rotations[index]));
    aTranslation = Math::Rotate(parentInverse, skeleton.positions[index] - skeleton.positions[parent]);
    return true;
}

bool GetAnimatedLocal(uint64_t aBoneHash, Math::Quat& aRotation, Math::Vec3& aTranslation)
{
    {
        std::scoped_lock lock(g_mutex);
        const auto it = std::find(g_animated.boneNames.begin(), g_animated.boneNames.end(), aBoneHash);
        if (it != g_animated.boneNames.end() && GetTickCount64() - g_animated.lastUpdate <= 1000)
        {
            const auto index = static_cast<size_t>(std::distance(g_animated.boneNames.begin(), it));
            if (index < g_animated.rotations.size())
            {
                aRotation = g_animated.rotations[index];
                aTranslation = g_animated.translations[index];
                return true;
            }
        }
    }
    // No custom pose applied: what is shown is the animation.
    return GetFinalLocal(aBoneHash, aRotation, aTranslation);
}

bool CaptureLocalPose(std::vector<CapturedBone>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const Skeleton& skeleton = g_main;
    const size_t count = skeleton.positions.size();
    if (count == 0 || GetTickCount64() - skeleton.lastUpdate > 1000 || skeleton.boneNames.size() < count ||
        skeleton.parents.size() < count || skeleton.rotations.size() < count)
    {
        return false;
    }
    const uint64_t head = RED4ext::CName("Head").hash;
    const uint64_t hips = RED4ext::CName("Hips").hash;

    // Bones below the head (face, eyes) stay out of the pose.
    std::vector<bool> inFace(count, false);
    for (size_t i = 0; i < count; ++i)
    {
        const int16_t parent = skeleton.parents[i];
        if (parent >= 0 && static_cast<size_t>(parent) < i)
        {
            inFace[i] = inFace[parent] || skeleton.boneNames[parent] == head;
        }
    }

    aOut.clear();
    aOut.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        CapturedBone bone;
        bone.hash = skeleton.boneNames[i];
        const char* name = RED4ext::CName(bone.hash).ToString();
        bone.name = name ? name : "";
        const int16_t parent = skeleton.parents[i];
        if (parent >= 0 && static_cast<size_t>(parent) < i)
        {
            const Quat parentInverse = Math::Conjugate(skeleton.rotations[parent]);
            bone.rotation = Math::Normalize(Math::Mul(parentInverse, skeleton.rotations[i]));
            bone.translation = Math::Rotate(parentInverse, skeleton.positions[i] - skeleton.positions[parent]);
            bone.inBase = !inFace[i] && !bone.name.empty() && bone.name != "None";
            bone.keepTranslation = bone.hash == hips;
        }
        else
        {
            bone.rotation = skeleton.rotations[i];
            bone.translation = skeleton.positions[i];
        }
        aOut.push_back(std::move(bone));
    }
    return true;
}

bool GetAncestorModelPosition(uint64_t aBoneHash, const std::vector<uint64_t>& aCandidates, Math::Vec3& aOut)
{
    std::scoped_lock lock(g_mutex);
    const Skeleton& skeleton = g_main;
    int32_t index = IndexOf(skeleton, aBoneHash);
    if (index < 0)
    {
        return false;
    }
    for (int32_t guard = 0; guard < 256; ++guard)
    {
        const int16_t parent = static_cast<size_t>(index) < skeleton.parents.size() ? skeleton.parents[index] : int16_t(-1);
        if (parent < 0 || static_cast<size_t>(parent) >= skeleton.positions.size())
        {
            return false;
        }
        index = parent;
        if (std::find(aCandidates.begin(), aCandidates.end(), skeleton.boneNames[index]) != aCandidates.end())
        {
            aOut = skeleton.positions[index];
            return true;
        }
    }
    return false;
}

bool GetBoneTipModelPosition(uint64_t aBoneHash, Math::Vec3& aOut)
{
    std::scoped_lock lock(g_mutex);
    const Skeleton& skeleton = g_main;
    const auto index = IndexOf(skeleton, aBoneHash);
    if (index < 0)
    {
        return false;
    }
    // Children that are not on top of the bone: their average (a hand points at the middle of its fingers, the
    // chest between the neck and the shoulders), or the farthest one when they surround the bone (the hips).
    const Vec3 origin = skeleton.positions[index];
    Vec3 sum{};
    Vec3 farthest{};
    float farthestDistance = 0.f;
    int32_t children = 0;
    for (size_t child = static_cast<size_t>(index) + 1; child < skeleton.parents.size() && child < skeleton.positions.size();
         ++child)
    {
        const float distance = Math::Length(skeleton.positions[child] - origin);
        if (skeleton.parents[child] == index && distance > 0.01f)
        {
            sum = sum + skeleton.positions[child];
            ++children;
            if (distance > farthestDistance)
            {
                farthestDistance = distance;
                farthest = skeleton.positions[child];
            }
        }
    }
    if (children > 0)
    {
        const Vec3 average = sum * (1.f / static_cast<float>(children));
        aOut = Math::Length(average - origin) > 0.03f ? average : farthest;
        return true;
    }
    // Leaf bone: continue the parent -> bone direction.
    const int16_t parent = static_cast<size_t>(index) < skeleton.parents.size() ? skeleton.parents[index] : int16_t(-1);
    if (parent < 0)
    {
        return false;
    }
    const Vec3 direction = skeleton.positions[index] - skeleton.positions[parent];
    if (Math::Length(direction) < 1e-4f)
    {
        return false;
    }
    aOut = skeleton.positions[index] + Math::Normalize(direction) * 0.08f;
    return true;
}
} // namespace APM::Pose::GizmoState
