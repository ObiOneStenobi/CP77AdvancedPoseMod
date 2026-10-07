#include "Core/Config.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <Windows.h>

#include <mutex>
#include <string>

namespace
{
std::mutex g_mutex;
APM::Config::Values g_values;

std::wstring ReadString(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey,
                        const wchar_t* aDefault)
{
    wchar_t buffer[256]{};
    GetPrivateProfileStringW(aSection, aKey, aDefault, buffer, static_cast<DWORD>(std::size(buffer)), aFile.c_str());
    return buffer;
}

bool ReadBool(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey, bool aDefault)
{
    return GetPrivateProfileIntW(aSection, aKey, aDefault ? 1 : 0, aFile.c_str()) != 0;
}

uint32_t ReadUInt(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey, uint32_t aDefault)
{
    return static_cast<uint32_t>(GetPrivateProfileIntW(aSection, aKey, static_cast<INT>(aDefault), aFile.c_str()));
}

float ReadFloat(const std::wstring& aFile, const wchar_t* aSection, const wchar_t* aKey, float aDefault)
{
    const auto text = ReadString(aFile, aSection, aKey, L"");
    if (text.empty())
    {
        return aDefault;
    }
    try
    {
        return std::stof(text);
    }
    catch (...)
    {
        return aDefault;
    }
}

std::string Narrow(const std::wstring& aText)
{
    std::string out;
    out.reserve(aText.size());
    for (const auto ch : aText)
    {
        out.push_back(ch < 0x80 ? static_cast<char>(ch) : '?');
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

    v.testEnabled = ReadBool(file, L"Test", L"Enabled", v.testEnabled);
    v.testBone = Narrow(ReadString(file, L"Test", L"Bone", L"Head"));
    v.testRotX = ReadFloat(file, L"Test", L"RotX", v.testRotX);
    v.testRotY = ReadFloat(file, L"Test", L"RotY", v.testRotY);
    v.testRotZ = ReadFloat(file, L"Test", L"RotZ", v.testRotZ);

    {
        std::scoped_lock lock(g_mutex);
        g_values = v;
    }

    Log::Info("Config loaded: probe=%d budget=%u dumpRig=%d test=%d bone=%s rot=(%.1f, %.1f, %.1f)",
              v.probeEnabled ? 1 : 0, v.probeScanBudget, v.probeDumpRig ? 1 : 0, v.testEnabled ? 1 : 0,
              v.testBone.c_str(), v.testRotX, v.testRotY, v.testRotZ);
}

Values Get()
{
    std::scoped_lock lock(g_mutex);
    return g_values;
}
} // namespace APM::Config
