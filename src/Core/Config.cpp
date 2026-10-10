#include "Core/Config.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <Windows.h>

#include <mutex>

namespace
{
std::mutex g_mutex;
APM::Config::Values g_values;

bool ReadBool(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey, bool aDefault)
{
    return GetPrivateProfileIntW(aSection, aKey, aDefault ? 1 : 0, aFile.c_str()) != 0;
}

uint32_t ReadUInt(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey, uint32_t aDefault)
{
    return static_cast<uint32_t>(GetPrivateProfileIntW(aSection, aKey, static_cast<INT>(aDefault), aFile.c_str()));
}
} // namespace

namespace APM::Config
{
void Load()
{
    const auto file = (GetRuntime().pluginDir / L"config.ini").wstring();

    Values v;
    v.probeEnabled = ReadBool(file, L"Probe", L"Enabled", v.probeEnabled);
    v.probeScanBudget = ReadUInt(file, L"Probe", L"ScanBudget", v.probeScanBudget);
    v.probeDumpRig = ReadBool(file, L"Probe", L"DumpRig", v.probeDumpRig);
    v.debugLog = ReadBool(file, L"Debug", L"Log", v.debugLog);
    Log::g_verbose = v.debugLog;

    {
        std::scoped_lock lock(g_mutex);
        g_values = v;
    }

    Log::Info("Config loaded: probe=%d budget=%u dumpRig=%d debugLog=%d", v.probeEnabled ? 1 : 0, v.probeScanBudget,
              v.probeDumpRig ? 1 : 0, v.debugLog ? 1 : 0);
}

Values Get()
{
    std::scoped_lock lock(g_mutex);
    return g_values;
}
} // namespace APM::Config
