#include "Pose/GizmoState.hpp"
#include "Pose/Characters.hpp"

#include "Core/Log.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include "Pose/PhysicsBones.hpp"
#include "Pose/PoseOverrides.hpp"

#include <algorithm>
#include <array>
#include <atomic>
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
    // Physics skeletons only: rig data, virtual group bones appended after the rig's bones, local pose before edits.
    std::string name;
    uintptr_t component = 0;
    uintptr_t rig = 0;
    std::vector<uint8_t> pivots;
    size_t rigCount = 0;
    std::vector<int16_t> chainRoots;
    std::vector<int8_t> groups;
    std::vector<Quat> animatedRotations;
    std::vector<Vec3> animatedTranslations;
    uint64_t animatedUpdate = 0;
};

std::mutex g_mutex;
struct HandSlots
{
    Vec3 positions[2]{};
    Quat rotations[2]{};
    uint64_t lastUpdate = 0;
};
std::array<HandSlots, 4> g_hands{}; // per character: [0] left, [1] right
std::vector<Skeleton> g_physics; // selected character's physics skeletons
uint32_t g_physicsSlot = UINT32_MAX;
std::atomic<uint32_t> g_physicsRevision{1};
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
    for (const auto& skeleton : g_physics)
    {
        if (now - skeleton.lastUpdate > 1000)
        {
            continue;
        }
        aIndex = IndexOf(skeleton, aBoneHash);
        if (aIndex >= 0 && (static_cast<size_t>(aIndex) >= skeleton.rigCount ||
                            (static_cast<size_t>(aIndex) < skeleton.chainRoots.size() && skeleton.chainRoots[aIndex] >= 0)))
        {
            return &skeleton;
        }
    }
    return nullptr;
}

// The main skeleton when it has the bone, else the physics skeleton that owns it (fresh or not; nullptr if none).
const Skeleton* MainOrPhysicsLocked(uint64_t aBoneHash, int32_t& aIndex)
{
    aIndex = IndexOf(g_main, aBoneHash);
    if (aIndex >= 0)
    {
        return &g_main;
    }
    for (const auto& skeleton : g_physics)
    {
        aIndex = IndexOf(skeleton, aBoneHash);
        if (aIndex >= 0 && (static_cast<size_t>(aIndex) >= skeleton.rigCount ||
                            (static_cast<size_t>(aIndex) < skeleton.chainRoots.size() && skeleton.chainRoots[aIndex] >= 0)))
        {
            return &skeleton;
        }
    }
    aIndex = -1;
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
            APM::Log::Verbose("Gizmo: %s leftHand=(%.3f %.3f %.3f)", name, p.x, p.y, p.z);
        }
    }
}
} // namespace

namespace
{
void PublishPhysics(const APM::Pose::Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    using namespace APM;
    if (aTarget.parents.size() < aCount || aTarget.chainRoots.size() < aCount || aTarget.groups.size() < aCount ||
        aTarget.pivots.size() < aCount)
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
        const int16_t parent = aTarget.parents[i];
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

    // Virtual group bones: at the middle of their members, turned by the group's edit from the bone they hang from.
    thread_local std::vector<uint64_t> groupNames;
    thread_local std::vector<int16_t> groupParents;
    groupNames.clear();
    groupParents.clear();
    const auto snapshot = Pose::PoseOverrides::GetSnapshot(aTarget.photoModeSlot);
    for (size_t g = 0; g < Pose::PhysicsBones::GroupCount; ++g)
    {
        Vec3 sum{};
        int32_t members = 0;
        int16_t parent = -1;
        int32_t pivot = -1;
        for (uint32_t i = 0; i < aCount; ++i)
        {
            if (aTarget.groups[i] == static_cast<int8_t>(g))
            {
                sum = sum + positions[i];
                ++members;
                parent = aTarget.parents[i];
                pivot = aTarget.pivots[i] ? static_cast<int32_t>(i) : pivot;
            }
        }
        if (members == 0 || parent < 0)
        {
            continue;
        }
        const uint64_t hash = RED4ext::CName(Pose::PhysicsBones::Groups[g].name).hash;
        Quat edit{0.f, 0.f, 0.f, 1.f};
        for (const auto& bone : snapshot->bones)
        {
            if (bone.boneHash == hash && bone.hasRotation)
            {
                edit = bone.rotation;
            }
        }
        groupNames.push_back(hash);
        groupParents.push_back(parent);
        positions.push_back(pivot >= 0 ? positions[pivot] : sum * (1.f / static_cast<float>(members)));
        rotations.push_back(Math::Normalize(Math::Mul(rotations[parent], edit)));
    }

    const uint64_t now = GetTickCount64();
    std::scoped_lock lock(g_mutex);
    if (g_physicsSlot != aTarget.photoModeSlot)
    {
        g_physics.clear();
        g_physicsSlot = aTarget.photoModeSlot;
        g_physicsRevision.fetch_add(1);
    }
    // Copies of a skeleton share its rig (added characters have several); two skeletons with the same name and their own
    // rigs (V's hair and a hairstyle that comes with clothing) are both kept.
    auto it = std::find_if(g_physics.begin(), g_physics.end(),
                           [&](const Skeleton& s) { return s.name == aTarget.debugName && s.rig == aTarget.rig; });
    if (it != g_physics.end() && it->component != aTarget.animatedComponent && now - it->lastUpdate <= 1000)
    {
        return; // another copy of the same skeleton (added characters have several): the first one is shown
    }
    if (it == g_physics.end())
    {
        g_physics.emplace_back();
        it = std::prev(g_physics.end());
        it->name = aTarget.debugName;
        it->rig = aTarget.rig;
    }
    auto& skeleton = *it;
    skeleton.component = aTarget.animatedComponent;
    const size_t total = aCount + groupNames.size();
    if (skeleton.rigCount != aCount || skeleton.boneNames.size() != total ||
        !std::equal(aTarget.boneNames.begin(), aTarget.boneNames.begin() + aCount, skeleton.boneNames.begin()))
    {
        skeleton.rigCount = aCount;
        skeleton.boneNames.assign(aTarget.boneNames.begin(), aTarget.boneNames.begin() + aCount);
        skeleton.boneNames.insert(skeleton.boneNames.end(), groupNames.begin(), groupNames.end());
        skeleton.parents.assign(aTarget.parents.begin(), aTarget.parents.begin() + aCount);
        skeleton.parents.insert(skeleton.parents.end(), groupParents.begin(), groupParents.end());
        skeleton.chainRoots.assign(aTarget.chainRoots.begin(), aTarget.chainRoots.begin() + aCount);
        skeleton.groups.assign(aTarget.groups.begin(), aTarget.groups.begin() + aCount);
        skeleton.pivots.assign(aTarget.pivots.begin(), aTarget.pivots.begin() + aCount);
        skeleton.animatedRotations.clear();
        skeleton.animatedTranslations.clear();
        g_physicsRevision.fetch_add(1);
    }
    skeleton.positions = positions;
    skeleton.rotations = rotations;
    skeleton.lastUpdate = now;
}
} // namespace

namespace APM::Pose::GizmoState
{
void PublishPhysicsAnimated(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    if (aTarget.photoModeSlot != Characters::Selected() || !aTarget.hasOwnBones)
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    for (auto& skeleton : g_physics)
    {
        if (skeleton.component != aTarget.animatedComponent || skeleton.rigCount != aCount)
        {
            continue;
        }
        skeleton.animatedRotations.resize(aCount);
        skeleton.animatedTranslations.resize(aCount);
        for (uint32_t i = 0; i < aCount; ++i)
        {
            skeleton.animatedRotations[i] = GetRotation(aBones[i]);
            skeleton.animatedTranslations[i] = {aBones[i].Translation.X, aBones[i].Translation.Y, aBones[i].Translation.Z};
        }
        skeleton.animatedUpdate = GetTickCount64();
    }
}

uint32_t GetPhysicsRevision()
{
    return g_physicsRevision.load();
}

std::vector<PhysicsSkeleton> GetPhysicsSkeletons()
{
    std::scoped_lock lock(g_mutex);
    std::vector<PhysicsSkeleton> result;
    for (const auto& skeleton : g_physics)
    {
        PhysicsSkeleton out;
        out.name = skeleton.name;
        out.rig = skeleton.rig;
        out.entity = g_entity;
        for (size_t i = 0; i < skeleton.boneNames.size(); ++i)
        {
            PhysicsBone bone;
            bone.hash = skeleton.boneNames[i];
            bone.parent = i < skeleton.parents.size() ? skeleton.parents[i] : int16_t(-1);
            bone.chainRoot = i < skeleton.chainRoots.size() ? skeleton.chainRoots[i] : int16_t(-1);
            bone.group = i < skeleton.groups.size() ? skeleton.groups[i] : int8_t(-1);
            bone.pivot = i < skeleton.pivots.size() && skeleton.pivots[i] != 0;
            out.bones.push_back(bone);
        }
        result.push_back(std::move(out));
    }
    return result;
}

void PublishHands(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    static const uint64_t slots[2] = {RED4ext::CName("WeaponLeft").hash, RED4ext::CName("WeaponRight").hash};
    if (aTarget.photoModeSlot >= g_hands.size() || aTarget.parents.size() < aCount)
    {
        return;
    }
    HandSlots hands;
    for (int h = 0; h < 2; ++h)
    {
        const auto it = std::find(aTarget.boneNames.begin(), aTarget.boneNames.end(), slots[h]);
        if (it == aTarget.boneNames.end())
        {
            return;
        }
        std::array<int32_t, 32> chain{};
        size_t length = 0;
        for (int32_t i = static_cast<int32_t>(std::distance(aTarget.boneNames.begin(), it));
             i >= 0 && static_cast<uint32_t>(i) < aCount && length < chain.size(); i = aTarget.parents[i])
        {
            chain[length++] = i;
            if (aTarget.parents[i] >= i)
            {
                break;
            }
        }
        Vec3 position{};
        Quat rotation{0.f, 0.f, 0.f, 1.f};
        for (size_t k = length; k-- > 0;)
        {
            const auto& bone = aBones[chain[k]];
            position = position + Math::Rotate(rotation, Vec3{bone.Translation.X, bone.Translation.Y, bone.Translation.Z});
            rotation = Math::Normalize(Math::Mul(rotation, GetRotation(bone)));
        }
        hands.positions[h] = position;
        hands.rotations[h] = rotation;
    }
    hands.lastUpdate = GetTickCount64();
    std::scoped_lock lock(g_mutex);
    g_hands[aTarget.photoModeSlot] = hands;
}

bool GetHandSlot(uint32_t aCharacter, bool aRightHand, Math::Vec3& aPosition, Math::Quat& aRotation)
{
    std::scoped_lock lock(g_mutex);
    if (aCharacter >= g_hands.size() || GetTickCount64() - g_hands[aCharacter].lastUpdate > 1000)
    {
        return false;
    }
    aPosition = g_hands[aCharacter].positions[aRightHand ? 1 : 0];
    aRotation = g_hands[aCharacter].rotations[aRightHand ? 1 : 0];
    return true;
}

void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, uintptr_t)
{
    if (aTarget.debugName == "root" && aCount == aTarget.boneNames.size())
    {
        PublishHands(aTarget, aBones, aCount);
    }
    if (aTarget.photoModeSlot != Characters::Selected() || aCount != aTarget.boneNames.size())
    {
        return;
    }
    const bool isMain = aTarget.debugName == "root";
    if (!isMain && aTarget.debugName != "deformations")
    {
        if (aTarget.hasOwnBones)
        {
            PublishPhysics(aTarget, aBones, aCount);
        }
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
        // Physics skeletons of another character, or no longer sampled (other hair / outfit): out of the list.
        const size_t before = g_physics.size();
        if (g_physicsSlot != aTarget.photoModeSlot)
        {
            g_physics.clear();
            g_physicsSlot = aTarget.photoModeSlot;
        }
        std::erase_if(g_physics, [now](const Skeleton& s) { return now - s.lastUpdate > 3000; });
        if (g_physics.size() != before)
        {
            g_physicsRevision.fetch_add(1);
        }
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
    g_physics.clear();
    g_physicsSlot = UINT32_MAX;
    g_physicsRevision.fetch_add(1);
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
    int32_t index = -1;
    const Skeleton* found = MainOrPhysicsLocked(aBoneHash, index);
    if (!found || GetTickCount64() - found->lastUpdate > 1000)
    {
        return false;
    }
    const Skeleton& skeleton = *found;
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
    if (aTarget.photoModeSlot != Characters::Selected() || aCount != aTarget.boneNames.size())
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
    int32_t index = -1;
    const Skeleton* found = MainOrPhysicsLocked(aBoneHash, index);
    if (!found)
    {
        return false;
    }
    const Skeleton& skeleton = *found;
    if (GetTickCount64() - skeleton.lastUpdate > 1000 || static_cast<size_t>(index) >= skeleton.parents.size())
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
        int32_t index = -1;
        const Skeleton* physics = MainOrPhysicsLocked(aBoneHash, index);
        if (physics && physics != &g_main)
        {
            if (static_cast<size_t>(index) >= physics->rigCount)
            {
                aRotation = {0.f, 0.f, 0.f, 1.f}; // group: its edit is all of its turn
                aTranslation = {};
                return true;
            }
            if (static_cast<size_t>(index) < physics->animatedRotations.size() &&
                GetTickCount64() - physics->animatedUpdate <= 1000)
            {
                aRotation = physics->animatedRotations[index];
                aTranslation = physics->animatedTranslations[index];
                return true;
            }
        }
    }
    // No custom pose applied: what is shown is the animation.
    return GetFinalLocal(aBoneHash, aRotation, aTranslation);
}

bool GetAnimatedModel(uint64_t aBoneHash, Math::Vec3& aPosition, Math::Quat& aRotation)
{
    std::scoped_lock lock(g_mutex);
    int32_t index = -1;
    const Skeleton* found = MainOrPhysicsLocked(aBoneHash, index);
    if (!found || GetTickCount64() - found->lastUpdate > 1000)
    {
        return false;
    }
    const Skeleton& skeleton = *found;
    const bool fresh = found == &g_main && g_animated.boneNames == skeleton.boneNames && GetTickCount64() - g_animated.lastUpdate <= 1000 &&
                       g_animated.rotations.size() >= skeleton.positions.size() &&
                       g_animated.translations.size() >= skeleton.positions.size();
    if (!fresh)
    {
        // No custom pose applied: what is shown is the animation.
        aPosition = skeleton.positions[index];
        aRotation = skeleton.rotations[index];
        return true;
    }
    // Chain from the root down to the bone.
    std::vector<int32_t> chain;
    for (int32_t i = index; i >= 0 && static_cast<size_t>(i) < skeleton.positions.size() && chain.size() < 256;)
    {
        chain.push_back(i);
        const int32_t parent = static_cast<size_t>(i) < skeleton.parents.size() ? skeleton.parents[i] : -1;
        i = parent < i ? parent : -1; // parents come before their children
    }
    Vec3 position{};
    Quat rotation{0.f, 0.f, 0.f, 1.f};
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        position = position + Math::Rotate(rotation, g_animated.translations[*it]);
        rotation = Math::Normalize(Math::Mul(rotation, g_animated.rotations[*it]));
    }
    aPosition = position;
    aRotation = rotation;
    return true;
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
    // Bones whose position can be edited (the hips, the points held props hang from): their position is part of
    // the pose, the other bones keep the rig's lengths.
    const uint64_t weaponLeft = RED4ext::CName("WeaponLeft").hash;
    const uint64_t weaponRight = RED4ext::CName("WeaponRight").hash;

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
            bone.keepTranslation = bone.hash == hips || bone.hash == weaponLeft || bone.hash == weaponRight;
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
    int32_t index = -1;
    const Skeleton* found = MainOrPhysicsLocked(aBoneHash, index);
    if (!found)
    {
        return false;
    }
    const Skeleton& skeleton = *found;
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
    int32_t index = -1;
    const Skeleton* found = MainOrPhysicsLocked(aBoneHash, index);
    if (!found)
    {
        return false;
    }
    const Skeleton& skeleton = *found;
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
    if (parent < 0 || static_cast<size_t>(parent) >= skeleton.positions.size())
    {
        return false;
    }
    const Vec3 direction = skeleton.positions[index] - skeleton.positions[parent];
    if (Math::Length(direction) < 1e-4f)
    {
        return false;
    }
    // As long as the segment before it (a fingertip ends about where the nail does), at most 4 cm (toes).
    const float length = std::clamp(Math::Length(direction) * 0.8f, 0.01f, 0.04f);
    aOut = skeleton.positions[index] + Math::Normalize(direction) * length;
    return true;
}
} // namespace APM::Pose::GizmoState
