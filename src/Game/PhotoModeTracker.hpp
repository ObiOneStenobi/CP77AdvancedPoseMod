#pragma once

#include <atomic>
#include <cstdint>

// Tracks Photo Mode activation and the puppets that live inside it.
namespace APM::PhotoModeTracker
{
bool Install();
void Uninstall();

bool IsActive();

// Incremented every time Photo Mode opens; lets other modules reset per-session state.
uint32_t GetSessionId();
} // namespace APM::PhotoModeTracker
