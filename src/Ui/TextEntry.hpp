#pragma once

#include <cstdint>
#include <string>

// Keyboard text entry for the panel (pose names). While it is active, the game window's keyboard messages are
// taken away from the game (so typing does not move the camera, take a photo or leave Photo Mode) and turned into
// text. Enter confirms, Esc cancels; the script polls the state and then calls End().
namespace APM::Ui::TextEntry
{
enum class State : int32_t
{
    Inactive = 0,
    Typing = 1,
    Confirmed = 2,
    Cancelled = 3,
};

bool Begin(const std::string& aText);
void End();
State GetState();
std::string GetText();

// Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z) pressed in the game window since the last call: 1 undo, 2 redo, 0 none.
// The first call hooks the game window.
int32_t ConsumeHotkey();

// Restores the game window's message handler (plugin unload).
void Shutdown();
} // namespace APM::Ui::TextEntry
