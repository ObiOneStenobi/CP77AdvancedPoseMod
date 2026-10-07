#include "Pose/PoseOverrides.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <mutex>

namespace
{
constexpr const char* PoseFormat = "AdvancedPoseMod.Pose";
constexpr int PoseFormatVersion = 1;

std::mutex g_mutex; // guards g_bones and g_snapshot replacement
std::vector<APM::Pose::BoneOverride> g_bones;
std::mutex g_snapshotMutex; // guards g_snapshot pointer only (held for a pointer copy)
std::shared_ptr<const APM::Pose::OverrideSnapshot> g_snapshot = std::make_shared<APM::Pose::OverrideSnapshot>();

uint64_t HashBone(const std::string& aBone)
{
    return RED4ext::CName(aBone.c_str()).hash;
}

// Must be called with g_mutex held.
void PublishLocked()
{
    auto snapshot = std::make_shared<APM::Pose::OverrideSnapshot>();
    for (const auto& bone : g_bones)
    {
        if (bone.hasRotation || bone.hasTranslation)
        {
            snapshot->bones.push_back(bone);
        }
    }
    std::scoped_lock lock(g_snapshotMutex);
    g_snapshot = std::move(snapshot);
}

// Must be called with g_mutex held.
APM::Pose::BoneOverride& FindOrAddLocked(const std::string& aBone)
{
    const uint64_t hash = HashBone(aBone);
    auto it = std::find_if(g_bones.begin(), g_bones.end(), [hash](const auto& b) { return b.boneHash == hash; });
    if (it != g_bones.end())
    {
        return *it;
    }

    APM::Pose::BoneOverride bone;
    bone.boneHash = hash;
    bone.boneName = aBone;
    g_bones.push_back(bone);
    return g_bones.back();
}

const APM::Pose::BoneOverride* FindLocked(const std::string& aBone)
{
    const uint64_t hash = HashBone(aBone);
    auto it = std::find_if(g_bones.begin(), g_bones.end(), [hash](const auto& b) { return b.boneHash == hash; });
    return it != g_bones.end() ? &*it : nullptr;
}

bool IsZero(const std::array<float, 3>& aValues)
{
    return aValues[0] == 0.f && aValues[1] == 0.f && aValues[2] == 0.f;
}

// Keeps file names portable and prevents path traversal.
std::string SanitizeName(const std::string& aName)
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
} // namespace

namespace APM::Pose::PoseOverrides
{
std::shared_ptr<const OverrideSnapshot> GetSnapshot()
{
    std::scoped_lock lock(g_snapshotMutex);
    return g_snapshot;
}

void SetRotation(const std::string& aBone, float aX, float aY, float aZ)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.rotationEuler = {aX, aY, aZ};
    bone.rotation = Math::FromEulerXYZDegrees(aX, aY, aZ);
    bone.hasRotation = !IsZero(bone.rotationEuler);
    PublishLocked();
}

void SetTranslation(const std::string& aBone, float aX, float aY, float aZ)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.translation = {aX, aY, aZ};
    bone.hasTranslation = !IsZero(bone.translation);
    PublishLocked();
}

bool GetRotation(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    aOut = (bone && bone->hasRotation) ? bone->rotationEuler : std::array<float, 3>{};
    return bone && bone->hasRotation;
}

bool GetTranslation(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    aOut = (bone && bone->hasTranslation) ? bone->translation : std::array<float, 3>{};
    return bone && bone->hasTranslation;
}

void ResetBone(const std::string& aBone)
{
    std::scoped_lock lock(g_mutex);
    const uint64_t hash = HashBone(aBone);
    std::erase_if(g_bones, [hash](const auto& b) { return b.boneHash == hash; });
    PublishLocked();
}

void ResetAll()
{
    std::scoped_lock lock(g_mutex);
    g_bones.clear();
    PublishLocked();
}

size_t Count()
{
    return GetSnapshot()->bones.size();
}

std::filesystem::path GetPoseDirectory()
{
    return GetRuntime().pluginDir / L"poses";
}

bool Save(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    if (name.empty())
    {
        Log::Warn("SavePose: invalid name");
        return false;
    }

    nlohmann::json root;
    root["format"] = PoseFormat;
    root["version"] = PoseFormatVersion;
    root["name"] = name;
    root["mode"] = "additive";
    root["skeleton"] = "V";

    auto& bones = root["bones"] = nlohmann::json::array();
    for (const auto& bone : GetSnapshot()->bones)
    {
        nlohmann::json entry;
        entry["name"] = bone.boneName;
        if (bone.hasRotation)
        {
            entry["rotation"] = bone.rotationEuler;
        }
        if (bone.hasTranslation)
        {
            entry["translation"] = bone.translation;
        }
        bones.push_back(std::move(entry));
    }

    std::error_code ec;
    std::filesystem::create_directories(GetPoseDirectory(), ec);
    const auto path = GetPoseDirectory() / (name + ".json");
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        Log::Error("SavePose: cannot write %s", path.string().c_str());
        return false;
    }
    out << root.dump(2);
    Log::Info("Pose saved: %s (%zu bones)", name.c_str(), bones.size());
    return true;
}

bool Load(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    const auto path = GetPoseDirectory() / (name + ".json");
    std::ifstream in(path);
    if (!in)
    {
        Log::Warn("LoadPose: file not found: %s", path.string().c_str());
        return false;
    }

    nlohmann::json root;
    try
    {
        root = nlohmann::json::parse(in);
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadPose: invalid JSON in %s: %s", name.c_str(), e.what());
        return false;
    }

    if (root.value("format", "") != PoseFormat)
    {
        Log::Error("LoadPose: %s is not an AdvancedPoseMod pose file", name.c_str());
        return false;
    }

    std::vector<BoneOverride> loaded;
    try
    {
        for (const auto& entry : root.at("bones"))
        {
            BoneOverride bone;
            bone.boneName = entry.at("name").get<std::string>();
            bone.boneHash = HashBone(bone.boneName);
            if (entry.contains("rotation"))
            {
                bone.rotationEuler = entry["rotation"].get<std::array<float, 3>>();
                bone.rotation = Math::FromEulerXYZDegrees(bone.rotationEuler[0], bone.rotationEuler[1],
                                                         bone.rotationEuler[2]);
                bone.hasRotation = !IsZero(bone.rotationEuler);
            }
            if (entry.contains("translation"))
            {
                bone.translation = entry["translation"].get<std::array<float, 3>>();
                bone.hasTranslation = !IsZero(bone.translation);
            }
            loaded.push_back(std::move(bone));
        }
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadPose: malformed bone entry in %s: %s", name.c_str(), e.what());
        return false;
    }

    {
        std::scoped_lock lock(g_mutex);
        g_bones = std::move(loaded);
        PublishLocked();
    }
    Log::Info("Pose loaded: %s (%zu bones)", name.c_str(), Count());
    return true;
}

std::vector<std::string> ListSaved()
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(GetPoseDirectory(), ec))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".json")
        {
            names.push_back(entry.path().stem().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}
} // namespace APM::Pose::PoseOverrides
