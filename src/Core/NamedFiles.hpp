#pragma once

#include <algorithm>
#include <cctype>
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
