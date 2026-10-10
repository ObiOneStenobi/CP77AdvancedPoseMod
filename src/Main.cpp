#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Game/AnimGraphHook.hpp"
#include "Game/PhotoModeMenuGuard.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Scripting/ScriptApi.hpp"
#include "Ui/TextEntry.hpp"

#include <RED4ext/RED4ext.hpp>

#include <Windows.h>

namespace
{
APM::Runtime g_runtime;

std::filesystem::path ResolvePluginDir()
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&ResolvePluginDir), &module))
    {
        return {};
    }

    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void OnPostRegisterTypes()
{
    // RTTI is ready here, so the animation node classes can be inspected and natives registered.
    APM::GetRuntime().animationReady = APM::AnimGraphHook::Install();
    APM::PhotoModeMenuGuard::Install();
    APM::ScriptApi::Register();
}
} // namespace

APM::Runtime& APM::GetRuntime()
{
    return g_runtime;
}

RED4EXT_C_EXPORT bool RED4EXT_CALL Main(RED4ext::v1::PluginHandle aHandle, RED4ext::v1::EMainReason aReason,
                                        const RED4ext::v1::Sdk* aSdk)
{
    switch (aReason)
    {
    case RED4ext::v1::EMainReason::Load:
    {
        g_runtime.handle = aHandle;
        g_runtime.sdk = aSdk;
        g_runtime.pluginDir = ResolvePluginDir();

        APM::Log::Info("AdvancedPoseMod %s is loading", APM_VERSION_STRING);

        g_runtime.trackerReady = APM::PhotoModeTracker::Install();
        RED4ext::CRTTISystem::Get()->AddPostRegisterCallback(&OnPostRegisterTypes);
        break;
    }
    case RED4ext::v1::EMainReason::Unload:
    {
        APM::Ui::TextEntry::Shutdown();
        APM::AnimGraphHook::Uninstall();
        APM::PhotoModeMenuGuard::Uninstall();
        APM::PhotoModeTracker::Uninstall();
        APM::Log::Info("AdvancedPoseMod unloaded");
        break;
    }
    }

    return true;
}

RED4EXT_C_EXPORT void RED4EXT_CALL Query(RED4ext::v1::PluginInfo* aInfo)
{
    aInfo->name = L"AdvancedPoseMod";
    aInfo->author = L"ObiOneStenobi";
    aInfo->version = RED4EXT_V1_SEMVER(APM_VERSION_MAJOR, APM_VERSION_MINOR, APM_VERSION_PATCH);
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_LATEST;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
