#include "Game/PhotoModeMenuGuard.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/FacePoses.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Ui/PhotoModeUi.hpp"

#include <RED4ext/Detail/AddressHashes.hpp>
#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Relocation.hpp>

#include <array>
#include <cstring>

// Our menu items use attribute keys the game does not know (7700-7999). Their values are handled by the
// plugin, so they must not reach the game's Photo Mode system. gameuiPhotoModeMenuController::OnAttributeUpdated
// (native) parses its arguments and forwards them with `call Forward(this + 0x130, attribute, value, apply)`
// (+0x140 since 2.5);
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
        APM::Pose::EyelidFollow::SetExpression(0, static_cast<int32_t>(aValue + 0.5f));
        APM::Pose::FacePoses::OnExpressionChanged(0, static_cast<int32_t>(aValue + 0.5f));
    }
    else if (aAttribute == 56)
    {
        // Facial expression of the character selected in the NPC page (attribute 68).
        const int32_t npc = APM::Pose::LookAtCamera::g_selectedNpc.load();
        APM::Log::Verbose("Photo Mode facial expression of added character %d: %.0f", npc + 1, aValue);
        if (npc >= 0 && npc < 3)
        {
            APM::Pose::EyelidFollow::SetExpression(static_cast<uint32_t>(npc) + 1, static_cast<int32_t>(aValue + 0.5f));
            APM::Pose::FacePoses::OnExpressionChanged(static_cast<uint32_t>(npc) + 1,
                                                      static_cast<int32_t>(aValue + 0.5f));
        }
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
    // lea rcx, [rdi+disp32] with a small displacement (0x130 in 2.31, 0x140 in 2.5)
    constexpr std::array<uint8_t, 3> leaPattern = {0x48, 0x8D, 0x8F};

    std::array<uint8_t, 0x140> code{};
    if (!APM::SafeMemory::ReadBytes(aHandler, code.data(), code.size()))
    {
        return 0;
    }

    for (size_t i = 0; i + 7 < code.size(); ++i)
    {
        if (std::memcmp(&code[i], leaPattern.data(), leaPattern.size()) != 0 || code[i + 5] != 0 || code[i + 6] != 0 ||
            code[i + 4] != 0x01)
        {
            continue;
        }
        for (size_t j = i + 7; j + 5 <= code.size() && j < i + 0x24; ++j)
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
