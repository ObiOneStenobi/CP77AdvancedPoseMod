#include "Core/CrashGuard.hpp"

#include "Core/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cwchar>

namespace
{
std::atomic<bool> g_shutdown{false};
std::atomic<int> g_logged{0};
constexpr int MaxLogged = 8; // first-chance exceptions the game handles itself could repeat
thread_local const char* t_stage = nullptr;
wchar_t g_crashLogPath[MAX_PATH]{};
uintptr_t g_moduleBase = 0;
uintptr_t g_moduleEnd = 0;

// Module + offset of an address (no allocation: this runs inside the exception handler).
void DescribeAddress(uintptr_t aAddress, char* aOut, size_t aSize)
{
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(aAddress), &module) ||
        !module)
    {
        std::snprintf(aOut, aSize, "0x%llX (no module)", static_cast<unsigned long long>(aAddress));
        return;
    }
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* name = std::wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    std::snprintf(aOut, aSize, "%ls+0x%llX", name,
                  static_cast<unsigned long long>(aAddress - reinterpret_cast<uintptr_t>(module)));
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* aInfo)
{
    const auto* record = aInfo ? aInfo->ExceptionRecord : nullptr;
    if (!record)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = record->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    const bool inPlugin = address >= g_moduleBase && address < g_moduleEnd;
    if ((!inPlugin && !t_stage) || g_logged.fetch_add(1) >= MaxLogged)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    char where[2 * MAX_PATH]{};
    DescribeAddress(address, where, sizeof(where));
    char access[96]{};
    if (code == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2)
    {
        const auto kind = record->ExceptionInformation[0];
        std::snprintf(access, sizeof(access), ", %s 0x%llX",
                      kind == 0 ? "reading" : (kind == 8 ? "executing" : "writing"),
                      static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    SYSTEMTIME time{};
    GetLocalTime(&time);
    char line[1024]{};
    const int length = std::snprintf(
        line, sizeof(line),
        "%04u-%02u-%02u %02u:%02u:%02u AdvancedPoseMod %s: exception 0x%08lX at %s%s, thread %lu, stage: %s%s%s\r\n",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, APM_VERSION_STRING, code, where,
        access, GetCurrentThreadId(), t_stage ? t_stage : "none", inPlugin ? " (in the plugin)" : "",
        g_shutdown.load() ? " (game exit)" : "");

    // Written straight to the file: the game may end before the RED4ext log is flushed.
    if (g_crashLogPath[0] && length > 0)
    {
        const HANDLE file = CreateFileW(g_crashLogPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(std::min<int>(length, sizeof(line) - 1)), &written, nullptr);
            FlushFileBuffers(file);
            CloseHandle(file);
        }
    }
    APM::Log::Error("%s", line);
    return EXCEPTION_CONTINUE_SEARCH; // the game (and its crash reporter) handle it as before
}
} // namespace

namespace APM::CrashGuard
{
void Install()
{
    HMODULE module = nullptr;
    // Pinned: never unloaded before the process ends (see the header).
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&Install), &module) &&
        module)
    {
        g_moduleBase = reinterpret_cast<uintptr_t>(module);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_moduleBase + dos->e_lfanew);
        g_moduleEnd = g_moduleBase + nt->OptionalHeader.SizeOfImage;
    }
    const auto path = (GetRuntime().pluginDir / L"crash.log").wstring();
    if (path.size() < MAX_PATH)
    {
        wcscpy_s(g_crashLogPath, path.c_str());
    }
    AddVectoredExceptionHandler(1, &OnException);
}

void BeginShutdown()
{
    g_shutdown = true;
}

bool ShuttingDown()
{
    return g_shutdown.load(std::memory_order_relaxed);
}

Stage::Stage(const char* aName)
    : m_previous(t_stage)
{
    t_stage = aName;
}

Stage::~Stage()
{
    t_stage = m_previous;
}
} // namespace APM::CrashGuard
