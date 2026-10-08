#include "Game/PhotoModeTracker.hpp"

#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/AddressHashes.hpp"
#include "Game/AnimGraphHook.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/TextEntry.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/Rig.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimatedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/Entity.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

namespace
{
using Activate_t = bool (*)(void* aSystem);
using Deactivate_t = void (*)(void* aSystem);
using SpawnCharacter_t = void (*)(void* aSystem, void* aCharacter, const void* aSpawnTransform, uint64_t a4);
using ApplyPuppetTransforms_t = void (*)(void* aSystem, void* aCharacterList, uint8_t aCharacterGroup);

void* g_addrActivate = nullptr;
void* g_addrDeactivate = nullptr;
void* g_addrSpawnCharacter = nullptr;
void* g_addrApplyPuppetTransforms = nullptr;

Activate_t g_origActivate = nullptr;
Deactivate_t g_origDeactivate = nullptr;
SpawnCharacter_t g_origSpawnCharacter = nullptr;
ApplyPuppetTransforms_t g_origApplyPuppetTransforms = nullptr;

std::atomic<bool> g_active{false};
std::atomic<uint32_t> g_sessionId{0};
std::set<uint64_t> g_dumpedRigs; // game thread only

struct RawArray
{
    uintptr_t entries;
    uint32_t capacity;
    uint32_t size;
};

std::string CNameToString(RED4ext::CName aName)
{
    const char* text = aName.ToString();
    return text ? std::string(text) : std::string("<unknown>");
}

void DumpRig(const APM::Pose::Target& aTarget, RED4ext::anim::Rig* aRig)
{
    if (!aRig || g_dumpedRigs.contains(aRig->hash))
    {
        return;
    }
    g_dumpedRigs.insert(aRig->hash);

    std::error_code ec;
    const auto dir = APM::GetRuntime().pluginDir / L"probe";
    std::filesystem::create_directories(dir, ec);

    char fileName[64]{};
    std::snprintf(fileName, sizeof(fileName), "rig_%016llx.txt", static_cast<unsigned long long>(aRig->hash));
    std::ofstream out(dir / fileName, std::ios::trunc);
    if (!out)
    {
        APM::Log::Warn("Probe: cannot write %s", fileName);
        return;
    }

    out << "# AdvancedPoseMod rig dump\n";
    out << "# component: " << aTarget.debugName << "\n";
    out << "# bones: " << aRig->boneNames.Size() << "\n";
    out << "# index\tparent\tname\tref_ls_translation(x y z)\tref_ls_rotation(i j k r)\n";

    for (uint32_t i = 0; i < aRig->boneNames.Size(); ++i)
    {
        const int16_t parent = aRig->parentIndices ? aRig->parentIndices[i] : -2;
        out << i << '\t' << parent << '\t' << CNameToString(aRig->boneNames[i]);
        if (aRig->referencePoseLS)
        {
            const auto& t = aRig->referencePoseLS[i];
            out << '\t' << t.Translation.X << ' ' << t.Translation.Y << ' ' << t.Translation.Z;
            out << '\t' << t.Rotation.i << ' ' << t.Rotation.j << ' ' << t.Rotation.k << ' ' << t.Rotation.r;
        }
        out << '\n';
    }

    APM::Log::Info("Probe: rig with %u bones written to probe/%s", aRig->boneNames.Size(), fileName);
}

// Composes local reference rotations along the hierarchy (parents always precede children in CDPR rigs).
std::vector<APM::Math::Quat> ComputeModelSpaceRotations(RED4ext::anim::Rig* aRig)
{
    const uint32_t count = aRig->boneNames.Size();
    std::vector<APM::Math::Quat> result(count);
    if (!aRig->referencePoseLS || !aRig->parentIndices)
    {
        return result;
    }
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto& r = aRig->referencePoseLS[i].Rotation;
        const APM::Math::Quat local{r.i, r.j, r.k, r.r};
        const int16_t parent = aRig->parentIndices[i];
        result[i] = (parent >= 0 && static_cast<uint32_t>(parent) < i)
                        ? APM::Math::Normalize(APM::Math::Mul(result[parent], local))
                        : local;
    }
    return result;
}

// Bones edited on the main skeleton that drive a differently named bone on another skeleton of the same character.
struct BoneLinkRule
{
    const char* source;
    const char* target;
};
constexpr BoneLinkRule BoneLinkRules[] = {
    {"LeftEye", "l_J_eye_JNT"},
    {"RightEye", "r_J_eye_JNT"},
};

int32_t IndexOf(const APM::Pose::Target& aTarget, uint64_t aHash)
{
    for (size_t i = 0; i < aTarget.boneNames.size(); ++i)
    {
        if (aTarget.boneNames[i] == aHash)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void LinkBones(std::vector<APM::Pose::Target>& aTargets)
{
    using namespace APM::Math;
    for (auto& target : aTargets)
    {
        for (const auto& rule : BoneLinkRules)
        {
            const uint64_t sourceHash = RED4ext::CName(rule.source).hash;
            const int32_t dst = IndexOf(target, RED4ext::CName(rule.target).hash);
            if (dst < 0 || static_cast<size_t>(dst) >= target.refRotationsMS.size())
            {
                continue;
            }
            for (const auto& source : aTargets)
            {
                const int32_t src = IndexOf(source, sourceHash);
                if (source.entity != target.entity || src < 0 ||
                    static_cast<size_t>(src) >= source.refRotationsMS.size())
                {
                    continue;
                }
                const Quat a = source.refRotationsMS[src];
                const Quat b = target.refRotationsMS[dst];
                APM::Pose::LinkedBone link;
                link.sourceBone = sourceHash;
                link.targetIndex = static_cast<uint32_t>(dst);
                link.pre = Normalize(Mul(Conjugate(b), a));
                link.post = Normalize(Mul(Conjugate(a), b));
                target.linkedBones.push_back(link);
                break;
            }
        }
    }
}

void RefreshTargets(void* aSystem)
{
    using namespace APM;

    const auto system = reinterpret_cast<uintptr_t>(aSystem);
    RawArray list{};
    if (!SafeMemory::ReadBytes(system + PhotoModeLayout::PlayerList, &list, sizeof(list)))
    {
        Log::Warn("PhotoMode: cannot read the player list");
        return;
    }
    if (list.size == 0 || list.size > 8 || !SafeMemory::LooksLikePointer(list.entries))
    {
        Log::Warn("PhotoMode: unexpected player list (size=%u)", list.size);
        return;
    }

    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* animatedCls = rtti->GetClass("entAnimatedComponent");
    if (!animatedCls)
    {
        Log::Error("PhotoMode: entAnimatedComponent class not found");
        return;
    }

    const auto config = Config::Get();
    std::vector<Pose::Target> targets;

    for (uint32_t slot = 0; slot < list.size; ++slot)
    {
        const uintptr_t character = list.entries + slot * PhotoModeLayout::CharacterSize;

        uint64_t puppet = 0;
        uint32_t characterType = 0;
        SafeMemory::ReadU64(character + PhotoModeLayout::CharacterPuppet, puppet);
        SafeMemory::ReadU32(character + PhotoModeLayout::CharacterType, characterType);
        if (!puppet)
        {
            continue;
        }

        auto* entity = reinterpret_cast<RED4ext::ent::Entity*>(puppet);
        for (auto& handle : entity->components)
        {
            auto* component = handle.GetPtr();
            if (!component)
            {
                continue;
            }
            auto* cls = component->GetType();
            if (!cls || !cls->IsA(animatedCls))
            {
                continue;
            }

            auto* animated = reinterpret_cast<RED4ext::ent::AnimatedComponent*>(component);
            RED4ext::anim::Rig* rig = nullptr;
            if (animated->rig.IsLoaded())
            {
                rig = animated->rig.Get().GetPtr();
            }

            Pose::Target target;
            target.entity = puppet;
            target.animatedComponent = reinterpret_cast<uintptr_t>(animated);
            target.rig = reinterpret_cast<uintptr_t>(rig);
            target.photoModeSlot = slot;
            target.characterType = characterType;
            target.debugName = CNameToString(component->name);

            if (rig)
            {
                target.boneNames.reserve(rig->boneNames.Size());
                target.parents.reserve(rig->boneNames.Size());
                for (uint32_t i = 0; i < rig->boneNames.Size(); ++i)
                {
                    target.boneNames.push_back(rig->boneNames[i].hash);
                    target.parents.push_back(rig->parentIndices ? rig->parentIndices[i] : int16_t(-1));
                }
                target.refRotationsMS = ComputeModelSpaceRotations(rig);
                if (rig->referencePoseLS)
                {
                    for (uint32_t i = 0; i < rig->boneNames.Size(); ++i)
                    {
                        const auto& t = rig->referencePoseLS[i];
                        target.refRotationsLS.push_back({t.Rotation.i, t.Rotation.j, t.Rotation.k, t.Rotation.r});
                        target.refTranslationsLS.push_back({t.Translation.X, t.Translation.Y, t.Translation.Z});
                    }
                }
            }

            if (config.probeEnabled && config.probeDumpRig)
            {
                DumpRig(target, rig);
            }

            targets.push_back(std::move(target));
        }
    }

    LinkBones(targets);

    // ApplyPuppetTransforms runs often; only log when the set of targets actually changes.
    static std::vector<std::pair<uintptr_t, uintptr_t>> s_lastKeys;
    std::vector<std::pair<uintptr_t, uintptr_t>> keys;
    for (const auto& target : targets)
    {
        keys.emplace_back(target.entity, target.animatedComponent);
    }
    if (keys != s_lastKeys)
    {
        for (const auto& target : targets)
        {
            Log::Info("PhotoMode: target slot=%u type=%u entity=%p component=%p name=%s rig=%p bones=%zu",
                      target.photoModeSlot, target.characterType, reinterpret_cast<void*>(target.entity),
                      reinterpret_cast<void*>(target.animatedComponent), target.debugName.c_str(),
                      reinterpret_cast<void*>(target.rig), target.boneNames.size());
        }
        s_lastKeys = std::move(keys);
    }

    Pose::TargetRegistry::Set(std::move(targets));
}

bool Detour_Activate(void* aSystem)
{
    const bool result = g_origActivate(aSystem);

    APM::Config::Load();
    APM::AnimGraphHook::OnPhotoModeActivated();
    g_sessionId.fetch_add(1);
    g_active = true;
    APM::Log::Info("PhotoMode: activated (session %u)", g_sessionId.load());
    return result;
}

void Detour_Deactivate(void* aSystem)
{
    g_active = false;
    APM::Pose::TargetRegistry::Clear();
    APM::Pose::GizmoState::Clear();
    // The custom pose is kept in memory but must be re-enabled by the player next time.
    APM::Pose::PoseOverrides::SetEnabled(false);
    APM::Ui::TextEntry::End();
    APM::Log::Info("PhotoMode: deactivated");
    g_origDeactivate(aSystem);
}

void Detour_SpawnCharacter(void* aSystem, void* aCharacter, const void* aSpawnTransform, uint64_t a4)
{
    g_origSpawnCharacter(aSystem, aCharacter, aSpawnTransform, a4);
    if (g_active)
    {
        RefreshTargets(aSystem);
    }
}

void Detour_ApplyPuppetTransforms(void* aSystem, void* aCharacterList, uint8_t aCharacterGroup)
{
    g_origApplyPuppetTransforms(aSystem, aCharacterList, aCharacterGroup);
    if (g_active)
    {
        RefreshTargets(aSystem);
    }
}

void* Resolve(uint32_t aHash, const char* aName)
{
    const auto address = RED4ext::UniversalRelocBase::Resolve(aHash);
    if (!address)
    {
        APM::Log::Error("Address not found: %s (hash %u)", aName, aHash);
    }
    return reinterpret_cast<void*>(address);
}
} // namespace

namespace APM::PhotoModeTracker
{
bool Install()
{
    g_addrActivate = Resolve(AddressHashes::PhotoModeSystem_Activate, "PhotoModeSystem::Activate");
    g_addrDeactivate = Resolve(AddressHashes::PhotoModeSystem_Deactivate, "PhotoModeSystem::Deactivate");
    g_addrSpawnCharacter = Resolve(AddressHashes::PhotoModeSystem_SpawnCharacter, "PhotoModeSystem::SpawnCharacter");
    g_addrApplyPuppetTransforms =
        Resolve(AddressHashes::PhotoModeSystem_ApplyPuppetTransforms, "PhotoModeSystem::ApplyPuppetTransforms");

    bool ok = true;
    ok &= AttachHook(g_addrActivate, &Detour_Activate, &g_origActivate);
    ok &= AttachHook(g_addrDeactivate, &Detour_Deactivate, &g_origDeactivate);
    ok &= AttachHook(g_addrSpawnCharacter, &Detour_SpawnCharacter, &g_origSpawnCharacter);
    ok &= AttachHook(g_addrApplyPuppetTransforms, &Detour_ApplyPuppetTransforms, &g_origApplyPuppetTransforms);

    if (ok)
    {
        Log::Info("PhotoMode: hooks installed");
    }
    else
    {
        Log::Error("PhotoMode: failed to install one or more hooks");
    }
    return ok;
}

void Uninstall()
{
    DetachHook(g_addrActivate);
    DetachHook(g_addrDeactivate);
    DetachHook(g_addrSpawnCharacter);
    DetachHook(g_addrApplyPuppetTransforms);
}

bool IsActive()
{
    return g_active.load();
}

uint32_t GetSessionId()
{
    return g_sessionId.load();
}
} // namespace APM::PhotoModeTracker
