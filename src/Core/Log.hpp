#pragma once

#include "Core/Runtime.hpp"

// Logging goes to red4ext/logs/advancedposemod-<date>.log through the RED4ext logger.
// All messages are in English by design.
#include <atomic>

namespace APM::Log
{
// Detailed diagnostics (config.ini [Debug] Log=1): off for players, so the log stays short.
inline std::atomic<bool> g_verbose{false};

template<typename... TArgs>
void Verbose(const char* aFormat, TArgs... aArgs)
{
    auto& rt = GetRuntime();
    if (g_verbose.load(std::memory_order_relaxed) && rt.sdk)
    {
        rt.sdk->logger->InfoF(rt.handle, aFormat, aArgs...);
    }
}

template<typename... TArgs>
void Info(const char* aFormat, TArgs... aArgs)
{
    auto& rt = GetRuntime();
    if (rt.sdk)
    {
        rt.sdk->logger->InfoF(rt.handle, aFormat, aArgs...);
    }
}

template<typename... TArgs>
void Warn(const char* aFormat, TArgs... aArgs)
{
    auto& rt = GetRuntime();
    if (rt.sdk)
    {
        rt.sdk->logger->WarnF(rt.handle, aFormat, aArgs...);
    }
}

template<typename... TArgs>
void Error(const char* aFormat, TArgs... aArgs)
{
    auto& rt = GetRuntime();
    if (rt.sdk)
    {
        rt.sdk->logger->ErrorF(rt.handle, aFormat, aArgs...);
    }
}

template<typename... TArgs>
void Debug(const char* aFormat, TArgs... aArgs)
{
    auto& rt = GetRuntime();
    if (rt.sdk)
    {
        rt.sdk->logger->DebugF(rt.handle, aFormat, aArgs...);
    }
}
} // namespace APM::Log
