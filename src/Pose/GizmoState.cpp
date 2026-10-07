#include "Pose/GizmoState.hpp"

#include "Core/SafeMemory.hpp"

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;

// ent::Entity::transformComponent (IPlacedComponent*) and IPlacedComponent::localTransform (WorldTransform).
// The Photo Mode puppet is not attached to anything, so its local transform is its world transform.
constexpr uintptr_t EntityTransformComponent = 0xB0;
constexpr uintptr_t PlacedComponentLocalTransform = 0xC0;
constexpr float FixedPointScale = 1.f / 131072.f; // FixedPoint: 17 fractional bits

std::mutex g_mutex;
uintptr_t g_entity = 0;
std::vector<uint64_t> g_boneNames;
std::vector<Vec3> g_positions; // model space

Quat GetRotation(const RED4ext::QsTransform& aTransform)
{
    return {aTransform.Rotation.i, aTransform.Rotation.j, aTransform.Rotation.k, aTransform.Rotation.r};
}

bool ReadEntityTransform(uintptr_t aEntity, Vec3& aPosition, Quat& aOrientation)
{
    uint64_t component = 0;
    if (!aEntity || !APM::SafeMemory::ReadU64(aEntity + EntityTransformComponent, component) ||
        !APM::SafeMemory::LooksLikePointer(component))
    {
        return false;
    }

    struct
    {
        int32_t x;
        int32_t y;
        int32_t z;
        int32_t pad;
        float i;
        float j;
        float k;
        float r;
    } transform{};
    if (!APM::SafeMemory::ReadBytes(component + PlacedComponentLocalTransform, &transform, sizeof(transform)))
    {
        return false;
    }

    aPosition = {transform.x * FixedPointScale, transform.y * FixedPointScale, transform.z * FixedPointScale};
    aOrientation = APM::Math::Normalize(Quat{transform.i, transform.j, transform.k, transform.r});
    return true;
}
} // namespace

namespace APM::Pose::GizmoState
{
void Publish(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    if (aTarget.photoModeSlot != 0 || aTarget.debugName != "root" || aCount != aTarget.boneNames.size())
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

    std::scoped_lock lock(g_mutex);
    g_entity = aTarget.entity;
    if (g_boneNames.size() != aTarget.boneNames.size())
    {
        g_boneNames = aTarget.boneNames;
    }
    g_positions = positions;
}

void Clear()
{
    std::scoped_lock lock(g_mutex);
    g_entity = 0;
    g_boneNames.clear();
    g_positions.clear();
}

bool GetEntityTransform(Math::Vec3& aPosition, Math::Quat& aOrientation)
{
    uintptr_t entity = 0;
    {
        std::scoped_lock lock(g_mutex);
        entity = g_entity;
    }
    return ReadEntityTransform(entity, aPosition, aOrientation);
}

bool GetBoneWorldPosition(uint64_t aBoneHash, Math::Vec3& aOut)
{
    uintptr_t entity = 0;
    Vec3 model{};
    {
        std::scoped_lock lock(g_mutex);
        const auto it = std::find(g_boneNames.begin(), g_boneNames.end(), aBoneHash);
        if (it == g_boneNames.end())
        {
            return false;
        }
        const auto index = static_cast<size_t>(std::distance(g_boneNames.begin(), it));
        if (index >= g_positions.size())
        {
            return false;
        }
        model = g_positions[index];
        entity = g_entity;
    }

    Vec3 position{};
    Quat orientation{};
    if (!ReadEntityTransform(entity, position, orientation))
    {
        return false;
    }
    aOut = position + Math::Rotate(orientation, model);
    return true;
}

Math::Vec3 WorldToModelDirection(const Math::Vec3& aWorld)
{
    Vec3 position{};
    Quat orientation{};
    if (!GetEntityTransform(position, orientation))
    {
        return aWorld;
    }
    return Math::Rotate(Math::Conjugate(orientation), aWorld);
}

bool GetBoneModelPosition(uint64_t aBoneHash, Math::Vec3& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto it = std::find(g_boneNames.begin(), g_boneNames.end(), aBoneHash);
    if (it == g_boneNames.end())
    {
        return false;
    }
    const auto index = static_cast<size_t>(std::distance(g_boneNames.begin(), it));
    if (index >= g_positions.size())
    {
        return false;
    }
    aOut = g_positions[index];
    return true;
}

uintptr_t GetEntity()
{
    std::scoped_lock lock(g_mutex);
    return g_entity;
}
} // namespace APM::Pose::GizmoState
