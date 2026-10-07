#pragma once

#include <cstdint>

// Guarded memory reads used by the research probe. Reading unknown engine structures can fault,
// so every speculative read goes through structured exception handling.
namespace APM::SafeMemory
{
// Cheap plausibility filter for user-mode heap/module pointers on x64 Windows.
inline bool LooksLikePointer(uintptr_t aValue)
{
    return aValue >= 0x10000 && aValue < 0x00007FFFFFFF0000ull && (aValue & 0x7) == 0;
}

bool ReadU64(uintptr_t aAddress, uint64_t& aOut);
bool ReadU32(uintptr_t aAddress, uint32_t& aOut);
bool ReadBytes(uintptr_t aAddress, void* aOut, size_t aSize);

// Image range of Cyberpunk2077.exe (used to classify vtable pointers).
uintptr_t GetGameImageBase();
uintptr_t GetGameImageEnd();
inline bool IsInGameImage(uintptr_t aValue)
{
    return aValue >= GetGameImageBase() && aValue < GetGameImageEnd();
}
} // namespace APM::SafeMemory
