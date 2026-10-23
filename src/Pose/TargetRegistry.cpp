#include "Pose/TargetRegistry.hpp"
#include "Pose/Characters.hpp"
#include "Pose/PoseProcessor.hpp"

#include <RED4ext/CName.hpp>

#include <Windows.h>

#include <algorithm>
#include <memory>

namespace
{
// The lock only guards swapping the list and the pointer tables: readers take the current list (an immutable
// snapshot) and release the lock before working on it. Animation threads used to keep the lock in shared mode for
// the whole pose (VisitTarget) while the joint limits took it again; a writer waiting in between (Set, on every
// ApplyPuppetTransforms) then deadlocked the game (SRW locks must not be acquired recursively).
SRWLOCK g_lock = SRWLOCK_INIT;
std::shared_ptr<const std::vector<APM::Pose::Target>> g_targets = std::make_shared<const std::vector<APM::Pose::Target>>();

// Flat copy of all known pointers for lock-cheap scanning.
std::array<uintptr_t, 64> g_pointers{};
size_t g_pointerCount = 0;

// Entities and animated components only: these identify a target unambiguously (rigs are shared).
std::array<uintptr_t, 256> g_identities{}; // entities and animated components of up to 4 characters
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

std::shared_ptr<const std::vector<APM::Pose::Target>> Snapshot()
{
    SharedGuard guard;
    return g_targets;
}

const APM::Pose::Target* FindIn(const std::vector<APM::Pose::Target>& aTargets, uintptr_t aPointer)
{
    for (const auto& target : aTargets)
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
    auto targets = std::make_shared<const std::vector<Target>>(std::move(aTargets));
    ExclusiveGuard guard;
    g_targets = targets; // the previous list lives on while a reader still holds it

    g_pointerCount = 0;
    g_identityCount = 0;
    for (const auto& target : *g_targets)
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
    return Snapshot()->size();
}

bool FindByPointer(uintptr_t aPointer, Target& aOut)
{
    const auto targets = Snapshot();
    if (const auto* target = FindIn(*targets, aPointer))
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

RED4ext::Handle<RED4ext::ISerializable> LockObject(uintptr_t aPointer)
{
    if (!aPointer)
    {
        return {};
    }
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.entity == aPointer)
        {
            return target.entityRef.Lock();
        }
        if (target.animatedComponent == aPointer)
        {
            return target.componentRef.Lock();
        }
    }
    return {};
}

RED4ext::Handle<RED4ext::ISerializable> LockMainComponent(uint32_t aCharacter)
{
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.photoModeSlot == aCharacter && target.debugName == "root")
        {
            return target.componentRef.Lock();
        }
    }
    return {};
}

size_t GetProbePointers(std::array<uintptr_t, 64>& aOut)
{
    SharedGuard guard;
    aOut = g_pointers;
    return g_pointerCount;
}

int32_t FindBoneIndex(uintptr_t aPointer, uint64_t aBoneNameHash)
{
    const auto targets = Snapshot();
    const auto* target = FindIn(*targets, aPointer);
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
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.boneNames.size() == aCount)
        {
            return true;
        }
    }
    return false;
}

bool GetReferencePose(ReferencePose& aOut)
{
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.photoModeSlot != Characters::Selected() || target.debugName != "root")
        {
            continue;
        }
        const size_t count = target.boneNames.size();
        if (target.parents.size() < count || target.refRotationsLS.size() < count ||
            target.refTranslationsLS.size() < count || target.refRotationsMS.size() < count)
        {
            return false;
        }
        aOut.boneNames = target.boneNames;
        aOut.parents.assign(target.parents.begin(), target.parents.begin() + count);
        aOut.localRotations.assign(target.refRotationsLS.begin(), target.refRotationsLS.begin() + count);
        aOut.localTranslations.assign(target.refTranslationsLS.begin(), target.refTranslationsLS.begin() + count);
        aOut.modelRotations.assign(target.refRotationsMS.begin(), target.refRotationsMS.begin() + count);
        return true;
    }
    return false;
}

bool GetBoneReferenceForward(uint64_t aBoneHash, Math::Vec3& aOut)
{
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.photoModeSlot != Characters::Selected() || target.debugName != "root")
        {
            continue;
        }
        const auto it = std::find(target.boneNames.begin(), target.boneNames.end(), aBoneHash);
        if (it == target.boneNames.end())
        {
            return false;
        }
        const size_t index = static_cast<size_t>(std::distance(target.boneNames.begin(), it));
        if (index >= target.refRotationsMS.size())
        {
            return false;
        }
        const Math::Vec3 face = PoseProcessor::ReferenceFaceForward(target);
        aOut = Math::Normalize(Math::Rotate(Math::Conjugate(target.refRotationsMS[index]), face));
        return true;
    }
    return false;
}
} // namespace APM::Pose::TargetRegistry

namespace APM::Pose::TargetRegistry
{
bool VisitTarget(uintptr_t aPointer, const std::function<void(const Target&)>& aVisitor)
{
    const auto targets = Snapshot(); // no lock held while the visitor runs (it may call back into the registry)
    const auto* target = FindIn(*targets, aPointer);
    if (!target)
    {
        return false;
    }
    aVisitor(*target);
    return true;
}

void ForEach(const std::function<void(const Target&)>& aVisitor)
{
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        aVisitor(target);
    }
}

std::vector<uint64_t> GetMainSkeletonBones()
{
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.photoModeSlot == Characters::Selected() && target.debugName == "root")
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
    const auto targets = Snapshot();
    for (const auto& target : *targets)
    {
        if (target.photoModeSlot != Characters::Selected() || target.debugName != "root")
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
        bool leftSideways = false;
        if (az >= ax && az >= ay)
        {
            bend1 = right;   // vertical bone (spine, legs): bend forward/back
            bend2 = forward; // bend sideways
        }
        else if (ax >= ay)
        {
            bend1 = up;      // sideways bone (arms, fingers): swing forward/back
            bend2 = forward; // raise/lower
            leftSideways = Dot(twist, right) < 0.f;
        }
        else
        {
            bend1 = right; // forward bone (feet, eyes): tilt up/down
            bend2 = up;    // turn left/right
        }

        // Make both bend axes perpendicular to the twist axis (Gram-Schmidt).
        bend1 = Normalize(bend1 - twist * Dot(bend1, twist));
        bend2 = Normalize(Cross(twist, bend1));
        if (leftSideways)
        {
            // Left limbs point the other way: flip the swing axis so that a positive "forward / back" moves both
            // arms (and elbows, hands, fingers) the same way. The basis becomes left-handed; RotationToAngles
            // in the UI handles that.
            bend1 = bend1 * -1.f;
        }

        // Hands: "forward / back" is the wrist bending towards the palm / the back of the hand (up / down with the
        // palm down), "sideways" towards the thumb / the little finger - as for the feet.
        static const uint64_t leftHand = RED4ext::CName("LeftHand").hash;
        static const uint64_t rightHand = RED4ext::CName("RightHand").hash;
        if (aBoneHash == leftHand || aBoneHash == rightHand)
        {
            const Vec3 sideways = bend1;
            bend1 = bend2 * -1.f; // positive: towards the palm
            bend2 = sideways;
        }

        aBend1 = Rotate(toLocal, bend1);
        aBend2 = Rotate(toLocal, bend2);
        aTwist = twistLocal;
        return true;
    }
    return false;
}
} // namespace APM::Pose::TargetRegistry
