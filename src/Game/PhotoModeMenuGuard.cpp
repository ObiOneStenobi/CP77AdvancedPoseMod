#include "Game/PhotoModeMenuGuard.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Ui/PhotoModeUi.hpp"

#include <RED4ext/Detail/AddressHashes.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Relocation.hpp>

#include <array>
#include <cstring>

// Our menu items use attribute keys the game does not know (7700-7799). Their values are handled by the
// plugin, so they must not reach the game's Photo Mode system. gameuiPhotoModeMenuController::OnAttributeUpdated
// (native) parses its arguments and forwards them with `call Forward(this + 0x130, attribute, value, apply)`;
// we hook that internal call and drop our keys.
namespace
{
using Forward_t = void (*)(void* aCallback, uint32_t aAttribute, float aValue, bool aApply);

void* g_addrForward = nullptr;
Forward_t g_origForward = nullptr;

void Detour_Forward(void* aCallback, uint32_t aAttribute, float aValue, bool aApply)
{
    if (APM::Ui::PhotoModeUi::IsOwnAttribute(aAttribute))
    {
        return;
    }
    if (aAttribute == 15 || aAttribute == 74)
    {
        APM::Log::Verbose("Photo Mode look at camera: attribute %u = %.0f", aAttribute, aValue);
    }
    else if (aAttribute == 28)
    {
        APM::Log::Verbose("Photo Mode facial expression: %.0f", aValue);
        APM::Pose::EyelidFollow::SetExpression(static_cast<int32_t>(aValue + 0.5f));
    }
    else
    {
        // Research (debug log): which attributes the Photo Mode menu changes (e.g. the added characters' options).
        APM::Log::Verbose("Photo Mode attribute %u = %.3f", aAttribute, aValue);
    }
    APM::Pose::LookAtCamera::OnPhotoModeAttribute(aAttribute, aValue);
    g_origForward(aCallback, aAttribute, aValue, aApply);
}

using Handler_t = void (*)(void*, void*, void*, void*);

uintptr_t FindForwardCall(uintptr_t aHandler)
{
    // lea rcx, [rdi+130h]
    constexpr std::array<uint8_t, 7> leaPattern = {0x48, 0x8D, 0x8F, 0x30, 0x01, 0x00, 0x00};

    std::array<uint8_t, 0x140> code{};
    if (!APM::SafeMemory::ReadBytes(aHandler, code.data(), code.size()))
    {
        return 0;
    }

    for (size_t i = 0; i + leaPattern.size() < code.size(); ++i)
    {
        if (std::memcmp(&code[i], leaPattern.data(), leaPattern.size()) != 0)
        {
            continue;
        }
        for (size_t j = i + leaPattern.size(); j + 5 <= code.size() && j < i + 0x24; ++j)
        {
            if (code[j] == 0xE8)
            {
                int32_t rel = 0;
                std::memcpy(&rel, &code[j + 1], sizeof(rel));
                const uintptr_t target = aHandler + j + 5 + static_cast<intptr_t>(rel);
                return APM::SafeMemory::IsInGameImage(target) ? target : 0;
            }
        }
    }
    return 0;
}
} // namespace

namespace APM::PhotoModeMenuGuard
{
bool Install()
{
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* cls = rtti ? rtti->GetClass("gameuiPhotoModeMenuController") : nullptr;
    auto* func = cls ? cls->GetFunction("OnAttributeUpdated") : nullptr;
    if (!func)
    {
        Log::Warn("MenuGuard: OnAttributeUpdated not found; custom menu values will also reach the game");
        return false;
    }

    static RED4ext::UniversalRelocPtr<Handler_t*> handlers(RED4ext::Detail::AddressHashes::CBaseFunction_Handlers);
    const auto handler = reinterpret_cast<uintptr_t>(handlers.GetAddr()[func->GetRegIndex()]);
    const uintptr_t forward = SafeMemory::IsInGameImage(handler) ? FindForwardCall(handler) : 0;
    if (!forward)
    {
        Log::Warn("MenuGuard: forward call not found; custom menu values will also reach the game");
        return false;
    }

    g_addrForward = reinterpret_cast<void*>(forward);
    if (!AttachHook(g_addrForward, &Detour_Forward, &g_origForward))
    {
        Log::Warn("MenuGuard: failed to hook the forward call");
        g_addrForward = nullptr;
        return false;
    }

    const auto base = SafeMemory::GetGameImageBase();
    Log::Info("MenuGuard: OnAttributeUpdated=+0x%llX forward=+0x%llX hooked",
              static_cast<unsigned long long>(handler - base), static_cast<unsigned long long>(forward - base));
    return true;
}

void Uninstall()
{
    if (g_addrForward)
    {
        DetachHook(g_addrForward);
        g_addrForward = nullptr;
    }
}
} // namespace APM::PhotoModeMenuGuard
