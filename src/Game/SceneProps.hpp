#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Scene props (.ent entity templates) that can be put in a hand: the lists of World Builder (entSpawner, a CET mod)
// when it is installed - the base game one (paths_ent.txt) and those added by prop packs. Read once (game thread).
namespace APM::SceneProps
{
bool Available();
size_t Count();
const std::string& Path(size_t aIndex); // empty if out of range
bool IsFromMod(size_t aIndex);          // listed by a prop pack, not by World Builder's base game list
// Indices of the props whose path contains every word of aQuery (case-insensitive), at most aMax, mods first.
std::vector<int32_t> Search(const std::string& aQuery, size_t aMax, bool aModsOnly);
} // namespace APM::SceneProps
