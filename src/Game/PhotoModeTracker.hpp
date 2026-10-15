#pragma once

#include <atomic>
#include <cstdint>

// Tracks Photo Mode activation and the puppets that live inside it.
namespace APM::PhotoModeTracker
{
bool Install();
void Uninstall();

bool IsActive();

// The player confirmed leaving Photo Mode: the edits stop being applied and the puppets are forgotten right away,
// before the game starts tearing Photo Mode down (Deactivate comes later).
void OnExitRequested();

// Puppet of a character (0 = V, 1-3 = added human characters), 0 if that character is not in the scene.
uint64_t GetCharacterPuppet(uint32_t aCharacter);

// Incremented every time Photo Mode opens; lets other modules reset per-session state.
uint32_t GetSessionId();
} // namespace APM::PhotoModeTracker
