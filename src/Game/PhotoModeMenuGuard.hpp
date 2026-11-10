#pragma once

// Keeps the values of AdvancedPoseMod's own Photo Mode menu items away from the game's Photo Mode system.
namespace APM::PhotoModeMenuGuard
{
bool Install();
void Uninstall();
// Game thread, every frame: switches Photo Mode's own Look at camera off and on again in the game when a pose without
// it is needed (Pose/LookAtCamera.hpp).
void UpdateGameLookAt();
// Photo Mode ended: its menu is gone.
void ForgetMenu();
// Leaving Photo Mode was confirmed (or Photo Mode ended): nothing more is sent to the game's menu, and a pending
// "game's Look at camera off for a moment" is dropped.
void OnExitRequested();
} // namespace APM::PhotoModeMenuGuard
