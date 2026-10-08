#include "Ui/TextEntry.hpp"

#include "Core/Log.hpp"

#include <Windows.h>

#include <atomic>
#include <mutex>
#include <set>

namespace
{
using APM::Ui::TextEntry::State;

constexpr size_t MaxLength = 40;

std::mutex g_mutex;
std::string g_text;
std::atomic<State> g_state{State::Inactive};
HWND g_window = nullptr;
std::atomic<int32_t> g_hotkey{0};
WNDPROC g_originalProc = nullptr;

// Characters a pose name (file name) may contain.
bool IsAllowed(wchar_t aChar)
{
    return (aChar >= L'a' && aChar <= L'z') || (aChar >= L'A' && aChar <= L'Z') || (aChar >= L'0' && aChar <= L'9') ||
           aChar == L' ' || aChar == L'-' || aChar == L'_';
}

void OnCharacter(wchar_t aChar)
{
    std::scoped_lock lock(g_mutex);
    if (aChar == L'\r')
    {
        g_state = State::Confirmed;
    }
    else if (aChar == 27) // Esc
    {
        g_state = State::Cancelled;
    }
    else if (aChar == L'\b')
    {
        if (!g_text.empty())
        {
            g_text.pop_back();
        }
    }
    else if (IsAllowed(aChar) && g_text.size() < MaxLength)
    {
        g_text.push_back(static_cast<char>(aChar));
    }
}

// Keys pressed while typing: their releases are kept from the game too, even after Enter / Esc (otherwise the game
// would see e.g. Esc being released and leave Photo Mode).
std::set<USHORT> g_keysDown;

bool IsTyping()
{
    return g_state == State::Typing;
}

// Raw keyboard input to keep from the game.
bool SwallowRawKeyboard(LPARAM aParam)
{
    RAWINPUT input{};
    UINT size = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(aParam), RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) ==
            static_cast<UINT>(-1) ||
        input.header.dwType != RIM_TYPEKEYBOARD)
    {
        return false;
    }
    const USHORT key = input.data.keyboard.VKey;
    const bool released = (input.data.keyboard.Flags & RI_KEY_BREAK) != 0;
    if (released)
    {
        return g_keysDown.erase(key) > 0 || IsTyping();
    }
    if (IsTyping())
    {
        g_keysDown.insert(key);
        return true;
    }
    return false;
}

LRESULT CALLBACK WindowProc(HWND aWindow, UINT aMessage, WPARAM aWParam, LPARAM aLParam)
{
    if (aMessage == WM_INPUT && (IsTyping() || !g_keysDown.empty()) && SwallowRawKeyboard(aLParam))
    {
        return DefWindowProcW(aWindow, aMessage, aWParam, aLParam);
    }
    // Undo / redo shortcuts (the keys still reach the game: Photo Mode does not use them).
    if (aMessage == WM_KEYDOWN && !IsTyping() && (aLParam & (1 << 30)) == 0 && (GetKeyState(VK_CONTROL) & 0x8000))
    {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (aWParam == 'Z')
        {
            g_hotkey = shift ? 2 : 1;
        }
        else if (aWParam == 'Y')
        {
            g_hotkey = 2;
        }
    }
    if (IsTyping() || !g_keysDown.empty())
    {
        switch (aMessage)
        {
        case WM_CHAR:
            if (IsTyping())
            {
                OnCharacter(static_cast<wchar_t>(aWParam));
            }
            return 0;
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_SYSCHAR:
        case WM_DEADCHAR:
            if (aMessage == WM_KEYDOWN && aWParam == VK_ESCAPE && IsTyping())
            {
                OnCharacter(27);
            }
            return 0;
        default:
            break;
        }
    }
    return CallWindowProcW(g_originalProc, aWindow, aMessage, aWParam, aLParam);
}

BOOL CALLBACK FindGameWindow(HWND aWindow, LPARAM aParam)
{
    DWORD process = 0;
    GetWindowThreadProcessId(aWindow, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(aWindow) || GetWindow(aWindow, GW_OWNER) != nullptr)
    {
        return TRUE;
    }
    auto& best = *reinterpret_cast<std::pair<HWND, LONG>*>(aParam);
    RECT rect{};
    GetClientRect(aWindow, &rect);
    const LONG area = (rect.right - rect.left) * (rect.bottom - rect.top);
    if (area > best.second)
    {
        best = {aWindow, area};
    }
    return TRUE;
}

bool InstallLocked()
{
    if (g_window && g_originalProc)
    {
        return true;
    }
    static uint64_t lastAttempt = 0;
    const uint64_t now = GetTickCount64();
    if (lastAttempt != 0 && now - lastAttempt < 2000)
    {
        return false;
    }
    lastAttempt = now;
    std::pair<HWND, LONG> best{nullptr, 0};
    EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&best));
    if (!best.first)
    {
        APM::Log::Warn("TextEntry: game window not found");
        return false;
    }
    g_window = best.first;
    g_originalProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&WindowProc)));
    if (!g_originalProc)
    {
        APM::Log::Warn("TextEntry: could not hook the game window");
        g_window = nullptr;
        return false;
    }
    return true;
}
} // namespace

namespace APM::Ui::TextEntry
{
bool Begin(const std::string& aText)
{
    {
        std::scoped_lock lock(g_mutex);
        if (!InstallLocked())
        {
            return false;
        }
        g_text.clear();
        for (const char ch : aText)
        {
            if (IsAllowed(static_cast<wchar_t>(static_cast<unsigned char>(ch))) && g_text.size() < MaxLength)
            {
                g_text.push_back(ch);
            }
        }
    }
    g_state = State::Typing;
    return true;
}

int32_t ConsumeHotkey()
{
    {
        std::scoped_lock lock(g_mutex);
        InstallLocked();
    }
    return g_hotkey.exchange(0);
}

void End()
{
    g_state = State::Inactive;
}

State GetState()
{
    return g_state;
}

std::string GetText()
{
    std::scoped_lock lock(g_mutex);
    return g_text;
}

void Shutdown()
{
    g_state = State::Inactive;
    std::scoped_lock lock(g_mutex);
    if (g_window && g_originalProc &&
        GetWindowLongPtrW(g_window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&WindowProc))
    {
        SetWindowLongPtrW(g_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalProc));
        g_window = nullptr;
        g_originalProc = nullptr;
    }
}
} // namespace APM::Ui::TextEntry
