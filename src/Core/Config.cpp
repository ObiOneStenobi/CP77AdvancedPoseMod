#include "Core/Config.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <Windows.h>

#include <cstdlib>
#include <iterator>
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

std::string ReadString(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey)
{
    wchar_t buffer[128]{};
    GetPrivateProfileStringW(aSection, aKey, L"", buffer, static_cast<DWORD>(std::size(buffer)), aFile.c_str());
    std::string out;
    for (const wchar_t* c = buffer; *c; ++c)
    {
        out.push_back(*c < 128 ? static_cast<char>(*c) : '?');
    }
    return out;
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
    v.faceGameSolver = ReadBool(file, L"Face", L"GameSolver", v.faceGameSolver);
    v.probeFaceTracks = ReadBool(file, L"Probe", L"FaceTracks", v.probeFaceTracks);
    v.probeFaceTrack = ReadString(file, L"Probe", L"FaceTrack");
    v.probeFaceTrackWeight = static_cast<float>(std::atof(ReadString(file, L"Probe", L"FaceTrackWeight").c_str()));
    v.debugLog = ReadBool(file, L"Debug", L"Log", v.debugLog);
    v.allowUnknownGameVersion = ReadBool(file, L"Compatibility", L"AllowUnknownGameVersion", v.allowUnknownGameVersion);
    Log::g_verbose = v.debugLog;

    {
        std::scoped_lock lock(g_mutex);
        g_values = v;
    }

    Log::Info("Config loaded: probe=%d budget=%u dumpRig=%d debugLog=%d allowUnknownGameVersion=%d",
              v.probeEnabled ? 1 : 0, v.probeScanBudget, v.probeDumpRig ? 1 : 0, v.debugLog ? 1 : 0,
              v.allowUnknownGameVersion ? 1 : 0);
    if (v.probeFaceTracks)
    {
        Log::Info("Config: face track probe on, track '%s' + %.2f", v.probeFaceTrack.c_str(), v.probeFaceTrackWeight);
    }
}

Values Get()
{
    std::scoped_lock lock(g_mutex);
    return g_values;
}
} // namespace APM::Config
