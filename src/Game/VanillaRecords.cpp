#include "Game/VanillaRecords.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <cstring>
#include <fstream>
#include <unordered_set>
#include <vector>

namespace
{
// TweakDBID: name hash (32 bits) + name length (8 bits); the top 24 bits are a run-time offset.
constexpr uint64_t IdMask = 0xFFFFFFFFFFull;
constexpr uint32_t Magic = 0x0BB1DB47;

std::unordered_set<uint64_t> g_records;
bool g_loaded = false;
bool g_available = false;

// Header: magic, blob version, parser version, records checksum, then the offsets of the flats, records, queries and
// group tags sections. Records: int32 count, then count x (uint64 id, uint32 type hash).
size_t ReadFile(const std::filesystem::path& aPath)
{
    std::ifstream file(aPath, std::ios::binary);
    if (!file)
    {
        return 0;
    }
    std::vector<char> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    uint32_t header[8]{};
    if (data.size() < sizeof(header))
    {
        return 0;
    }
    std::memcpy(header, data.data(), sizeof(header));
    const size_t records = header[5];
    const size_t queries = header[6];
    if (header[0] != Magic || records + 4 > data.size() || queries > data.size())
    {
        return 0;
    }
    int32_t count = 0;
    std::memcpy(&count, data.data() + records, 4);
    if (count <= 0 || records + 4 + static_cast<size_t>(count) * 12 > data.size() ||
        records + 4 + static_cast<size_t>(count) * 12 != queries)
    {
        return 0;
    }
    for (int32_t i = 0; i < count; ++i)
    {
        uint64_t id = 0;
        std::memcpy(&id, data.data() + records + 4 + static_cast<size_t>(i) * 12, 8);
        g_records.insert(id & IdMask);
    }
    return static_cast<size_t>(count);
}

void Load()
{
    if (g_loaded)
    {
        return;
    }
    g_loaded = true;
    const auto cache = APM::GetRuntime().pluginDir.parent_path().parent_path().parent_path() / L"r6" / L"cache";
    const size_t base = ReadFile(cache / L"tweakdb.bin");
    const size_t ep1 = ReadFile(cache / L"tweakdb_ep1.bin");
    g_available = base > 0;
    APM::Log::Info("Base game records: %zu (tweakdb.bin) + %zu (tweakdb_ep1.bin)%s", base, ep1,
                   g_available ? "" : ": not readable, items from mods cannot be told apart");
}
} // namespace

namespace APM::VanillaRecords
{
bool Available()
{
    Load();
    return g_available;
}

bool IsFromMod(uint64_t aRecord)
{
    Load();
    return g_available && !g_records.contains(aRecord & IdMask);
}
} // namespace APM::VanillaRecords
