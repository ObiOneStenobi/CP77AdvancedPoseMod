#include "Game/MeshBones.hpp"

#include "Core/Log.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/EngineLayout.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/ResourceLoader.hpp>
#include <RED4ext/Scripting/Natives/Generated/CMesh.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
// Meshes asked for once stay referenced here, so the next look finds them loaded (they are in use anyway).
std::unordered_map<uint64_t, RED4ext::SharedPtr<RED4ext::ResourceToken<RED4ext::CMesh>>> g_tokens;
} // namespace

namespace APM::MeshBones
{
bool Collect(uintptr_t aEntity, std::unordered_set<uint64_t>& aOut, bool& aComplete)
{
    aComplete = true;
    EngineLayout::ComponentList components;
    if (!aEntity || !EngineLayout::LooksAlive(aEntity) || !EngineLayout::GetComponents(aEntity, components))
    {
        return false;
    }
    auto* loader = RED4ext::ResourceLoader::Get();
    if (!loader)
    {
        aComplete = false;
        return true;
    }
    for (uint32_t c = 0; c < components.size; ++c)
    {
        const uintptr_t component = EngineLayout::GetComponent(components, c);
        auto* cls = component && EngineLayout::LooksAlive(component)
                        ? reinterpret_cast<RED4ext::ISerializable*>(component)->GetType()
                        : nullptr;
        auto* meshProp = cls ? cls->GetProperty("mesh") : nullptr;
        if (!meshProp)
        {
            continue;
        }
        uint64_t mesh = 0;
        SafeMemory::ReadU64(component + meshProp->valueOffset, mesh);
        uint64_t chunkMask = ~0ull;
        if (auto* prop = cls->GetProperty("chunkMask"))
        {
            SafeMemory::ReadU64(component + prop->valueOffset, chunkMask);
        }
        bool enabled = true;
        if (auto* prop = cls->GetProperty("isEnabled"))
        {
            uint8_t value = 1;
            SafeMemory::ReadBytes(component + prop->valueOffset, &value, 1);
            enabled = value != 0;
        }
        if (mesh == 0 || !enabled || chunkMask == 0)
        {
            continue; // shows nothing
        }
        auto& token = g_tokens[mesh];
        if (!token)
        {
            token = loader->LoadAsync<RED4ext::CMesh>(RED4ext::ResourcePath(mesh));
        }
        if (token && token->IsFailed())
        {
            continue; // not a mesh it can read (another kind of resource): nothing to wait for
        }
        if (!token || !token->IsLoaded())
        {
            aComplete = false;
            continue;
        }
        const auto& handle = token->Get();
        const auto* resource = handle.GetPtr();
        if (!resource)
        {
            aComplete = false;
            continue;
        }
        for (const auto& name : resource->boneNames)
        {
            aOut.insert(name.hash);
        }
    }
    return true;
}
} // namespace APM::MeshBones
