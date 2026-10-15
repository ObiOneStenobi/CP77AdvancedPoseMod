#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Game/AnimGraphHook.hpp"
#include "Game/PhotoModeMenuGuard.hpp"
#include "Game/PhotoModeTracker.hpp"
#include "Scripting/ScriptApi.hpp"
#include "Ui/TextEntry.hpp"

#include <RED4ext/RED4ext.hpp>

#include <Windows.h>

#include <algorithm>
#include <iterator>
#include <vector>

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

// Game version (file version of the executable), e.g. 3.0.80.51928 for 2.31.
bool ReadGameVersion(uint16_t (&aOut)[4])
{
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr, path, MAX_PATH))
    {
        return false;
    }
    DWORD unused = 0;
    const DWORD size = GetFileVersionInfoSizeW(path, &unused);
    if (size == 0)
    {
        return false;
    }
    std::vector<uint8_t> data(size);
    VS_FIXEDFILEINFO* info = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(path, 0, size, data.data()) ||
        !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info)
    {
        return false;
    }
    aOut[0] = HIWORD(info->dwFileVersionMS);
    aOut[1] = LOWORD(info->dwFileVersionMS);
    aOut[2] = HIWORD(info->dwFileVersionLS);
    aOut[3] = LOWORD(info->dwFileVersionLS);
    return true;
}

// The plugin loads on every game version (so its script natives always exist and the game can start), but it only
// hooks the game it was built for: structure offsets and functions change between versions. On another version the
// panel shows "Unsupported game version".
bool CheckGameVersion()
{
    constexpr uint16_t Supported[4] = {3, 0, 80, 51928}; // 2.31 (RED4EXT_V1_RUNTIME_VERSION_2_31)
    uint16_t version[4]{};
    if (!ReadGameVersion(version))
    {
        APM::Log::Error("Could not read the game version: Photo Mode hooks are off");
        return false;
    }
    if (!std::equal(std::begin(version), std::end(version), std::begin(Supported)))
    {
        APM::Log::Error("Game version %u.%u.%u.%u is not supported (built for 2.31 = 3.0.80.51928): Photo Mode hooks "
                        "are off. Update AdvancedPoseMod.",
                        version[0], version[1], version[2], version[3]);
        return false;
    }
    return true;
}

void OnPostRegisterTypes()
{
    // RTTI is ready here, so the animation node classes can be inspected and natives registered.
    if (APM::GetRuntime().gameSupported)
    {
        APM::GetRuntime().animationReady = APM::AnimGraphHook::Install();
        APM::PhotoModeMenuGuard::Install();
    }
    APM::ScriptApi::Register(); // always: the scripts declare these natives, the game would not start without them
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

        g_runtime.gameSupported = CheckGameVersion();
        g_runtime.trackerReady = g_runtime.gameSupported && APM::PhotoModeTracker::Install();
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
    // Loaded on every game version; the hooks check the version themselves (CheckGameVersion).
    aInfo->runtime = RED4EXT_V1_RUNTIME_VERSION_INDEPENDENT;
    aInfo->sdk = RED4EXT_V1_SDK_VERSION_CURRENT;
}

RED4EXT_C_EXPORT uint32_t RED4EXT_CALL Supports()
{
    return RED4EXT_API_VERSION_1;
}
