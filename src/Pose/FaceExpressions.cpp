#include "Pose/FaceExpressions.hpp"

#include "Core/Log.hpp"
#include "Core/Runtime.hpp"
#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

namespace
{
using APM::Math::Quat;
using APM::Math::Vec3;

constexpr const char* RegionNames[APM::Pose::FaceRegionCount] = {"Brows", "Eyes", "Mouth", "Jaw"};
constexpr int32_t NoRegion = -1;

// Recording: wait for the face to settle on a newly chosen expression, then average it over a moment (blinks and
// small idle moves wash out).
constexpr ULONGLONG SettleMs = 1200;
constexpr ULONGLONG RecordMs = 1500;

struct Recording
{
    std::string name;
    std::unordered_map<uint64_t, std::pair<Quat, Vec3>> bones; // local rotation and translation per bone
};

struct Accumulator
{
    int32_t expression = -1;
    uint32_t frames = 0;
    std::unordered_map<uint64_t, std::pair<Quat, Vec3>> sums;
};

std::mutex g_mutex;
std::vector<std::string> g_choiceNames;
std::vector<int32_t> g_choiceValues;
std::map<int32_t, Recording> g_recordings;
bool g_loaded = false;
Accumulator g_accumulator;

std::atomic<int32_t> g_current{-1};
std::atomic<ULONGLONG> g_changedAt{0};

// Face region of a face rig bone, from its name (cached per bone).
int32_t RegionOf(uint64_t aHash)
{
    static std::unordered_map<uint64_t, int32_t> cache;
    if (const auto it = cache.find(aHash); it != cache.end())
    {
        return it->second;
    }
    const char* text = RED4ext::CName(aHash).ToString();
    std::string name = text ? text : "";
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto has = [&](const char* aPart) { return name.find(aPart) != std::string::npos; };

    int32_t region = NoRegion;
    if (name.empty() || has("_grp") || has("neck") || has("sterno") || has("head") || has("gravity") ||
        has("pupil") || name == "l_j_eye_jnt" || name == "r_j_eye_jnt")
    {
        region = NoRegion; // eyeballs follow the eye bones; neck and helpers are not part of an expression
    }
    else if (has("brows") || has("eye_nose"))
    {
        region = 0;
    }
    else if (has("eye_lid") || has("lashes") || has("eye_check") || has("wetness"))
    {
        region = 1;
    }
    else if (has("mug") || has("nosabial") || has("tongue") || has("nose"))
    {
        region = 2;
    }
    else if (has("jaw"))
    {
        region = 3;
    }
    cache.emplace(aHash, region);
    return region;
}

std::filesystem::path FilePath()
{
    return APM::GetRuntime().pluginDir / L"expressions.json";
}

// Must be called with g_mutex held.
void LoadLocked()
{
    if (g_loaded)
    {
        return;
    }
    g_loaded = true;
    std::ifstream in(FilePath());
    if (!in)
    {
        return;
    }
    try
    {
        const auto root = nlohmann::json::parse(in);
        for (const auto& [key, entry] : root.at("expressions").items())
        {
            Recording recording;
            recording.name = entry.value("name", std::string());
            for (const auto& [bone, value] : entry.at("bones").items())
            {
                const auto r = value.at("r").get<std::array<float, 4>>();
                const auto t = value.at("t").get<std::array<float, 3>>();
                recording.bones[std::stoull(bone)] = {Quat{r[0], r[1], r[2], r[3]}, Vec3{t[0], t[1], t[2]}};
            }
            g_recordings[std::stoi(key)] = std::move(recording);
        }
        APM::Log::Info("Expressions: %zu recordings loaded", g_recordings.size());
    }
    catch (const std::exception& e)
    {
        APM::Log::Warn("Expressions: cannot read %s: %s", FilePath().string().c_str(), e.what());
        g_recordings.clear();
    }
}

// Must be called with g_mutex held.
void SaveLocked()
{
    nlohmann::json root;
    root["format"] = "AdvancedPoseMod.Expressions";
    root["version"] = 1;
    auto& expressions = root["expressions"] = nlohmann::json::object();
    for (const auto& [value, recording] : g_recordings)
    {
        auto& entry = expressions[std::to_string(value)];
        entry["name"] = recording.name;
        auto& bones = entry["bones"] = nlohmann::json::object();
        for (const auto& [hash, pose] : recording.bones)
        {
            bones[std::to_string(hash)] = {{"r", {pose.first.i, pose.first.j, pose.first.k, pose.first.r}},
                                           {"t", {pose.second.x, pose.second.y, pose.second.z}}};
        }
    }
    std::ofstream out(FilePath(), std::ios::trunc);
    if (out)
    {
        out << root.dump();
    }
}

// Must be called with g_mutex held.
std::string NameOfLocked(int32_t aValue)
{
    for (size_t i = 0; i < g_choiceValues.size() && i < g_choiceNames.size(); ++i)
    {
        if (g_choiceValues[i] == aValue)
        {
            return g_choiceNames[i];
        }
    }
    const auto it = g_recordings.find(aValue);
    return it != g_recordings.end() && !it->second.name.empty() ? it->second.name : "Expression " + std::to_string(aValue);
}

// The neutral expression is the first choice of the menu.
int32_t NeutralLocked()
{
    return g_choiceValues.empty() ? 0 : g_choiceValues.front();
}

Quat Power(const Quat& aRotation, float aWeight)
{
    Quat q = aRotation.r < 0.f ? Quat{-aRotation.i, -aRotation.j, -aRotation.k, -aRotation.r} : aRotation;
    const float s = std::sqrt(q.i * q.i + q.j * q.j + q.k * q.k);
    if (s < 1e-7f)
    {
        return {0.f, 0.f, 0.f, 1.f};
    }
    const float angle = 2.f * std::atan2(s, q.r) * aWeight;
    const float k = std::sin(angle * 0.5f) / s;
    return {q.i * k, q.j * k, q.k * k, std::cos(angle * 0.5f)};
}
} // namespace

namespace APM::Pose::FaceExpressions
{
const char* GetRegionName(size_t aRegion)
{
    return aRegion < FaceRegionCount ? RegionNames[aRegion] : "";
}

void SetChoices(const std::vector<std::string>& aNames, const std::vector<int32_t>& aValues)
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    g_choiceNames = aNames;
    g_choiceValues = aValues;
    std::string list;
    for (size_t i = 0; i < aNames.size() && i < aValues.size(); ++i)
    {
        list += (i ? ", " : "") + aNames[i] + "=" + std::to_string(aValues[i]);
    }
    Log::Info("Expressions: %zu choices: %s", aNames.size(), list.c_str());
}

size_t GetChoiceCount()
{
    std::scoped_lock lock(g_mutex);
    return std::min(g_choiceNames.size(), g_choiceValues.size());
}

std::string GetChoiceName(size_t aIndex)
{
    std::scoped_lock lock(g_mutex);
    return aIndex < g_choiceNames.size() ? g_choiceNames[aIndex] : std::string();
}

int32_t GetChoiceValue(size_t aIndex)
{
    std::scoped_lock lock(g_mutex);
    return aIndex < g_choiceValues.size() ? g_choiceValues[aIndex] : -1;
}

std::string GetNameOf(int32_t aValue)
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    return NameOfLocked(aValue);
}

void OnPhotoModeAttribute(uint32_t aAttribute, float aValue)
{
    if (aAttribute != 28)
    {
        return;
    }
    const int32_t value = static_cast<int32_t>(std::lround(aValue));
    if (g_current.exchange(value) != value)
    {
        g_changedAt = GetTickCount64();
        Log::Info("Expressions: Photo Mode expression %d", value);
    }
}

bool IsRecorded(int32_t aValue)
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    return g_recordings.contains(aValue);
}

size_t RecordedCount()
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    return g_recordings.size();
}

bool IsNeutralRecorded()
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    return g_recordings.contains(NeutralLocked());
}

void ForgetRecordings()
{
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    g_recordings.clear();
    g_accumulator = {};
    g_changedAt = GetTickCount64(); // the expression shown now is recorded again after the settle time
    SaveLocked();
    Log::Info("Expressions: recordings cleared");
}

void Observe(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    const int32_t current = g_current.load();
    if (current < 0)
    {
        return;
    }
    const ULONGLONG elapsed = GetTickCount64() - g_changedAt.load();
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    if (g_choiceValues.empty() || g_recordings.contains(current) || elapsed < SettleMs)
    {
        g_accumulator.frames = g_accumulator.expression == current ? g_accumulator.frames : 0;
        return;
    }
    if (g_accumulator.expression != current)
    {
        g_accumulator = {};
        g_accumulator.expression = current;
    }
    const uint32_t count = std::min<uint32_t>(aCount, static_cast<uint32_t>(aTarget.boneNames.size()));
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t hash = aTarget.boneNames[i];
        if (RegionOf(hash) == NoRegion)
        {
            continue;
        }
        const auto& bone = aBones[i];
        Quat q{bone.Rotation.i, bone.Rotation.j, bone.Rotation.k, bone.Rotation.r};
        auto& sum = g_accumulator.sums[hash];
        if (g_accumulator.frames > 0 && sum.first.i * q.i + sum.first.j * q.j + sum.first.k * q.k + sum.first.r * q.r < 0.f)
        {
            q = {-q.i, -q.j, -q.k, -q.r}; // same hemisphere as the frames before
        }
        sum.first = {sum.first.i + q.i, sum.first.j + q.j, sum.first.k + q.k, sum.first.r + q.r};
        sum.second = sum.second + Vec3{bone.Translation.X, bone.Translation.Y, bone.Translation.Z};
    }
    ++g_accumulator.frames;
    if (elapsed < SettleMs + RecordMs || g_accumulator.frames < 10)
    {
        return;
    }

    Recording recording;
    recording.name = NameOfLocked(current);
    const float frames = static_cast<float>(g_accumulator.frames);
    for (const auto& [hash, sum] : g_accumulator.sums)
    {
        recording.bones[hash] = {Math::Normalize(sum.first), sum.second * (1.f / frames)};
    }
    g_recordings[current] = std::move(recording);
    Log::Info("Expressions: recorded %s (%d) from %u frames, %zu bones", NameOfLocked(current).c_str(), current,
              g_accumulator.frames, g_accumulator.sums.size());
    g_accumulator = {};
    SaveLocked();
}

void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const FaceMix& aMix)
{
    if (!AnyFaceMix(aMix))
    {
        return;
    }
    std::scoped_lock lock(g_mutex);
    LoadLocked();
    const auto neutral = g_recordings.find(NeutralLocked());
    if (neutral == g_recordings.end())
    {
        return;
    }
    std::array<const Recording*, FaceRegionCount> chosen{};
    for (size_t r = 0; r < FaceRegionCount; ++r)
    {
        if (aMix[r].IsActive())
        {
            const auto it = g_recordings.find(aMix[r].expression);
            chosen[r] = it != g_recordings.end() ? &it->second : nullptr;
        }
    }
    const uint32_t count = std::min<uint32_t>(aCount, static_cast<uint32_t>(aTarget.boneNames.size()));
    for (uint32_t i = 0; i < count; ++i)
    {
        const uint64_t hash = aTarget.boneNames[i];
        const int32_t region = RegionOf(hash);
        if (region == NoRegion || !chosen[region])
        {
            continue;
        }
        const auto from = neutral->second.bones.find(hash);
        const auto to = chosen[region]->bones.find(hash);
        if (from == neutral->second.bones.end() || to == chosen[region]->bones.end())
        {
            continue;
        }
        const float weight = std::clamp(aMix[region].weight, 0.f, 1.f);
        // The change from the neutral face, at the chosen strength, on top of the face shown now.
        const Quat change = Math::Mul(Math::Conjugate(from->second.first), to->second.first);
        auto& bone = aBones[i];
        const Quat current{bone.Rotation.i, bone.Rotation.j, bone.Rotation.k, bone.Rotation.r};
        const Quat result = Math::Normalize(Math::Mul(current, Power(change, weight)));
        bone.Rotation.i = result.i;
        bone.Rotation.j = result.j;
        bone.Rotation.k = result.k;
        bone.Rotation.r = result.r;
        const Vec3 move = (to->second.second - from->second.second) * weight;
        bone.Translation.X += move.x;
        bone.Translation.Y += move.y;
        bone.Translation.Z += move.z;
    }
}
} // namespace APM::Pose::FaceExpressions
