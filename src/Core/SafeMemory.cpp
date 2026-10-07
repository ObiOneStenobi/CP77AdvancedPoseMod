#include "Core/SafeMemory.hpp"

#include <Windows.h>

#include <cstring>

namespace
{
// These helpers must not contain objects with destructors (required for __try/__except).
bool GuardedCopy(uintptr_t aAddress, void* aOut, size_t aSize)
{
    __try
    {
        std::memcpy(aOut, reinterpret_cast<const void*>(aAddress), aSize);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

struct ImageRange
{
    uintptr_t begin = 0;
    uintptr_t end = 0;
};

ImageRange ResolveImageRange()
{
    ImageRange range;
    const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!base)
    {
        return range;
    }

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    range.begin = base;
    range.end = base + nt->OptionalHeader.SizeOfImage;
    return range;
}

const ImageRange& GetImageRange()
{
    static const ImageRange range = ResolveImageRange();
    return range;
}
} // namespace

namespace APM::SafeMemory
{
bool ReadU64(uintptr_t aAddress, uint64_t& aOut)
{
    if (!LooksLikePointer(aAddress))
    {
        return false;
    }
    return GuardedCopy(aAddress, &aOut, sizeof(aOut));
}

bool ReadU32(uintptr_t aAddress, uint32_t& aOut)
{
    if (aAddress < 0x10000 || aAddress >= 0x00007FFFFFFF0000ull)
    {
        return false;
    }
    return GuardedCopy(aAddress, &aOut, sizeof(aOut));
}

bool ReadBytes(uintptr_t aAddress, void* aOut, size_t aSize)
{
    if (aAddress < 0x10000 || aAddress >= 0x00007FFFFFFF0000ull)
    {
        return false;
    }
    return GuardedCopy(aAddress, aOut, aSize);
}

uintptr_t GetGameImageBase()
{
    return GetImageRange().begin;
}

uintptr_t GetGameImageEnd()
{
    return GetImageRange().end;
}
} // namespace APM::SafeMemory
