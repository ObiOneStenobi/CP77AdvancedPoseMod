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

namespace APM::Pose::TargetRegistry
{
bool GetBoneAxes(uint64_t aBoneHash, Math::Vec3& aBend1, Math::Vec3& aBend2, Math::Vec3& aTwist)
{
    using namespace Math;
    SharedGuard guard;
    for (const auto& target : g_targets)
    {
        if (target.photoModeSlot != 0 || target.debugName != "root")
        {
            continue;
        }
        const auto it = std::find(target.boneNames.begin(), target.boneNames.end(), aBoneHash);
        if (it == target.boneNames.end())
        {
            return false;
        }
        const size_t index = static_cast<size_t>(std::distance(target.boneNames.begin(), it));
        if (index >= target.refRotationsMS.size() || index >= target.refTranslationsLS.size() ||
            index >= target.refRotationsLS.size())
        {
            return false;
        }

        // Twist axis (local): towards the first child, or continuing the parent->bone direction for leaf bones.
        Vec3 twistLocal{};
        for (size_t child = index + 1; child < target.parents.size(); ++child)
        {
            if (target.parents[child] == static_cast<int16_t>(index) && Length(target.refTranslationsLS[child]) > 1e-4f)
            {
                twistLocal = Normalize(target.refTranslationsLS[child]);
                break;
            }
        }
        if (Length(twistLocal) < 0.5f)
        {
            twistLocal = Normalize(Rotate(Conjugate(target.refRotationsLS[index]), target.refTranslationsLS[index]));
        }
        if (Length(twistLocal) < 0.5f)
        {
            twistLocal = {1.f, 0.f, 0.f};
        }

        // Work in model space: X right, Y forward, Z up.
        const Quat toModel = target.refRotationsMS[index];
        const Quat toLocal = Conjugate(toModel);
        const Vec3 twist = Normalize(Rotate(toModel, twistLocal));

        const Vec3 right{1.f, 0.f, 0.f};
        const Vec3 forward{0.f, 1.f, 0.f};
        const Vec3 up{0.f, 0.f, 1.f};
        const float ax = std::abs(Dot(twist, right));
        const float ay = std::abs(Dot(twist, forward));
        const float az = std::abs(Dot(twist, up));

        Vec3 bend1;
        Vec3 bend2;
        if (az >= ax && az >= ay)
        {
            bend1 = right;   // vertical bone (spine, legs): bend forward/back
            bend2 = forward; // bend sideways
        }
        else if (ax >= ay)
        {
            bend1 = up;      // sideways bone (arms, fingers): swing forward/back
            bend2 = forward; // raise/lower
        }
        else
        {
            bend1 = right; // forward bone (feet, eyes): tilt up/down
            bend2 = up;    // turn left/right
        }

        // Make both bend axes perpendicular to the twist axis (Gram-Schmidt).
        bend1 = Normalize(bend1 - twist * Dot(bend1, twist));
        bend2 = Normalize(Cross(twist, bend1));

        aBend1 = Rotate(toLocal, bend1);
        aBend2 = Rotate(toLocal, bend2);
        aTwist = twistLocal;
        return true;
    }
    return false;
}
} // namespace APM::Pose::TargetRegistry
