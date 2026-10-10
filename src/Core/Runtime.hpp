#pragma once

#include <RED4ext/RED4ext.hpp>

#include <filesystem>

namespace APM
{
// Global plugin state shared by every module.
struct Runtime
{
    RED4ext::v1::PluginHandle handle = nullptr;
    const RED4ext::v1::Sdk* sdk = nullptr;
    std::filesystem::path pluginDir; // red4ext/plugins/AdvancedPoseMod
    bool trackerReady = false;       // Photo Mode hooks installed
    bool animationReady = false;     // animation hook installed (false after an incompatible game update)
};

Runtime& GetRuntime();

// Thin wrapper around the RED4ext hooking API.
template<typename TFunc>
bool AttachHook(void* aTarget, TFunc aDetour, TFunc* aOriginal)
{
    auto& rt = GetRuntime();
    if (!rt.sdk || !aTarget)
    {
        return false;
    }

    return rt.sdk->hooking->Attach(rt.handle, aTarget, reinterpret_cast<void*>(aDetour),
                                   reinterpret_cast<void**>(aOriginal));
}

inline bool DetachHook(void* aTarget)
{
    auto& rt = GetRuntime();
    if (!rt.sdk || !aTarget)
    {
        return false;
    }

    return rt.sdk->hooking->Detach(rt.handle, aTarget);
}
} // namespace APM
