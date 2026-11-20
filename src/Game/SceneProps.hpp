#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Scene props (.ent entity templates) that can be put in a hand: our list of the game's templates without characters
// (scene_props.txt in the plugin folder, made by tools\scene_props_list.ps1), then the prop packs' lists of World
// Builder (entSpawner, a CET mod) when it is installed; World Builder's base game list (paths_ent.txt) only without
// ours. Read once (game thread).
namespace APM::SceneProps
{
bool Available();
// False for characters and their pieces (any "characters\" folder, files "player_*", templates found to be characters
// in the game): a second player crashed the game (test 20/11), other characters stood still and their pieces were
// loose parts.
bool IsAllowed(const std::string& aPath);
// A template that turned out to be a character when created (prop packs' lists, typed paths): never offered again
// (remembered in scene_props_excluded.txt).
void Exclude(const std::string& aPath);
size_t Count();
const std::string& Path(size_t aIndex); // empty if out of range
bool IsFromMod(size_t aIndex);          // listed by a prop pack, not by World Builder's base game list
// Indices of the props whose path contains every word of aQuery (case-insensitive), at most aMax, mods first.
std::vector<int32_t> Search(const std::string& aQuery, size_t aMax, bool aModsOnly);
} // namespace APM::SceneProps
