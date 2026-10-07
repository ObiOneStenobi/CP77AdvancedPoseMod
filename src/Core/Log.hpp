#pragma once

#include "Core/Runtime.hpp"

// Logging goes to red4ext/logs/advancedposemod-<date>.log through the RED4ext logger.
// All messages are in English by design.
namespace APM::Log
{
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
