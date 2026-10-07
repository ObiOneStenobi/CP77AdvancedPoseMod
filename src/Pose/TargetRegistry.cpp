#include "Pose/TargetRegistry.hpp"

#include <Windows.h>

#include <algorithm>

namespace
{
SRWLOCK g_lock = SRWLOCK_INIT;
std::vector<APM::Pose::Target> g_targets;

// Flat copy of all known pointers for lock-cheap scanning.
std::array<uintptr_t, 64> g_pointers{};
size_t g_pointerCount = 0;

// Entities and animated components only: these identify a target unambiguously (rigs are shared).
std::array<uintptr_t, 64> g_identities{};
size_t g_identityCount = 0;

struct SharedGuard
{
    SharedGuard() { AcquireSRWLockShared(&g_lock); }
    ~SharedGuard() { ReleaseSRWLockShared(&g_lock); }
};

struct ExclusiveGuard
{
    ExclusiveGuard() { AcquireSRWLockExclusive(&g_lock); }
    ~ExclusiveGuard() { ReleaseSRWLockExclusive(&g_lock); }
};

const APM::Pose::Target* FindUnlocked(uintptr_t aPointer)
{
    for (const auto& target : g_targets)
    {
        if (target.entity == aPointer || target.animatedComponent == aPointer)
        {
            return &target;
        }
    }
    return nullptr;
}
} // namespace

namespace APM::Pose::TargetRegistry
{
void Set(std::vector<Target> aTargets)
{
    ExclusiveGuard guard;
    g_targets = std::move(aTargets);

    g_pointerCount = 0;
    g_identityCount = 0;
    for (const auto& target : g_targets)
    {
        for (const uintptr_t pointer : {target.entity, target.animatedComponent})
        {
            if (pointer && g_identityCount < g_identities.size() &&
                std::find(g_identities.begin(), g_identities.begin() + g_identityCount, pointer) ==
                    g_identities.begin() + g_identityCount)
            {
                g_identities[g_identityCount++] = pointer;
            }
        }

        for (const uintptr_t pointer : {target.entity, target.animatedComponent, target.rig})
        {
            if (!pointer || g_pointerCount >= g_pointers.size())
            {
                continue;
            }
            if (std::find(g_pointers.begin(), g_pointers.begin() + g_pointerCount, pointer) ==
                g_pointers.begin() + g_pointerCount)
            {
                g_pointers[g_pointerCount++] = pointer;
            }
        }
    }
}

void Clear()
{
    Set({});
}

size_t Count()
{
    SharedGuard guard;
    return g_targets.size();
}

bool FindByPointer(uintptr_t aPointer, Target& aOut)
{
    SharedGuard guard;
    if (const auto* target = FindUnlocked(aPointer))
    {
        aOut = *target;
        return true;
    }
    return false;
}

bool ContainsPointer(uintptr_t aPointer)
{
    if (!aPointer)
    {
        return false;
    }

    SharedGuard guard;
    for (size_t i = 0; i < g_identityCount; ++i)
    {
        if (g_identities[i] == aPointer)
        {
            return true;
        }
    }
    return false;
}

size_t GetProbePointers(std::array<uintptr_t, 64>& aOut)
{
    SharedGuard guard;
    aOut = g_pointers;
    return g_pointerCount;
}

int32_t FindBoneIndex(uintptr_t aPointer, uint64_t aBoneNameHash)
{
    SharedGuard guard;
    const auto* target = FindUnlocked(aPointer);
    if (!target)
    {
        return -1;
    }

    const auto it = std::find(target->boneNames.begin(), target->boneNames.end(), aBoneNameHash);
    if (it == target->boneNames.end())
    {
        return -1;
    }
    return static_cast<int32_t>(std::distance(target->boneNames.begin(), it));
}
} // namespace APM::Pose::TargetRegistry

namespace APM::Pose::TargetRegistry
{
bool HasBoneCount(uint32_t aCount)
{
    SharedGuard guard;
    for (const auto& target : g_targets)
    {
        if (target.boneNames.size() == aCount)
        {
            return true;
        }
    }
    return false;
}
} // namespace APM::Pose::TargetRegistry

namespace APM::Pose::TargetRegistry
{
bool VisitTarget(uintptr_t aPointer, const std::function<void(const Target&)>& aVisitor)
{
    SharedGuard guard;
    const auto* target = FindUnlocked(aPointer);
    if (!target)
    {
        return false;
    }
    aVisitor(*target);
    return true;
}

std::vector<uint64_t> GetMainSkeletonBones()
{
    SharedGuard guard;
    for (const auto& target : g_targets)
    {
        if (target.photoModeSlot == 0 && target.debugName == "root")
        {
            return target.boneNames;
        }
    }
    return {};
}
} // namespace APM::Pose::TargetRegistry
