#include "Game/PhotoModeTracker.hpp"

#include "Core/Config.hpp"
#include "Core/Log.hpp"
#include "Pose/Characters.hpp"
#include "Pose/LookAtCamera.hpp"
#include "Core/Runtime.hpp"
#include "Core/SafeMemory.hpp"
#include "Game/AddressHashes.hpp"
#include "Game/AnimGraphHook.hpp"
#include "Pose/EyelidFollow.hpp"
#include "Pose/GizmoState.hpp"
#include "Pose/PoseOverrides.hpp"
#include "Pose/PoseProcessor.hpp"
#include "Pose/TargetRegistry.hpp"
#include "Ui/PhotoModeUi.hpp"
#include "Ui/TextEntry.hpp"

#include <RED4ext/RED4ext.hpp>
#include <RED4ext/Relocation.hpp>
#include <RED4ext/Scripting/Natives/Generated/anim/Rig.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/AnimatedComponent.hpp>
#include <RED4ext/Scripting/Natives/Generated/ent/Entity.hpp>

#include <algorithm>
#include <cstdio>
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
                if (static_cast<size_t>(src) < source.refRotationsLS.size() &&
                    static_cast<size_t>(dst) < target.refRotationsLS.size())
                {
                    link.sourceReferenceLocal = source.refRotationsLS[src];
                    link.targetReferenceLocal = target.refRotationsLS[dst];
                }
                target.linkedBones.push_back(link);
                break;
            }
        }
    }
}

// Research probe ([Debug] Log=1): where Photo Mode keeps the characters added to the scene and what they are made of.
// - On every spawn: which field or array of gamePhotoModeSystem holds the new character (by value, by pointer or as
//   a direct field), searched in the first 0x1000 bytes.
// - On every ApplyPuppetTransforms: the character list the game passes (once per list / size / group).
// - Once per puppet, when its model is loaded: the animated components of the characters of those lists and of the
//   characters spawned in this session (rigs written to probe\ with DumpRig=1).
std::set<uint64_t> g_probedPuppets;          // game thread only
std::vector<uintptr_t> g_spawnedCharacters;  // game thread only
std::set<std::string> g_probedLists;         // game thread only

void ProbeSpawn(void* aSystem, void* aCharacter)
{
    using namespace APM;
    if (!Log::g_verbose.load(std::memory_order_relaxed))
    {
        return;
    }
    const auto system = reinterpret_cast<uintptr_t>(aSystem);
    const auto character = reinterpret_cast<uintptr_t>(aCharacter);
    uint64_t puppet = 0;
    uint32_t type = 0;
    SafeMemory::ReadU64(character + PhotoModeLayout::CharacterPuppet, puppet);
    SafeMemory::ReadU32(character + PhotoModeLayout::CharacterType, type);
    Log::Verbose("CharacterProbe: spawn character=%p type=%u puppet=%p", aCharacter, type,
                 reinterpret_cast<void*>(puppet));
    if (std::find(g_spawnedCharacters.begin(), g_spawnedCharacters.end(), character) == g_spawnedCharacters.end())
    {
        g_spawnedCharacters.push_back(character);
    }
    bool found = false;
    for (uintptr_t offset = 0; offset < 0x1000; offset += 8)
    {
        uint64_t field = 0;
        if (!SafeMemory::ReadU64(system + offset, field))
        {
            continue;
        }
        if (field == character)
        {
            Log::Verbose("CharacterProbe: system+0x%llX points to it", static_cast<unsigned long long>(offset));
            found = true;
        }
        RawArray array{};
        if (!SafeMemory::ReadBytes(system + offset, &array, sizeof(array)) || !SafeMemory::LooksLikePointer(array.entries) ||
            array.size == 0 || array.size > 256 || array.capacity < array.size || array.capacity > 4096)
        {
            continue;
        }
        if (character >= array.entries && character < array.entries + array.size * 0x1000)
        {
            const uintptr_t distance = character - array.entries;
            Log::Verbose("CharacterProbe: inside the array at system+0x%llX (size %u), distance 0x%llX",
                         static_cast<unsigned long long>(offset), array.size, static_cast<unsigned long long>(distance));
            found = true;
        }
        for (uint32_t i = 0; i < array.size; ++i)
        {
            for (const uint32_t stride : {8u, 16u})
            {
                uint64_t element = 0;
                if (SafeMemory::ReadU64(array.entries + i * stride, element) && element == character)
                {
                    Log::Verbose("CharacterProbe: the array at system+0x%llX (size %u) holds a pointer to it at index "
                                 "%u (stride %u)",
                                 static_cast<unsigned long long>(offset), array.size, i, stride);
                    found = true;
                }
            }
        }
    }
    if (!found)
    {
        Log::Verbose("CharacterProbe: nothing in the first 0x1000 bytes of the system refers to this character");
    }
}

// Logs the animated components of a puppet (once per puppet). The puppet must look like a live entity.
void ProbePuppet(uint64_t aPuppet, const char* aWhere, uint32_t aType)
{
    using namespace APM;
    uint64_t vtable = 0;
    if (!aPuppet || !SafeMemory::LooksLikePointer(aPuppet) || !SafeMemory::ReadU64(aPuppet, vtable) ||
        !SafeMemory::IsInGameImage(vtable) || g_probedPuppets.contains(aPuppet))
    {
        return;
    }
    auto* rtti = RED4ext::CRTTISystem::Get();
    auto* animatedCls = rtti ? rtti->GetClass("entAnimatedComponent") : nullptr;
    auto* entityCls = rtti ? rtti->GetClass("entEntity") : nullptr;
    auto* entity = reinterpret_cast<RED4ext::ent::Entity*>(aPuppet);
    auto* entityType = entity->GetType();
    if (!animatedCls || !entityCls || !entityType || !entityType->IsA(entityCls))
    {
        return;
    }
    // Wait until the model is loaded: at spawn time the components and their rigs are not there yet.
    uint32_t loadedRigs = 0;
    for (auto& handle : entity->components)
    {
        auto* component = handle.GetPtr();
        auto* cls = component ? component->GetType() : nullptr;
        if (cls && cls->IsA(animatedCls) &&
            reinterpret_cast<RED4ext::ent::AnimatedComponent*>(component)->rig.IsLoaded())
        {
            ++loadedRigs;
        }
    }
    if (loadedRigs == 0)
    {
        return;
    }
    g_probedPuppets.insert(aPuppet);
    const auto config = Config::Get();
    Log::Verbose("CharacterProbe: %s type=%u puppet=%p class=%s components=%u", aWhere, aType,
                 reinterpret_cast<void*>(aPuppet), CNameToString(entityType->GetName()).c_str(),
                 entity->components.Size());
    for (auto& handle : entity->components)
    {
        auto* component = handle.GetPtr();
        auto* cls = component ? component->GetType() : nullptr;
        if (!cls || !cls->IsA(animatedCls))
        {
            continue;
        }
        auto* animated = reinterpret_cast<RED4ext::ent::AnimatedComponent*>(component);
        RED4ext::anim::Rig* rig = animated->rig.IsLoaded() ? animated->rig.Get().GetPtr() : nullptr;
        Log::Verbose("CharacterProbe:   component %s bones=%u rig=%016llx", CNameToString(component->name).c_str(),
                     rig ? rig->boneNames.Size() : 0u, rig ? static_cast<unsigned long long>(rig->hash) : 0ull);
        if (rig && config.probeEnabled && config.probeDumpRig)
        {
            APM::Pose::Target target;
            target.debugName = CNameToString(component->name);
            DumpRig(target, rig);
        }
    }
}

void ProbeList(uintptr_t aList, const char* aWhere)
{
    using namespace APM;
    RawArray list{};
    if (!SafeMemory::ReadBytes(aList, &list, sizeof(list)) || list.size > 64 ||
        (list.size && !SafeMemory::LooksLikePointer(list.entries)))
    {
        return;
    }
    for (uint32_t slot = 0; slot < list.size; ++slot)
    {
        const uintptr_t character = list.entries + slot * PhotoModeLayout::CharacterSize;
        uint64_t puppet = 0;
        uint32_t type = 0;
        SafeMemory::ReadU64(character + PhotoModeLayout::CharacterPuppet, puppet);
        SafeMemory::ReadU32(character + PhotoModeLayout::CharacterType, type);
        char where[96];
        std::snprintf(where, sizeof(where), "%s slot %u", aWhere, slot);
        ProbePuppet(puppet, where, type);
    }
}

void ProbeLists(void* aSystem, void* aCharacterList = nullptr, uint8_t aGroup = 0xFF)
{
    using namespace APM;
    if (!Log::g_verbose.load(std::memory_order_relaxed))
    {
        return;
    }
    const auto system = reinterpret_cast<uintptr_t>(aSystem);
    if (aCharacterList)
    {
        const auto list = reinterpret_cast<uintptr_t>(aCharacterList);
        RawArray array{};
        SafeMemory::ReadBytes(list, &array, sizeof(array));
        char key[128];
        const bool inside = list >= system && list < system + 0x1000;
        std::snprintf(key, sizeof(key), "%s0x%llX group %u size %u", inside ? "system+" : "",
                      static_cast<unsigned long long>(inside ? list - system : list), aGroup, array.size);
        if (g_probedLists.insert(key).second)
        {
            Log::Verbose("CharacterProbe: ApplyPuppetTransforms list %s (entries=%p capacity=%u)", key,
                         reinterpret_cast<void*>(array.entries), array.capacity);
        }
        ProbeList(list, "ApplyPuppetTransforms list");
    }
    ProbeList(system + PhotoModeLayout::PlayerList, "list system+0x130");
    ProbeList(system + PhotoModeLayout::CharacterList, "list system+0x180");
    for (const uintptr_t character : g_spawnedCharacters)
    {
        uint64_t puppet = 0;
        uint32_t type = 0;
        if (SafeMemory::ReadU32(character + PhotoModeLayout::CharacterType, type) && type == 4 &&
            SafeMemory::ReadU64(character + PhotoModeLayout::CharacterPuppet, puppet))
        {
            ProbePuppet(puppet, "spawned character", type);
        }
    }
}

// Adds the animated components of one Photo Mode puppet (aCharacter: 0 = V, 1-3 = added characters).
void AddPuppetTargets(std::vector<APM::Pose::Target>& targets, uint64_t puppet, uint32_t aCharacter, uint32_t aCharacterType,
                      RED4ext::CClass* animatedCls, const APM::Config::Values& config)
{
    using namespace APM;
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
        target.entityRef = entity->ref;
        target.componentRef = component->ref;
        target.rig = reinterpret_cast<uintptr_t>(rig);
        target.photoModeSlot = aCharacter;
        target.characterType = aCharacterType;
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

// An added character can be posed when its main skeleton is a human one, like V's ("root", 71 bones). Animals and
// objects have unrelated skeletons.
bool IsHumanCharacter(const std::vector<APM::Pose::Target>& aTargets, uint64_t aPuppet)
{
    static const uint64_t required[] = {RED4ext::CName("Hips").hash, RED4ext::CName("Spine3").hash,
                                        RED4ext::CName("Head").hash, RED4ext::CName("LeftHand").hash,
                                        RED4ext::CName("RightFoot").hash};
    for (const auto& target : aTargets)
    {
        if (target.entity != aPuppet || target.debugName != "root" || target.boneNames.size() != 71)
        {
            continue;
        }
        return std::all_of(std::begin(required), std::end(required), [&](uint64_t aHash) {
            return std::find(target.boneNames.begin(), target.boneNames.end(), aHash) != target.boneNames.end();
        });
    }
    return false;
}

// Puppet of each character last time (0 = none): a new puppet in a slot is a new character, whose pose starts empty.
std::array<uint64_t, APM::Pose::Characters::Count> g_characterPuppets{};

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
        if (puppet && slot == 0)
        {
            AddPuppetTargets(targets, puppet, 0, characterType, animatedCls, config); // V
        }
    }

    // Characters added in the scene: the three slots point to their catalog entries; only human ones are posed.
    std::array<uint64_t, Pose::Characters::Count> puppets{};
    puppets[0] = targets.empty() ? 0 : targets.front().entity;
    for (uint32_t i = 0; i + 1 < Pose::Characters::Count; ++i)
    {
        uint64_t entry = 0;
        uint64_t puppet = 0;
        uint32_t characterType = 0;
        if (!SafeMemory::ReadU64(system + PhotoModeLayout::SceneSlots + i * sizeof(uint64_t), entry) ||
            !SafeMemory::LooksLikePointer(entry) ||
            !SafeMemory::ReadU64(entry + PhotoModeLayout::CharacterPuppet, puppet) || !puppet ||
            !SafeMemory::ReadU32(entry + PhotoModeLayout::CharacterType, characterType) || characterType != 4)
        {
            continue;
        }
        std::vector<Pose::Target> added;
        AddPuppetTargets(added, puppet, i + 1, characterType, animatedCls, config);
        if (!IsHumanCharacter(added, puppet))
        {
            continue;
        }
        puppets[i + 1] = puppet;
        std::move(added.begin(), added.end(), std::back_inserter(targets));
    }
    for (uint32_t c = 1; c < Pose::Characters::Count; ++c)
    {
        if (puppets[c] != g_characterPuppets[c])
        {
            Pose::PoseOverrides::ClearCharacter(c);
            Pose::LookAtCamera::ResetCharacter(c);
            Pose::PoseProcessor::ResetCharacter(c);
            Log::Info("PhotoMode: character %u %s", c, puppets[c] ? "added (human, can be posed)" : "removed");
            if (!puppets[c] && Pose::Characters::Selected() == c)
            {
                Pose::PoseOverrides::SelectCharacter(0);
                Pose::GizmoState::Clear();
            }
        }
    }
    g_characterPuppets = puppets;

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
            Log::Verbose("PhotoMode: target slot=%u type=%u entity=%p component=%p name=%s rig=%p bones=%zu",
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
    // Every Photo Mode session starts from the game's pose: nothing from the previous session is kept.
    APM::Pose::PoseOverrides::ClearSession();
    APM::Ui::PhotoModeUi::ResetSession();
    APM::Pose::EyelidFollow::ResetSession();
    const bool result = g_origActivate(aSystem);

    APM::Config::Load();
    APM::Pose::LookAtCamera::Reset(); // the game starts every session with "Look at camera" off
    APM::Pose::LookAtCamera::ForgetCamera();
    APM::AnimGraphHook::OnPhotoModeActivated();
    g_probedPuppets.clear();
    g_spawnedCharacters.clear();
    g_characterPuppets = {};
    APM::Pose::PoseOverrides::SelectCharacter(0);
    g_probedLists.clear();
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
    // The edits are dropped: the next session starts from the game's pose.
    APM::Pose::PoseOverrides::ClearSession();
    APM::Ui::TextEntry::End();
    APM::Log::Info("PhotoMode: deactivated");
    g_origDeactivate(aSystem);
}

void Detour_SpawnCharacter(void* aSystem, void* aCharacter, const void* aSpawnTransform, uint64_t a4)
{
    g_origSpawnCharacter(aSystem, aCharacter, aSpawnTransform, a4);
    if (g_active)
    {
        ProbeSpawn(aSystem, aCharacter);
        RefreshTargets(aSystem);
        ProbeLists(aSystem);
    }
}

void Detour_ApplyPuppetTransforms(void* aSystem, void* aCharacterList, uint8_t aCharacterGroup)
{
    g_origApplyPuppetTransforms(aSystem, aCharacterList, aCharacterGroup);
    if (g_active)
    {
        RefreshTargets(aSystem);
        ProbeLists(aSystem, aCharacterList, aCharacterGroup);
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

uint64_t GetCharacterPuppet(uint32_t aCharacter)
{
    if (!g_active || aCharacter >= g_characterPuppets.size())
    {
        return 0;
    }
    return g_characterPuppets[aCharacter];
}

void OnExitRequested()
{
    if (!g_active.exchange(false))
    {
        return;
    }
    APM::Pose::TargetRegistry::Clear();
    APM::Pose::GizmoState::Clear();
    APM::Ui::TextEntry::End();
    APM::Log::Info("PhotoMode: leaving (edits no longer applied)");
}

uint32_t GetSessionId()
{
    return g_sessionId.load();
}
} // namespace APM::PhotoModeTracker
