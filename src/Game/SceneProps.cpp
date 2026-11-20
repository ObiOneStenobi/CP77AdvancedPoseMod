#include "Game/SceneProps.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace
{
struct Prop
{
    std::string path;
    std::string lower; // for the search
    bool mod = false;
};

std::vector<Prop> g_props;
bool g_loaded = false;

std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return aText;
}

void Load()
{
    if (g_loaded)
    {
        return;
    }
    g_loaded = true;
    const auto game = APM::GetRuntime().pluginDir.parent_path().parent_path().parent_path();
    const auto dir = game / L"bin" / L"x64" / L"plugins" / L"cyber_engine_tweaks" / L"mods" / L"entSpawner" / L"data" /
                     L"spawnables" / L"entity" / L"templates";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
    {
        APM::Log::Info("Scene props: World Builder's lists not found (%s)", dir.string().c_str());
        return;
    }
    std::unordered_set<std::string> seen;
    size_t files = 0;
    // The base game list first, then the prop packs'.
    std::vector<std::filesystem::path> lists;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
    {
        if (entry.is_regular_file(ec) && Lower(entry.path().extension().string()) == ".txt")
        {
            lists.push_back(entry.path());
        }
    }
    std::sort(lists.begin(), lists.end(), [](const auto& a, const auto& b)
              { return (Lower(a.filename().string()) != "paths_ent.txt") < (Lower(b.filename().string()) != "paths_ent.txt") ||
                       ((Lower(a.filename().string()) != "paths_ent.txt") == (Lower(b.filename().string()) != "paths_ent.txt") &&
                        a.filename() < b.filename()); });
    size_t mods = 0;
    for (const auto& list : lists)
    {
        const bool mod = Lower(list.filename().string()) != "paths_ent.txt";
        std::ifstream file(list);
        std::string line;
        ++files;
        while (std::getline(file, line))
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            {
                line.pop_back();
            }
            const size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos)
            {
                continue;
            }
            line = line.substr(start);
            const std::string lower = Lower(line);
            if (lower.size() < 5 || lower.substr(lower.size() - 4) != ".ent" || !seen.insert(lower).second)
            {
                continue;
            }
            g_props.push_back({line, lower, mod});
            mods += mod ? 1 : 0;
        }
    }
    APM::Log::Info("Scene props: %zu from World Builder's lists (%zu files, %zu from prop packs)", g_props.size(), files,
                   mods);
}
} // namespace

namespace APM::SceneProps
{
bool Available()
{
    Load();
    return !g_props.empty();
}

size_t Count()
{
    Load();
    return g_props.size();
}

const std::string& Path(size_t aIndex)
{
    static const std::string empty;
    Load();
    return aIndex < g_props.size() ? g_props[aIndex].path : empty;
}

bool IsFromMod(size_t aIndex)
{
    Load();
    return aIndex < g_props.size() && g_props[aIndex].mod;
}

std::vector<int32_t> Search(const std::string& aQuery, size_t aMax, bool aModsOnly)
{
    Load();
    std::vector<std::string> words;
    std::istringstream stream(Lower(aQuery));
    for (std::string word; stream >> word;)
    {
        words.push_back(word);
    }
    std::vector<int32_t> result;
    for (const bool modPass : {true, false})
    {
        for (size_t i = 0; i < g_props.size() && result.size() < aMax; ++i)
        {
            const auto& prop = g_props[i];
            if (prop.mod != modPass || (aModsOnly && !prop.mod))
            {
                continue;
            }
            if (std::all_of(words.begin(), words.end(),
                            [&](const std::string& w) { return prop.lower.find(w) != std::string::npos; }))
            {
                result.push_back(static_cast<int32_t>(i));
            }
        }
    }
    return result;
}
} // namespace APM::SceneProps
