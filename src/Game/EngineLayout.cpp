#include "Game/EngineLayout.hpp"

#include "Core/Log.hpp"
#include "Core/SafeMemory.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/ResourceReference.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/Rig.hpp>

#include <atomic>

namespace
{
// RED4ext.SDK layout (game 2.3x), used when the reflection does not have the field.
constexpr uint32_t SdkEntityComponents = 0xA0;
constexpr uint32_t SdkComponentName = 0x40;
constexpr uint32_t SdkAnimatedRig = 0x138;
constexpr uint32_t SdkRigBoneNames = 0x50;
constexpr uint32_t SdkRigTags = 0x120;
// Native rig fields, relative to boneNames (parentIndices and referencePoseLS sit right before it and share its
// size; the hash sits right before the tags).
constexpr int32_t RigParentsFromBoneNames = -0x10;
constexpr int32_t RigReferenceFromBoneNames = -0x08;
constexpr int32_t RigHashFromTags = -0x08;
// ISerializable::ref, the object's own weak reference (instance, refCount).
constexpr uint32_t SerializableRef = 0x08;

uint32_t g_componentName = SdkComponentName;
uint32_t g_animatedRig = SdkAnimatedRig;
uint32_t g_rigBoneNames = SdkRigBoneNames;
uint32_t g_rigTags = SdkRigTags;
std::atomic<uint32_t> g_entityComponents{SdkEntityComponents};
std::atomic<bool> g_componentsChecked{false};

RED4ext::CClass* g_componentClass = nullptr;
RED4ext::CClass* g_rigClass = nullptr;

uint32_t PropertyOffset(const char* aClass, const char* aProperty, uint32_t aSdk)
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass(aClass) : nullptr;
    auto* property = cls ? cls->GetProperty(aProperty) : nullptr;
    if (!property)
    {
        APM::Log::Warn("Layout: %s.%s not in the reflection, using the SDK offset 0x%X", aClass, aProperty, aSdk);
        return aSdk;
    }
    if (property->valueOffset != aSdk)
    {
        APM::Log::Warn("Layout: %s.%s at 0x%X (SDK: 0x%X)", aClass, aProperty, property->valueOffset, aSdk);
    }
    return property->valueOffset;
}

// An object of a reflected class (ISerializable with a vtable in the game image) that is a aClass.
bool IsInstanceOf(uintptr_t aObject, RED4ext::CClass* aClass)
{
    uint64_t vtable = 0;
    if (!aClass || !APM::SafeMemory::LooksLikePointer(aObject) || !APM::SafeMemory::ReadU64(aObject, vtable) ||
        !APM::SafeMemory::IsInGameImage(vtable))
    {
        return false;
    }
    auto* type = reinterpret_cast<RED4ext::ISerializable*>(aObject)->GetType();
    return type && type->IsA(aClass);
}

// A DynArray<Handle<IComponent>> at aEntity + aOffset: plausible header and component entries.
bool LooksLikeComponents(uintptr_t aEntity, uint32_t aOffset, APM::EngineLayout::ComponentList& aOut)
{
    uint64_t entries = 0;
    uint32_t capacity = 0;
    uint32_t size = 0;
    if (!APM::SafeMemory::ReadU64(aEntity + aOffset, entries) ||
        !APM::SafeMemory::ReadU32(aEntity + aOffset + 0x08, capacity) ||
        !APM::SafeMemory::ReadU32(aEntity + aOffset + 0x0C, size) || size == 0 || size > 4096 || capacity < size ||
        !APM::SafeMemory::LooksLikePointer(entries))
    {
        return false;
    }
    // A few entries must be components (all of them would cost too much on every call).
    const uint32_t checks = size < 4 ? size : 4;
    for (uint32_t i = 0; i < checks; ++i)
    {
        uint64_t instance = 0;
        if (!APM::SafeMemory::ReadU64(entries + i * 0x10, instance) || (instance && !IsInstanceOf(instance, g_componentClass)))
        {
            return false;
        }
    }
    aOut.entries = entries;
    aOut.size = size;
    return true;
}
} // namespace

namespace APM::EngineLayout
{
void Resolve()
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    g_componentClass = rtti ? rtti->GetClass("entIComponent") : nullptr;
    g_rigClass = rtti ? rtti->GetClass("animRig") : nullptr;
    g_componentName = PropertyOffset("entIComponent", "name", SdkComponentName);
    g_animatedRig = PropertyOffset("entAnimatedComponent", "rig", SdkAnimatedRig);
    g_rigBoneNames = PropertyOffset("animRig", "boneNames", SdkRigBoneNames);
    g_rigTags = PropertyOffset("animRig", "tags", SdkRigTags);
    Log::Info("Layout: component name +0x%X, rig +0x%X; rig bones +0x%X, tags +0x%X", g_componentName, g_animatedRig,
              g_rigBoneNames, g_rigTags);
}

bool GetComponents(uintptr_t aEntity, ComponentList& aOut)
{
    aOut = {};
    if (!SafeMemory::LooksLikePointer(aEntity))
    {
        return false;
    }
    const uint32_t offset = g_entityComponents.load();
    if (LooksLikeComponents(aEntity, offset, aOut))
    {
        g_componentsChecked = true;
        return true;
    }
    if (g_componentsChecked)
    {
        return false; // the offset worked before: this entity simply has none (yet)
    }
    // Not where the SDK has it (first entity seen): look for the array in the entity (once).
    for (uint32_t candidate = 0x40; candidate < 0x200; candidate += 8)
    {
        if (candidate != offset && LooksLikeComponents(aEntity, candidate, aOut))
        {
            g_entityComponents = candidate;
            g_componentsChecked = true;
            Log::Warn("Layout: entity components at +0x%X (SDK: 0x%X)", candidate, SdkEntityComponents);
            return true;
        }
    }
    return false;
}

uintptr_t GetComponent(const ComponentList& aList, uint32_t aIndex)
{
    uint64_t instance = 0;
    if (aIndex >= aList.size || !SafeMemory::ReadU64(aList.entries + aIndex * 0x10, instance))
    {
        return 0;
    }
    return static_cast<uintptr_t>(instance);
}

RED4ext::CName GetComponentName(uintptr_t aComponent)
{
    uint64_t hash = 0;
    SafeMemory::ReadU64(aComponent + g_componentName, hash);
    return RED4ext::CName(hash);
}

uintptr_t GetRig(uintptr_t aAnimatedComponent)
{
    if (!SafeMemory::LooksLikePointer(aAnimatedComponent))
    {
        return 0;
    }
    auto& reference = *reinterpret_cast<RED4ext::Ref<RED4ext::anim::Rig>*>(aAnimatedComponent + g_animatedRig);
    if (!reference.IsLoaded())
    {
        return 0;
    }
    const auto rig = reinterpret_cast<uintptr_t>(reference.Get().GetPtr());
    return IsInstanceOf(rig, g_rigClass) ? rig : 0;
}

bool ViewRig(uintptr_t aRig, RigView& aOut)
{
    aOut = {};
    uint64_t names = 0;
    uint32_t count = 0;
    if (!aRig || !SafeMemory::ReadU64(aRig + g_rigBoneNames, names) ||
        !SafeMemory::ReadU32(aRig + g_rigBoneNames + 0x0C, count) || count > 4096 ||
        (count > 0 && !SafeMemory::LooksLikePointer(names)))
    {
        return false;
    }
    uint64_t parents = 0;
    uint64_t reference = 0;
    uint64_t hash = 0;
    SafeMemory::ReadU64(aRig + g_rigBoneNames + RigParentsFromBoneNames, parents);
    SafeMemory::ReadU64(aRig + g_rigBoneNames + RigReferenceFromBoneNames, reference);
    SafeMemory::ReadU64(aRig + g_rigTags + RigHashFromTags, hash);
    aOut.rig = aRig;
    aOut.boneCount = count;
    aOut.boneNames = reinterpret_cast<const RED4ext::CName*>(names);
    aOut.parents = SafeMemory::LooksLikePointer(parents) ? reinterpret_cast<const int16_t*>(parents) : nullptr;
    aOut.referencePose =
        SafeMemory::LooksLikePointer(reference) ? reinterpret_cast<const RED4ext::QsTransform*>(reference) : nullptr;
    aOut.hash = hash;
    return true;
}

RED4ext::Handle<RED4ext::ISerializable> LockAlive(uintptr_t aObject)
{
    struct RawRef
    {
        uint64_t instance;
        uint64_t refCount;
    } raw{};
    uint64_t vtable = 0;
    uint32_t strongRefs = 0;
    if (!SafeMemory::LooksLikePointer(aObject) || !SafeMemory::ReadU64(aObject, vtable) ||
        !SafeMemory::IsInGameImage(vtable) || !SafeMemory::ReadBytes(aObject + SerializableRef, &raw, sizeof(raw)) ||
        raw.instance != aObject || !SafeMemory::LooksLikePointer(raw.refCount) ||
        !SafeMemory::ReadU32(raw.refCount, strongRefs) || strongRefs == 0)
    {
        return {};
    }
    // Borrows the weak reference without copying it (no weak count change) and takes a strong one only if the
    // object still has an owner (atomic compare-exchange): from here on it cannot be freed while the handle lives.
    static_assert(sizeof(RawRef) == sizeof(RED4ext::WeakHandle<RED4ext::ISerializable>));
    return reinterpret_cast<const RED4ext::WeakHandle<RED4ext::ISerializable>&>(raw).Lock();
}
} // namespace APM::EngineLayout
