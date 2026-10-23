#include "Pose/FacePoses.hpp"

#include "Core/Log.hpp"
#include "Core/NamedFiles.hpp"
#include "Core/Runtime.hpp"
#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;
using APM::Pose::MaxFacePoses;

constexpr const char* ExpressionFormat = "AdvancedPoseMod.Expression";
constexpr int ExpressionVersion = 2; // 2: pose weights (1 held the face bones, no longer read)

struct PoseInfo
{
    std::string name;
    std::string zone;
    std::string side;
    int32_t mirror = -1;
};

struct BoneDelta
{
    uint32_t bone = 0; // index in Head::boneHashes
    Quat rotation{};
    Vec3 translation{};
};

// Every inbetween of a pose lists the same bones in the same order (missing ones filled with no change), so that two
// inbetweens blend entry by entry.
struct Inbetween
{
    float weight = 1.f;
    std::vector<BoneDelta> deltas;
};

struct Head
{
    std::string name; // "h0_000_pwa_c__basehead"
    std::vector<uint64_t> boneHashes;
    std::array<std::vector<Inbetween>, MaxFacePoses> poses; // by pose index; empty if the head lacks the pose
};

struct Data
{
    std::vector<PoseInfo> poses;
    std::unique_ptr<Head> female; // pwa: female V and added characters
    std::unique_ptr<Head> male;   // pma
};

// Written once by LoadData (plugin load), read-only afterwards.
Data g_data;

// Face rig bone index of every head bone (-1: not in the rig), per rig resource and head.
struct BoneMap
{
    size_t rigBoneCount = 0;
    std::vector<int32_t> indices;
};
std::shared_mutex g_mapMutex;
std::unordered_map<uint64_t, std::shared_ptr<const BoneMap>> g_maps;

Quat Slerp(const Quat& a, Quat b, float t)
{
    float d = a.i * b.i + a.j * b.j + a.k * b.k + a.r * b.r;
    if (d < 0.f)
    {
        b = {-b.i, -b.j, -b.k, -b.r};
        d = -d;
    }
    float wa = 1.f - t;
    float wb = t;
    if (d < 0.9995f)
    {
        const float angle = std::acos(d);
        const float s = std::sin(angle);
        wa = std::sin((1.f - t) * angle) / s;
        wb = std::sin(t * angle) / s;
    }
    return APM::Math::Normalize({a.i * wa + b.i * wb, a.j * wa + b.j * wb, a.k * wa + b.k * wb, a.r * wa + b.r * wb});
}

// Reads one head file. The first file read defines the pose list; later ones are matched to it by name.
std::unique_ptr<Head> ReadHead(const std::filesystem::path& aPath, std::vector<PoseInfo>& aPoses)
{
    std::ifstream in(aPath);
    if (!in)
    {
        APM::Log::Warn("Face poses: %s not found (expression editor disabled)", APM::NamedFiles::ToUtf8(aPath).c_str());
        return nullptr;
    }
    auto head = std::make_unique<Head>();
    try
    {
        const auto root = nlohmann::json::parse(in);
        head->name = root.value("head", "");
        for (const auto& bone : root.at("bones"))
        {
            head->boneHashes.push_back(RED4ext::CName(bone.get<std::string>().c_str()).hash);
        }
        const bool definesPoses = aPoses.empty();
        for (const auto& pose : root.at("poses"))
        {
            const auto name = pose.at("name").get<std::string>();
            size_t index = 0;
            while (index < aPoses.size() && aPoses[index].name != name)
            {
                ++index;
            }
            if (index == aPoses.size())
            {
                if (!definesPoses || aPoses.size() >= MaxFacePoses)
                {
                    APM::Log::Warn("Face poses: %s: pose %s skipped", head->name.c_str(), name.c_str());
                    continue;
                }
                aPoses.push_back({name, pose.value("zone", ""), pose.value("side", ""), -1});
            }

            // Bones of all the inbetweens, in order of first appearance.
            std::vector<uint32_t> bones;
            for (const auto& inbetween : pose.at("inbetweens"))
            {
                for (const auto& row : inbetween.at("t"))
                {
                    const uint32_t bone = row.at(0).get<uint32_t>();
                    if (bone < head->boneHashes.size() && std::find(bones.begin(), bones.end(), bone) == bones.end())
                    {
                        bones.push_back(bone);
                    }
                }
            }
            auto& inbetweens = head->poses[index];
            for (const auto& inbetween : pose.at("inbetweens"))
            {
                Inbetween out;
                out.weight = inbetween.at("w").get<float>();
                out.deltas.resize(bones.size());
                for (size_t b = 0; b < bones.size(); ++b)
                {
                    out.deltas[b].bone = bones[b];
                }
                for (const auto& row : inbetween.at("t"))
                {
                    const auto v = row.get<std::array<float, 8>>();
                    const auto it = std::find(bones.begin(), bones.end(), static_cast<uint32_t>(v[0]));
                    if (it == bones.end())
                    {
                        continue;
                    }
                    auto& delta = out.deltas[it - bones.begin()];
                    delta.rotation = APM::Math::Normalize({v[1], v[2], v[3], v[4]});
                    delta.translation = {v[5], v[6], v[7]};
                }
                inbetweens.push_back(std::move(out));
            }
            std::sort(inbetweens.begin(), inbetweens.end(),
                      [](const Inbetween& a, const Inbetween& b) { return a.weight < b.weight; });
        }
    }
    catch (const std::exception& e)
    {
        APM::Log::Error("Face poses: %s: %s", APM::NamedFiles::ToUtf8(aPath).c_str(), e.what());
        return nullptr;
    }
    return head;
}

const Head* HeadFor(const APM::Pose::Target& aTarget)
{
    if (aTarget.characterType == 2 && g_data.male)
    {
        return g_data.male.get();
    }
    return g_data.female ? g_data.female.get() : g_data.male.get();
}

std::shared_ptr<const BoneMap> MapFor(const APM::Pose::Target& aTarget, const Head& aHead)
{
    const uint64_t key = static_cast<uint64_t>(aTarget.rig) ^ (&aHead == g_data.male.get() ? 1ull : 0ull);
    {
        std::shared_lock lock(g_mapMutex);
        const auto it = g_maps.find(key);
        if (it != g_maps.end() && it->second->rigBoneCount == aTarget.boneNames.size())
        {
            return it->second;
        }
    }
    auto map = std::make_shared<BoneMap>();
    map->rigBoneCount = aTarget.boneNames.size();
    size_t missing = 0;
    for (const uint64_t hash : aHead.boneHashes)
    {
        const auto it = std::find(aTarget.boneNames.begin(), aTarget.boneNames.end(), hash);
        map->indices.push_back(it == aTarget.boneNames.end() ? -1 : static_cast<int32_t>(it - aTarget.boneNames.begin()));
        missing += it == aTarget.boneNames.end() ? 1 : 0;
    }
    if (missing > 0)
    {
        APM::Log::Warn("Face poses: %zu of %zu bones of %s not in the face rig of character %u", missing,
                       aHead.boneHashes.size(), aHead.name.c_str(), aTarget.photoModeSlot);
    }
    std::unique_lock lock(g_mapMutex);
    g_maps[key] = map;
    return map;
}
} // namespace

namespace APM::Pose::FacePoses
{
void LoadData()
{
    const auto directory = GetRuntime().pluginDir / L"face";
    g_data.female = ReadHead(directory / L"pwa.json", g_data.poses);
    g_data.male = ReadHead(directory / L"pma.json", g_data.poses);
    for (auto& pose : g_data.poses)
    {
        if (pose.side.empty())
        {
            continue;
        }
        // "eye_l_blink" <-> "eye_r_blink"
        const size_t at = pose.name.find(pose.side == "l" ? "_l_" : "_r_");
        if (at == std::string::npos)
        {
            continue;
        }
        std::string other = pose.name;
        other[at + 1] = pose.side == "l" ? 'r' : 'l';
        pose.mirror = Find(other);
    }
    if (IsAvailable())
    {
        Log::Info("Face poses: %zu poses (%s%s%s)", g_data.poses.size(), g_data.female ? "pwa" : "",
                  g_data.female && g_data.male ? ", " : "", g_data.male ? "pma" : "");
    }
}

bool IsAvailable()
{
    return !g_data.poses.empty() && (g_data.female || g_data.male);
}

size_t Count()
{
    return IsAvailable() ? g_data.poses.size() : 0;
}

const char* Name(size_t aPose)
{
    return aPose < Count() ? g_data.poses[aPose].name.c_str() : "";
}

const char* Zone(size_t aPose)
{
    return aPose < Count() ? g_data.poses[aPose].zone.c_str() : "";
}

const char* Side(size_t aPose)
{
    return aPose < Count() ? g_data.poses[aPose].side.c_str() : "";
}

int32_t Mirror(size_t aPose)
{
    return aPose < Count() ? g_data.poses[aPose].mirror : -1;
}

int32_t Find(const std::string& aName)
{
    for (size_t i = 0; i < g_data.poses.size(); ++i)
    {
        if (g_data.poses[i].name == aName)
        {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose)
{
    if (!AnyFaceWeight(aPose.faceWeights) || !IsAvailable())
    {
        return;
    }
    const Head* head = HeadFor(aTarget);
    if (!head)
    {
        return;
    }
    const auto map = MapFor(aTarget, *head);
    const size_t poseCount = std::min(g_data.poses.size(), MaxFacePoses);
    for (size_t p = 0; p < poseCount; ++p)
    {
        const float weight = std::clamp(aPose.faceWeights[p], 0.f, 1.f);
        const auto& inbetweens = head->poses[p];
        if (weight <= 0.f || inbetweens.empty())
        {
            continue;
        }
        // The segment the weight falls in; below the first inbetween it starts from no change (weight 0).
        size_t hi = 0;
        while (hi + 1 < inbetweens.size() && inbetweens[hi].weight < weight)
        {
            ++hi;
        }
        const Inbetween* lo = hi > 0 ? &inbetweens[hi - 1] : nullptr;
        const float from = lo ? lo->weight : 0.f;
        const float span = inbetweens[hi].weight - from;
        const float t = span > 1e-6f ? std::clamp((weight - from) / span, 0.f, 1.f) : 1.f;
        const auto& deltas = inbetweens[hi].deltas;
        for (size_t d = 0; d < deltas.size(); ++d)
        {
            const int32_t bone = deltas[d].bone < map->indices.size() ? map->indices[deltas[d].bone] : -1;
            if (bone < 0 || static_cast<uint32_t>(bone) >= aCount)
            {
                continue;
            }
            const Quat rotation = Slerp(lo ? lo->deltas[d].rotation : Quat{}, deltas[d].rotation, t);
            const Vec3 base = lo ? lo->deltas[d].translation : Vec3{};
            const Vec3 translation = base + (deltas[d].translation - base) * t;
            auto& out = aBones[bone];
            const Quat current{out.Rotation.i, out.Rotation.j, out.Rotation.k, out.Rotation.r};
            const Quat q = Math::Normalize(Math::Mul(current, rotation));
            out.Rotation.i = q.i;
            out.Rotation.j = q.j;
            out.Rotation.k = q.k;
            out.Rotation.r = q.r;
            out.Translation.X += translation.x;
            out.Translation.Y += translation.y;
            out.Translation.Z += translation.z;
        }
    }
}

std::filesystem::path GetDirectory()
{
    return GetRuntime().pluginDir / L"expressions";
}

std::vector<std::string> ListSaved()
{
    return NamedFiles::List(GetDirectory());
}

bool Save(const std::string& aName)
{
    std::filesystem::path path;
    if (!NamedFiles::Resolve(GetDirectory(), aName, path))
    {
        Log::Warn("SaveExpression: invalid name");
        return false;
    }
    nlohmann::json root;
    root["format"] = ExpressionFormat;
    root["version"] = ExpressionVersion;
    auto& weights = root["weights"] = nlohmann::json::object();
    const auto current = PoseOverrides::GetFaceWeights();
    for (size_t i = 0; i < Count(); ++i)
    {
        if (current[i] > 0.f)
        {
            weights[g_data.poses[i].name] = std::round(current[i] * 1000.f) / 1000.f;
        }
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        Log::Error("SaveExpression: cannot write %s", NamedFiles::ToUtf8(path).c_str());
        return false;
    }
    out << root.dump(1);
    Log::Info("Expression saved: %s (%zu poses)", aName.c_str(), weights.size());
    return true;
}

std::string SaveNew()
{
    std::error_code ec;
    for (int number = 1; number < 10000; ++number)
    {
        char name[32]{};
        std::snprintf(name, sizeof(name), "Expression %03d", number);
        if (!std::filesystem::exists(GetDirectory() / (std::string(name) + ".json"), ec))
        {
            return Save(name) ? std::string(name) : std::string();
        }
    }
    return {};
}

bool Load(const std::string& aName)
{
    std::filesystem::path path;
    if (!NamedFiles::Resolve(GetDirectory(), aName, path))
    {
        return false;
    }
    std::ifstream in(path);
    if (!in)
    {
        Log::Warn("LoadExpression: file not found: %s", NamedFiles::ToUtf8(path).c_str());
        return false;
    }
    FaceWeights weights{};
    try
    {
        const auto root = nlohmann::json::parse(in);
        if (root.value("format", "") != ExpressionFormat || root.value("version", 0) < ExpressionVersion ||
            !root.contains("weights"))
        {
            Log::Warn("LoadExpression: %s: old or unknown format", aName.c_str());
            return false;
        }
        for (const auto& [name, value] : root.at("weights").items())
        {
            const int32_t pose = Find(name);
            if (pose < 0)
            {
                Log::Verbose("LoadExpression: %s: unknown pose %s", aName.c_str(), name.c_str());
                continue;
            }
            weights[pose] = std::clamp(value.get<float>(), 0.f, 1.f);
        }
    }
    catch (const std::exception& e)
    {
        Log::Error("LoadExpression: %s: %s", aName.c_str(), e.what());
        return false;
    }
    PoseOverrides::SetFaceWeights(weights, aName);
    Log::Info("Expression loaded: %s", aName.c_str());
    return true;
}

bool Delete(const std::string& aName)
{
    std::filesystem::path path;
    if (!NamedFiles::Resolve(GetDirectory(), aName, path))
    {
        return false;
    }
    std::error_code ec;
    return std::filesystem::remove(path, ec);
}
} // namespace APM::Pose::FacePoses
