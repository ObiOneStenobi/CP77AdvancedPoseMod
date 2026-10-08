#include "Pose/PoseOverrides.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Pose/GizmoState.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_set>

namespace
{
constexpr const char* PoseFormat = "AdvancedPoseMod.Pose";
constexpr int PoseFormatVersion = 2; // 2: "base" holds the whole pose

std::atomic<bool> g_enabled{false};
std::mutex g_mutex; // guards g_bones and g_snapshot replacement
std::vector<APM::Pose::BoneOverride> g_bones;
std::array<APM::Pose::IkChainSettings, APM::Pose::IkChainCount> g_ik{};
std::shared_ptr<const APM::Pose::BasePose> g_base;
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
    snapshot->ik = g_ik;
    snapshot->base = g_base;
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

namespace APM::Pose
{
namespace
{
constexpr const char* IkChainNames[IkChainCount] = {"LeftHand", "RightHand", "LeftFoot", "RightFoot"};
}

bool ParseIkChain(const std::string& aName, IkChain& aOut)
{
    for (size_t i = 0; i < IkChainCount; ++i)
    {
        if (aName == IkChainNames[i])
        {
            aOut = static_cast<IkChain>(i);
            return true;
        }
    }
    return false;
}

const char* GetIkChainName(IkChain aChain)
{
    const auto index = static_cast<size_t>(aChain);
    return index < IkChainCount ? IkChainNames[index] : "";
}
} // namespace APM::Pose

namespace APM::Pose::PoseOverrides
{
void SetIkChain(IkChain aChain, const IkChainSettings& aSettings)
{
    const auto index = static_cast<size_t>(aChain);
    if (index >= IkChainCount)
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    g_ik[index] = aSettings;
    PublishLocked();
    if (aSettings.enabled)
    {
        g_enabled = true;
    }
}

IkChainSettings GetIkChain(IkChain aChain)
{
    const auto index = static_cast<size_t>(aChain);
    std::scoped_lock lock(g_mutex);
    return index < IkChainCount ? g_ik[index] : IkChainSettings{};
}

void SetEnabled(bool aEnabled)
{
    if (g_enabled.exchange(aEnabled) != aEnabled)
    {
        Log::Info("Custom pose %s", aEnabled ? "enabled" : "disabled");
    }
}

bool IsEnabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

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
    bone.hasUiAngles = false;
    bone.hasRotation = !IsZero(bone.rotationEuler);
    PublishLocked();
    g_enabled = true;
}

void SetTranslation(const std::string& aBone, float aX, float aY, float aZ)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.translation = {aX, aY, aZ};
    bone.hasTranslation = !IsZero(bone.translation);
    PublishLocked();
    g_enabled = true;
}

void SetRotationQuat(const std::string& aBone, const Math::Quat& aRotation, const std::array<float, 3>& aUiAngles)
{
    std::scoped_lock lock(g_mutex);
    auto& bone = FindOrAddLocked(aBone);
    bone.rotation = Math::Normalize(aRotation);
    bone.rotationEuler = {};
    bone.uiAngles = aUiAngles;
    bone.hasUiAngles = true;
    bone.hasRotation = !IsZero(aUiAngles);
    PublishLocked();
    g_enabled = true;
}

bool GetUiAngles(const std::string& aBone, std::array<float, 3>& aOut)
{
    std::scoped_lock lock(g_mutex);
    const auto* bone = FindLocked(aBone);
    const bool has = bone && bone->hasRotation && bone->hasUiAngles;
    aOut = has ? bone->uiAngles : std::array<float, 3>{};
    return has;
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
    g_ik = {};
    g_base.reset();
    PublishLocked();
}

size_t Count()
{
    const auto snapshot = GetSnapshot();
    size_t count = snapshot->bones.size();
    for (const auto& chain : snapshot->ik)
    {
        count += chain.enabled ? 1 : 0;
    }
    return count;
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
    root["skeleton"] = "V";

    // The whole visible pose: body bones as local rotations (edits and IK included). Bones of the face stay
    // edits on top of the animation (the face keeps its Photo Mode expression).
    std::vector<GizmoState::CapturedBone> captured;
    const bool full = GizmoState::CaptureLocalPose(captured);
    std::unordered_set<uint64_t> baseBones;
    if (full)
    {
        root["mode"] = "full";
        auto& base = root["base"] = nlohmann::json::object();
        for (const auto& bone : captured)
        {
            if (!bone.inBase)
            {
                continue;
            }
            nlohmann::json entry;
            entry["r"] = {bone.rotation.i, bone.rotation.j, bone.rotation.k, bone.rotation.r};
            if (bone.keepTranslation)
            {
                entry["t"] = {bone.translation.x, bone.translation.y, bone.translation.z};
            }
            base[bone.name] = std::move(entry);
            baseBones.insert(bone.hash);
        }
    }
    else
    {
        root["mode"] = "additive";
    }

    auto& bones = root["bones"] = nlohmann::json::array();
    for (const auto& bone : GetSnapshot()->bones)
    {
        if (baseBones.contains(bone.boneHash))
        {
            continue; // already part of the saved pose
        }
        nlohmann::json entry;
        entry["name"] = bone.boneName;
        if (bone.hasRotation)
        {
            if (bone.hasUiAngles)
            {
                entry["rotationQuat"] = {bone.rotation.i, bone.rotation.j, bone.rotation.k, bone.rotation.r};
                entry["ui"] = bone.uiAngles;
            }
            else
            {
                entry["rotation"] = bone.rotationEuler;
            }
        }
        if (bone.hasTranslation)
        {
            entry["translation"] = bone.translation;
        }
        bones.push_back(std::move(entry));
    }

    auto& ik = root["ik"] = nlohmann::json::object();
    const auto snapshot = GetSnapshot();
    for (size_t i = 0; i < IkChainCount && !full; ++i) // a full pose already has the IK result in it
    {
        const auto& chain = snapshot->ik[i];
        if (chain.enabled)
        {
            ik[GetIkChainName(static_cast<IkChain>(i))] = {
                {"offset", chain.offset}, {"swivel", chain.swivel}, {"pin", chain.followBody ? "body" : "world"}};
        }
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
    Log::Info("Pose saved: %s (%s, %zu body bones, %zu edits, %zu IK chains)", name.c_str(), full ? "full" : "edits only",
              baseBones.size(), bones.size(), ik.size());
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
    std::array<IkChainSettings, IkChainCount> loadedIk{};
    std::shared_ptr<BasePose> loadedBase;
    try
    {
        if (root.contains("base"))
        {
            loadedBase = std::make_shared<BasePose>();
            loadedBase->name = name;
            for (const auto& [boneName, entry] : root["base"].items())
            {
                BasePoseBone bone;
                bone.boneHash = HashBone(boneName);
                const auto q = entry.at("r").get<std::array<float, 4>>();
                bone.rotation = Math::Normalize(Math::Quat{q[0], q[1], q[2], q[3]});
                if (entry.contains("t"))
                {
                    bone.translation = entry["t"].get<std::array<float, 3>>();
                    bone.hasTranslation = true;
                }
                loadedBase->bones.push_back(bone);
            }
        }

        if (root.contains("ik"))
        {
            for (const auto& [chainName, entry] : root["ik"].items())
            {
                IkChain chain;
                if (!ParseIkChain(chainName, chain))
                {
                    continue;
                }
                auto& settings = loadedIk[static_cast<size_t>(chain)];
                settings.enabled = true;
                settings.offset = entry.value("offset", std::array<float, 3>{});
                settings.swivel = entry.value("swivel", 0.f);
                settings.followBody = entry.value("pin", std::string("world")) == "body";
            }
        }

        for (const auto& entry : root.at("bones"))
        {
            BoneOverride bone;
            bone.boneName = entry.at("name").get<std::string>();
            bone.boneHash = HashBone(bone.boneName);
            if (entry.contains("rotationQuat"))
            {
                const auto q = entry["rotationQuat"].get<std::array<float, 4>>();
                bone.rotation = Math::Normalize(Math::Quat{q[0], q[1], q[2], q[3]});
                bone.uiAngles = entry.value("ui", std::array<float, 3>{});
                bone.hasUiAngles = entry.contains("ui");
                bone.hasRotation = true;
            }
            else if (entry.contains("rotation"))
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
        g_ik = loadedIk;
        g_base = std::move(loadedBase);
        PublishLocked();
    }
    g_enabled = true;
    Log::Info("Pose loaded: %s (%zu bones)", name.c_str(), Count());
    return true;
}

std::string GetBasePoseName()
{
    std::scoped_lock lock(g_mutex);
    return g_base ? g_base->name : std::string();
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

bool Delete(const std::string& aName)
{
    const auto name = SanitizeName(aName);
    if (name.empty())
    {
        return false;
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(GetPoseDirectory() / (name + ".json"), ec);
    Log::Info("Pose '%s' %s", name.c_str(), removed ? "deleted" : "not found");
    return removed;
}

std::string Rename(const std::string& aOldName, const std::string& aNewName)
{
    const auto from = SanitizeName(aOldName);
    std::string base = SanitizeName(aNewName);
    while (!base.empty() && base.front() == ' ')
    {
        base.erase(base.begin());
    }
    if (from.empty() || base.empty())
    {
        return {};
    }
    if (base == from)
    {
        return from;
    }
    std::error_code ec;
    const auto source = GetPoseDirectory() / (from + ".json");
    if (!std::filesystem::exists(source, ec))
    {
        return {};
    }
    std::string name = base;
    for (int number = 2; std::filesystem::exists(GetPoseDirectory() / (name + ".json"), ec); ++number)
    {
        // Same name in another case on a case-insensitive disk: renaming onto itself is fine.
        if (std::filesystem::equivalent(source, GetPoseDirectory() / (name + ".json"), ec))
        {
            break;
        }
        if (number > 999)
        {
            return {};
        }
        name = base + " " + std::to_string(number);
    }
    std::filesystem::rename(source, GetPoseDirectory() / (name + ".json"), ec);
    if (ec)
    {
        Log::Warn("Pose '%s' could not be renamed to '%s': %s", from.c_str(), name.c_str(), ec.message().c_str());
        return {};
    }
    Log::Info("Pose '%s' renamed to '%s'", from.c_str(), name.c_str());
    return name;
}

std::string SaveNew()
{
    std::error_code ec;
    for (int number = 1; number < 10000; ++number)
    {
        char name[32]{};
        std::snprintf(name, sizeof(name), "Pose %03d", number);
        if (!std::filesystem::exists(GetPoseDirectory() / (std::string(name) + ".json"), ec))
        {
            return Save(name) ? std::string(name) : std::string();
        }
    }
    return {};
}
} // namespace APM::Pose::PoseOverrides
