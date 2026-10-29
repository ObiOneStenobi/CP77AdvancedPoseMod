#pragma once

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Libraries of named .json files (poses, expressions, hand poses). A name is the file's path relative to the
// library folder, without ".json", with '/' between folders: files shipped by other mods can live in their own
// subfolders ("Some mod/Sitting") so they do not overwrite each other. Names are UTF-8.
namespace APM::NamedFiles
{
inline std::filesystem::path FromUtf8(const std::string& aText)
{
    return std::filesystem::path(std::u8string(aText.begin(), aText.end()));
}

inline std::string ToUtf8(const std::filesystem::path& aPath)
{
    const auto text = aPath.generic_u8string();
    return std::string(text.begin(), text.end());
}

// Name for a new file written by the mod (always in the library's own folder): plain characters only.
inline std::string SanitizeNew(const std::string& aName)
{
    std::string out;
    for (const char ch : aName)
    {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '_' || ch == '-' || ch == ' ';
        out.push_back(ok ? ch : '_');
        if (out.size() >= 64)
        {
            break;
        }
    }
    while (!out.empty() && out.back() == ' ')
    {
        out.pop_back();
    }
    return out;
}

// Path of an existing file from its name, refusing anything that would leave the library folder.
inline bool Resolve(const std::filesystem::path& aDirectory, const std::string& aName, std::filesystem::path& aOut)
{
    if (aName.empty() || aName.size() > 400 || aName.front() == '/' || aName.back() == '/')
    {
        return false;
    }
    size_t start = 0;
    while (start <= aName.size())
    {
        const size_t end = std::min(aName.find('/', start), aName.size());
        const std::string part = aName.substr(start, end - start);
        if (part.empty() || part == "." || part == ".." || part.find_first_of("\\:*?\"<>|") != std::string::npos)
        {
            return false;
        }
        start = end + 1;
    }
    aOut = aDirectory / FromUtf8(aName + ".json");
    return true;
}

// Folder part of a name ("Some mod/" for "Some mod/Sitting", empty in the library's own folder).
inline std::string FolderOf(const std::string& aName)
{
    const size_t slash = aName.rfind('/');
    return slash == std::string::npos ? std::string() : aName.substr(0, slash + 1);
}

// Name of a file the mod writes: the folder part as given (an existing subfolder), its own name with plain
// characters only. "" if the folder is not valid.
inline std::string SanitizeInFolder(const std::filesystem::path& aDirectory, const std::string& aName)
{
    const std::string folder = FolderOf(aName);
    const std::string base = SanitizeNew(aName.substr(folder.size()));
    std::filesystem::path unused;
    if (base.empty() || !Resolve(aDirectory, folder + base, unused))
    {
        return {};
    }
    return folder + base;
}

// First free "<prefix> 001", "<prefix> 002"... in a folder of the library ("" = the library's own folder,
// otherwise "Some mod/" with the slash). "" if the folder is not valid or every number is taken.
inline std::string FreeName(const std::filesystem::path& aDirectory, const std::string& aFolder, const char* aPrefix)
{
    std::error_code ec;
    for (int number = 1; number < 10000; ++number)
    {
        char base[48]{};
        std::snprintf(base, sizeof(base), "%s %03d", aPrefix, number);
        std::filesystem::path path;
        if (!Resolve(aDirectory, aFolder + base, path))
        {
            return {};
        }
        if (!std::filesystem::exists(path, ec))
        {
            return aFolder + base;
        }
    }
    return {};
}

// Path of a folder of the library ("" or "Some mod/"), false if it would leave the library folder.
inline bool ResolveFolder(const std::filesystem::path& aDirectory, const std::string& aFolder, std::filesystem::path& aOut)
{
    if (aFolder.empty())
    {
        aOut = aDirectory;
        return true;
    }
    std::filesystem::path file;
    if (aFolder.back() != '/' || !Resolve(aDirectory, aFolder + "x", file))
    {
        return false;
    }
    aOut = file.parent_path();
    return true;
}

// Renames a file; it stays in its folder and only its own name changes (a folder typed in front of the new name is
// the file's own one). Returns the new name, made unique with " 2", " 3"... if taken, or "" on failure.
inline std::string Rename(const std::filesystem::path& aDirectory, const std::string& aOldName,
                          const std::string& aNewName)
{
    const std::string folder = FolderOf(aOldName);
    const bool typedFolder = !folder.empty() && aNewName.rfind(folder, 0) == 0;
    std::string base = SanitizeNew(typedFolder ? aNewName.substr(folder.size()) : aNewName);
    while (!base.empty() && base.front() == ' ')
    {
        base.erase(base.begin());
    }
    std::filesystem::path source;
    std::filesystem::path target;
    std::error_code ec;
    if (base.empty() || !Resolve(aDirectory, aOldName, source) || !std::filesystem::exists(source, ec))
    {
        return {};
    }
    std::string name = folder + base;
    if (name == aOldName)
    {
        return name;
    }
    for (int number = 2; Resolve(aDirectory, name, target) && std::filesystem::exists(target, ec); ++number)
    {
        // Same name in another case on a case-insensitive disk: renaming onto itself is fine.
        if (std::filesystem::equivalent(source, target, ec))
        {
            break;
        }
        if (number > 999)
        {
            return {};
        }
        name = folder + base + " " + std::to_string(number);
    }
    if (!Resolve(aDirectory, name, target))
    {
        return {};
    }
    std::filesystem::rename(source, target, ec);
    return ec ? std::string() : name;
}

// Every .json file of the library and its subfolders (a few levels deep): the library's own files first, then
// the subfolders, each sorted without regard to case.
inline std::vector<std::string> List(const std::filesystem::path& aDirectory, int aMaxDepth = 4)
{
    std::vector<std::string> names;
    std::error_code ec;
    auto it = std::filesystem::recursive_directory_iterator(
        aDirectory, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (it.depth() >= aMaxDepth)
        {
            it.disable_recursion_pending();
        }
        std::error_code fileError;
        if (!it->is_regular_file(fileError))
        {
            continue;
        }
        auto extension = ToUtf8(it->path().extension());
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char aChar) { return static_cast<char>(std::tolower(aChar)); });
        if (extension != ".json")
        {
            continue;
        }
        auto relative = it->path().lexically_relative(aDirectory);
        relative.replace_extension();
        names.push_back(ToUtf8(relative));
    }
    const auto lower = [](std::string aText)
    {
        std::transform(aText.begin(), aText.end(), aText.begin(),
                       [](unsigned char aChar) { return static_cast<char>(std::tolower(aChar)); });
        return aText;
    };
    std::sort(names.begin(), names.end(),
              [&](const std::string& a, const std::string& b)
              {
                  const bool aNested = a.find('/') != std::string::npos;
                  const bool bNested = b.find('/') != std::string::npos;
                  if (aNested != bNested)
                  {
                      return !aNested;
                  }
                  return lower(a) < lower(b);
              });
    return names;
}
} // namespace APM::NamedFiles
