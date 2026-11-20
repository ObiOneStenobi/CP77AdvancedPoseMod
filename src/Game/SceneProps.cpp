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
    bool excluded = false; // found to be a character when created
};

std::vector<Prop> g_props;
std::unordered_set<std::string> g_excluded; // lower case, backslashes
bool g_loaded = false;

std::filesystem::path ExcludedFile()
{
    return APM::GetRuntime().pluginDir / L"scene_props_excluded.txt";
}

// One path per line, blanks trimmed (any case, "/" or "\").
template<typename F>
void ReadLines(const std::filesystem::path& aFile, F&& aLine)
{
    std::ifstream file(aFile);
    std::string line;
    while (std::getline(file, line))
    {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
        {
            line.pop_back();
        }
        const size_t start = line.find_first_not_of(" \t");
        if (start != std::string::npos)
        {
            aLine(line.substr(start));
        }
    }
}

std::string Lower(std::string aText)
{
    std::transform(aText.begin(), aText.end(), aText.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return aText;
}

bool IsAllowedLower(const std::string& aLower)
{
    std::string path = aLower;
    std::replace(path.begin(), path.end(), '/', '\\');
    const size_t slash = path.find_last_of('\\');
    const std::string file = slash == std::string::npos ? path : path.substr(slash + 1);
    // Characters and their pieces (garments, heads, crowd...) are left out: they stand still in a T pose or as loose
    // parts (test 20/11), and the player's own crash the game.
    return path.find("characters\\") == std::string::npos && file.rfind("player_", 0) != 0 && !g_excluded.contains(path);
}

std::string Normalized(const std::string& aPath)
{
    std::string path = Lower(aPath);
    std::replace(path.begin(), path.end(), '/', '\\');
    return path;
}

void Load()
{
    if (g_loaded)
    {
        return;
    }
    g_loaded = true;
    std::error_code ec;
    ReadLines(ExcludedFile(), [](const std::string& aLine) { g_excluded.insert(Normalized(aLine)); });

    std::unordered_set<std::string> seen;
    const auto add = [&](const std::string& aLine, bool aMod)
    {
        const std::string lower = Lower(aLine);
        if (lower.size() < 5 || lower.substr(lower.size() - 4) != ".ent" || !IsAllowedLower(lower) ||
            !seen.insert(Normalized(lower)).second)
        {
            return false;
        }
        g_props.push_back({aLine, lower, aMod});
        return true;
    };

    // Ours first: the game's templates without characters.
    size_t ours = 0;
    const auto ourList = APM::GetRuntime().pluginDir / L"scene_props.txt";
    if (std::filesystem::is_regular_file(ourList, ec))
    {
        ReadLines(ourList, [&](const std::string& aLine) { ours += add(aLine, false) ? 1 : 0; });
    }
    else
    {
        APM::Log::Warn("Scene props: %s not found", ourList.string().c_str());
    }

    // Then World Builder's: the prop packs' (its base game list only without ours, it also names characters).
    const auto game = APM::GetRuntime().pluginDir.parent_path().parent_path().parent_path();
    const auto dir = game / L"bin" / L"x64" / L"plugins" / L"cyber_engine_tweaks" / L"mods" / L"entSpawner" / L"data" /
                     L"spawnables" / L"entity" / L"templates";
    size_t files = 0;
    size_t mods = 0;
    if (std::filesystem::is_directory(dir, ec))
    {
        std::vector<std::filesystem::path> lists;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
        {
            if (entry.is_regular_file(ec) && Lower(entry.path().extension().string()) == ".txt" &&
                (ours == 0 || Lower(entry.path().filename().string()) != "paths_ent.txt"))
            {
                lists.push_back(entry.path());
            }
        }
        // World Builder's base game list first, then the prop packs'.
        const auto isBase = [](const std::filesystem::path& aList)
        { return Lower(aList.filename().string()) == "paths_ent.txt"; };
        std::sort(lists.begin(), lists.end(), [&](const auto& a, const auto& b)
                  { return isBase(a) != isBase(b) ? isBase(a) : a.filename() < b.filename(); });
        for (const auto& list : lists)
        {
            const bool mod = !isBase(list);
            ++files;
            ReadLines(list, [&](const std::string& aLine) { mods += add(aLine, mod) && mod ? 1 : 0; });
        }
    }
    APM::Log::Info("Scene props: %zu (%zu from our list, %zu from World Builder's prop packs in %zu files, %zu left out "
                   "as characters)",
                   g_props.size(), ours, mods, files, g_excluded.size());
}
} // namespace

namespace APM::SceneProps
{
bool IsAllowed(const std::string& aPath)
{
    Load();
    return IsAllowedLower(Lower(aPath));
}

void Exclude(const std::string& aPath)
{
    Load();
    const std::string path = Normalized(aPath);
    if (path.empty() || !g_excluded.insert(path).second)
    {
        return;
    }
    for (auto& prop : g_props)
    {
        if (Normalized(prop.lower) == path)
        {
            prop.excluded = true;
        }
    }
    std::ofstream file(ExcludedFile(), std::ios::app);
    file << aPath << '\n';
    APM::Log::Info("Scene props: %s is a character, left out from now on (%s)", aPath.c_str(),
                   ExcludedFile().filename().string().c_str());
}

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
            if (prop.excluded || prop.mod != modPass || (aModsOnly && !prop.mod))
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
