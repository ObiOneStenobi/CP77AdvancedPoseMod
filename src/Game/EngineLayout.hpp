#pragma once

#include <RED4ext/CName.hpp>
#include <RED4ext/Scripting/Natives/Generated/QsTransform.hpp>

#include <cstdint>

// Where engine objects keep the fields the plugin reads (entity components, a component's rig, a rig's bones).
// Resolved at runtime from the game's reflection where it has them (RTTI property offsets) and checked against
// the RED4ext.SDK layout, so a patch that moves them does not silently break the plugin. Fields that are not
// reflected (entity components, the rig's native arrays) are taken relative to reflected ones or validated
// against the objects themselves.
namespace APM::EngineLayout
{
// Call once RTTI is ready (OnPostRegisterTypes). Logs every offset that differs from the SDK.
void Resolve();

// Components of an entity (ent::Entity*), as handles; false if the array could not be found or looks wrong.
struct ComponentList
{
    uintptr_t entries = 0; // Handle<IComponent>[] (16 bytes each: instance, refcount)
    uint32_t size = 0;
};
bool GetComponents(uintptr_t aEntity, ComponentList& aOut);
uintptr_t GetComponent(const ComponentList& aList, uint32_t aIndex); // instance pointer, 0 if none

RED4ext::CName GetComponentName(uintptr_t aComponent);
// Loaded rig of an animated component (anim::Rig*), 0 if none / not loaded yet.
uintptr_t GetRig(uintptr_t aAnimatedComponent);

struct RigView
{
    uintptr_t rig = 0;
    uint32_t boneCount = 0;
    const RED4ext::CName* boneNames = nullptr;
    const int16_t* parents = nullptr;                 // may be null
    const RED4ext::QsTransform* referencePose = nullptr; // local space, may be null
    uint64_t hash = 0;
};
bool ViewRig(uintptr_t aRig, RigView& aOut);
} // namespace APM::EngineLayout
