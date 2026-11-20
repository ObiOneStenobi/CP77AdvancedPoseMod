#pragma once

#include <cstdint>

// Records of the base game (and Phantom Liberty), read from r6\cache\tweakdb.bin and tweakdb_ep1.bin, which mods never
// change (TweakXL adds its records at run time, REDmod writes r6\cache\modded). A record missing from them comes
// from a mod. Read once, on first use (game thread).
namespace APM::VanillaRecords
{
// False when the files could not be read: then no record counts as a mod's.
bool Available();
// aRecord: TweakDBID value (only its name hash and length are compared).
bool IsFromMod(uint64_t aRecord);
} // namespace APM::VanillaRecords
