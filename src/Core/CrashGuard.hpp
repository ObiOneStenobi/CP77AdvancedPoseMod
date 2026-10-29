#pragma once

#include "Core/Log.hpp"

#include <exception>

// Crash diagnostics and shutdown safety. A crash on a thread that is running the mod's code (a Stage), or inside the
// plugin itself, is written to red4ext/plugins/AdvancedPoseMod/crash.log (module + offset, thread, stage), so a user
// report tells which part of the mod was involved. The plugin is pinned in memory: at game exit, hooks and the window
// procedure that other threads may still be running never point to unloaded code.
namespace APM::CrashGuard
{
void Install();
// Unload: the detours only call the game's functions from now on.
void BeginShutdown();
bool ShuttingDown();

// The mod's code running on this thread, named in the crash log (a string literal).
class Stage
{
public:
    explicit Stage(const char* aName);
    ~Stage();
    Stage(const Stage&) = delete;
    Stage& operator=(const Stage&) = delete;

private:
    const char* m_previous;
};

// Runs aBody: a C++ exception is logged instead of unwinding into the game (which would end the process).
template<typename TBody>
void Guard(const char* aWhere, TBody&& aBody)
{
    try
    {
        Stage stage(aWhere);
        aBody();
    }
    catch (const std::exception& e)
    {
        Log::Error("%s: exception: %s", aWhere, e.what());
    }
    catch (...)
    {
        Log::Error("%s: unknown exception", aWhere);
    }
}
} // namespace APM::CrashGuard
