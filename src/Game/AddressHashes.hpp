#pragma once

#include <cstdint>

// Address hashes resolved by RED4ext through cyberpunk2077_addresses.json.
// Hashes are stable across game patches (addresses are not).
// Photo Mode hashes and structure offsets: credit to psiberx / PhotoModeEx (MIT license).
namespace APM::AddressHashes
{
constexpr uint32_t PhotoModeSystem_Activate = 2593396187;
constexpr uint32_t PhotoModeSystem_Deactivate = 3043759683;
constexpr uint32_t PhotoModeSystem_SpawnCharacter = 978529375;
constexpr uint32_t PhotoModeSystem_ApplyPuppetTransforms = 2694650124;
} // namespace APM::AddressHashes

// Offsets inside gamePhotoModeSystem (game 2.3x).
namespace APM::PhotoModeLayout
{
constexpr uintptr_t PlayerList = 0x130;    // DynArray<PhotoModeCharacter>
constexpr uintptr_t CharacterList = 0x180; // DynArray<PhotoModeCharacter> (NPCs, future use)
constexpr uintptr_t CharacterSize = 0x190; // sizeof(PhotoModeCharacter)
constexpr uintptr_t CharacterPuppet = 0x00; // Handle<gamePuppet>
constexpr uintptr_t CharacterType = 0x5C;   // 1 = PlayerFemale, 2 = PlayerMale, 4 = NPC
constexpr uintptr_t SceneSlots = 0x1D0;     // 3 pointers to the catalog entries of the characters added in the scene
} // namespace APM::PhotoModeLayout
