#include "Pose/FacePoses.hpp"

#include "Core/Log.hpp"
#include "Core/NamedFiles.hpp"
#include "Core/Runtime.hpp"
#include "Pose/Characters.hpp"
#include "Math/Quat.hpp"
#include "Math/Vec3.hpp"

#include <RED4ext/CName.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <mutex>
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
// 3: the whole face (value of every pose); 2 held weights added to the Photo Mode expression, 1 the face bones (neither
// read any more).
constexpr int ExpressionVersion = 3;

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

// Poses that act only through the solver's correctives (empty main poses, so the head files lack them).
struct TrackOnlyPose
{
    const char* name;
    const char* zone;
};
constexpr TrackOnlyPose TrackOnlyPoses[] = {{"lips_together_up", "lips"}, {"lips_together_dn", "lips"},
                                            {"lips_tighten_up", "lips"},  {"lips_tighten_dn", "lips"},
                                            {"jaw_mid_close", "jaw"},     {"jaw_mid_clench", "jaw"}};

// Face track of every pose (-1: the rig lacks it), per rig resource.
struct TrackMap
{
    uint32_t trackCount = 0;
    std::vector<int32_t> tracks;
    std::array<std::array<int32_t, 4>, 2> gaze{}; // eye_<l|r>_dir_<up|dn|in|out> (-1: missing)
};

// Gaze tracks at 1: the eye turned by about this much (probe 25/10, game 2.31; the same limits as the eye bones).
constexpr float GazeUpDegrees = 21.f;
constexpr float GazeDownDegrees = 30.f;
constexpr float GazeSideDegrees = 40.f;
std::shared_mutex g_trackMapMutex;
std::unordered_map<uint64_t, std::shared_ptr<const TrackMap>> g_trackMaps;

// Faces the solver took the values for (animated component -> time of the last frame, ms): Apply only adds the poses
// their rig has no track for. And the Photo Mode expression of each character as the panel shows it (see GameValue):
// read again when captureAt (ms, 0 = no) has passed.
std::mutex g_solverMutex;
std::unordered_map<uintptr_t, uint64_t> g_solverFaces;
struct GameExpression
{
    APM::Pose::FaceWeights values{};
    bool known = false;
    uint64_t captureAt = 0;
    int32_t menuIndex = 0; // expression chosen in the menu (Photo Mode starts and adds characters with the first one)
};
std::array<GameExpression, APM::Pose::Characters::Count> g_gameExpressions{};
std::array<std::atomic<bool>, APM::Pose::Characters::Count> g_characterMale{};
// Editor gaze per character (see SetGaze), and when the solver last took it.
struct Gaze
{
    bool active = false;
    std::array<APM::Math::Vec3, 2> turns{};
    uint64_t takenAt = 0;
};
std::array<Gaze, APM::Pose::Characters::Count> g_gazes{};

// Whether a face's solver result reaches its final pose (see OnSolverInput), by animated component.
struct SolverCheck
{
    std::vector<int32_t> watched; // face rig bones the poses move
    std::vector<RED4ext::QsTransform> input;
    std::vector<RED4ext::QsTransform> output;
    bool active = false; // a pose track is up in this frame
    bool moved = false;  // the solver moved the watched bones
    bool hasOutput = false;
    uint32_t badFrames = 0;
    bool confirmed = false; // seen working
    bool ignored = false;   // posed on the bones from now on
};
std::mutex g_checkMutex;
std::unordered_map<uintptr_t, SolverCheck> g_checks;
constexpr float CheckActivity = 0.2f; // pose track needed to judge a frame
constexpr float MovedDegrees = 0.5f;
constexpr float MovedMeters = 0.0005f;
constexpr float SameDegrees = 3.f; // later nodes may still touch the face a little
constexpr float SameMeters = 0.002f;
constexpr uint32_t BadFramesToIgnore = 30;

bool IsIgnoredFace(uintptr_t aComponent)
{
    std::scoped_lock lock(g_checkMutex);
    const auto it = g_checks.find(aComponent);
    return it != g_checks.end() && it->second.ignored;
}

// Largest difference between two sets of bones (degrees, meters).
std::pair<float, float> Difference(const RED4ext::QsTransform* aA, const RED4ext::QsTransform* aB, size_t aCount)
{
    float degrees = 0.f;
    float meters = 0.f;
    for (size_t i = 0; i < aCount; ++i)
    {
        const auto& a = aA[i];
        const auto& b = aB[i];
        const float dot = std::fabs(a.Rotation.i * b.Rotation.i + a.Rotation.j * b.Rotation.j +
                                    a.Rotation.k * b.Rotation.k + a.Rotation.r * b.Rotation.r);
        degrees = std::max(degrees, 2.f * std::acos(std::min(dot, 1.f)) * 57.29578f);
        const float dx = a.Translation.X - b.Translation.X;
        const float dy = a.Translation.Y - b.Translation.Y;
        const float dz = a.Translation.Z - b.Translation.Z;
        meters = std::max(meters, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return {degrees, meters};
}
constexpr uint64_t SolverFaceTimeout = 1000;
constexpr uint64_t ExpressionSettleMs = 500; // after a change in the menu, before the new expression is read
constexpr float MaxLoadedValue = 4.f;        // values of a saved expression (other mods' expressions go above 1)

uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

bool IsSolverFace(uintptr_t aComponent)
{
    std::scoped_lock lock(g_solverMutex);
    const auto it = g_solverFaces.find(aComponent);
    return it != g_solverFaces.end() && NowMs() - it->second < SolverFaceTimeout;
}

std::shared_ptr<const TrackMap> TrackMapFor(uint64_t aRig, const RED4ext::CName* aNames, uint32_t aCount);

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
    const bool male = aTarget.characterType == 2 ||
                      (aTarget.photoModeSlot < APM::Pose::Characters::Count && g_characterMale[aTarget.photoModeSlot]);
    if (male && g_data.male)
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

std::shared_ptr<const TrackMap> TrackMapFor(uint64_t aRig, const RED4ext::CName* aNames, uint32_t aCount)
{
    {
        std::shared_lock lock(g_trackMapMutex);
        const auto it = g_trackMaps.find(aRig);
        if (it != g_trackMaps.end() && it->second->trackCount == aCount)
        {
            return it->second;
        }
    }
    auto map = std::make_shared<TrackMap>();
    map->trackCount = aCount;
    size_t missing = 0;
    std::string missingNames;
    for (const auto& pose : g_data.poses)
    {
        const uint64_t hash = RED4ext::CName(pose.name.c_str()).hash;
        int32_t track = -1;
        for (uint32_t i = 0; i < aCount && track < 0; ++i)
        {
            if (aNames[i].hash == hash)
            {
                track = static_cast<int32_t>(i);
            }
        }
        map->tracks.push_back(track);
        if (track < 0)
        {
            ++missing;
            missingNames += " " + pose.name;
        }
    }
    static constexpr const char* GazeNames[2][4] = {
        {"eye_l_dir_up", "eye_l_dir_dn", "eye_l_dir_in", "eye_l_dir_out"},
        {"eye_r_dir_up", "eye_r_dir_dn", "eye_r_dir_in", "eye_r_dir_out"}};
    for (int e = 0; e < 2; ++e)
    {
        for (int d = 0; d < 4; ++d)
        {
            const uint64_t hash = RED4ext::CName(GazeNames[e][d]).hash;
            map->gaze[e][d] = -1;
            for (uint32_t i = 0; i < aCount; ++i)
            {
                if (aNames[i].hash == hash)
                {
                    map->gaze[e][d] = static_cast<int32_t>(i);
                }
            }
        }
    }
    APM::Log::Info("Face poses: face rig %016llx with %u tracks, %zu of %zu poses found%s%s",
                   static_cast<unsigned long long>(aRig), aCount, g_data.poses.size() - missing, g_data.poses.size(),
                   missing > 0 ? " (on the bones:" : "", missing > 0 ? (missingNames + ")").c_str() : "");
    std::unique_lock lock(g_trackMapMutex);
    g_trackMaps[aRig] = map;
    return map;
}

std::shared_ptr<const TrackMap> FindTrackMap(uint64_t aRig)
{
    std::shared_lock lock(g_trackMapMutex);
    const auto it = g_trackMaps.find(aRig);
    return it != g_trackMaps.end() ? it->second : nullptr;
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
    if (!g_data.poses.empty())
    {
        for (const auto& extra : TrackOnlyPoses)
        {
            if (Find(extra.name) < 0 && g_data.poses.size() < MaxFacePoses)
            {
                g_data.poses.push_back({extra.name, extra.zone, "", -1});
            }
        }
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

float ApplyTracks(const Target& aTarget, float* aTracks, uint32_t aCount, const RED4ext::CName* aNames, uint32_t aNameCount,
                  const OverrideSnapshot& aPose, bool aApply)
{
    if (!IsAvailable() || !aTracks || !aNames || IsIgnoredFace(aTarget.animatedComponent))
    {
        return 0.f;
    }
    const auto map = TrackMapFor(aTarget.rig, aNames, std::min(aNameCount, aCount));
    const size_t poseCount = std::min({g_data.poses.size(), MaxFacePoses, map->tracks.size()});
    {
        std::scoped_lock lock(g_solverMutex);
        const uint64_t now = NowMs();
        g_solverFaces[aTarget.animatedComponent] = now;
        if (aTarget.photoModeSlot < Characters::Count)
        {
            auto& expression = g_gameExpressions[aTarget.photoModeSlot];
            if (!expression.known || (expression.captureAt != 0 && now >= expression.captureAt))
            {
                // Read once more a moment after the first time (the expression may still be blending in).
                expression.captureAt = expression.known ? 0 : now + ExpressionSettleMs;
                for (size_t p = 0; p < poseCount; ++p)
                {
                    const int32_t track = map->tracks[p];
                    expression.values[p] =
                        track >= 0 && std::isfinite(aTracks[track]) ? std::max(aTracks[track], 0.f) : 0.f;
                }
                expression.known = true;
            }
        }
    }
    if (aApply && aTarget.photoModeSlot < Characters::Count)
    {
        Gaze gaze;
        {
            std::scoped_lock lock(g_solverMutex);
            gaze = g_gazes[aTarget.photoModeSlot];
        }
        if (gaze.active && map->gaze[0][0] >= 0)
        {
            for (int e = 0; e < 2; ++e)
            {
                // Sideways: + turns the left eye out and the right eye in (towards the character's left).
                const float pitch = gaze.turns[e].z;
                const float side = gaze.turns[e].x;
                const float out = e == 0 ? side : -side;
                const float values[4] = {std::clamp(pitch / GazeUpDegrees, 0.f, 1.f),
                                         std::clamp(-pitch / GazeDownDegrees, 0.f, 1.f),
                                         std::clamp(-out / GazeSideDegrees, 0.f, 1.f),
                                         std::clamp(out / GazeSideDegrees, 0.f, 1.f)};
                for (int d = 0; d < 4; ++d)
                {
                    const int32_t track = map->gaze[e][d];
                    if (track >= 0 && static_cast<uint32_t>(track) < aCount)
                    {
                        aTracks[track] = values[d];
                    }
                }
            }
            std::scoped_lock lock(g_solverMutex);
            g_gazes[aTarget.photoModeSlot].takenAt = NowMs();
        }
    }
    if (aApply && AnyFaceWeight(aPose.faceWeights))
    {
        for (size_t p = 0; p < poseCount; ++p)
        {
            const int32_t track = map->tracks[p];
            if (track >= 0 && aPose.faceWeights[p] >= 0.f)
            {
                aTracks[track] = aPose.faceWeights[p];
            }
        }
    }
    float highest = 0.f;
    for (size_t p = 0; p < poseCount; ++p)
    {
        const int32_t track = map->tracks[p];
        if (track >= 0 && std::isfinite(aTracks[track]))
        {
            highest = std::max(highest, aTracks[track]);
        }
    }
    return highest;
}

void OnSolverInput(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount, float aActivity)
{
    if (!aBones || !IsAvailable())
    {
        return;
    }
    std::scoped_lock lock(g_checkMutex);
    auto& check = g_checks[aTarget.animatedComponent];
    check.active = false;
    check.hasOutput = false;
    if (check.ignored || check.confirmed || aActivity < CheckActivity)
    {
        return;
    }
    if (check.watched.empty())
    {
        if (const Head* head = HeadFor(aTarget))
        {
            for (const int32_t bone : MapFor(aTarget, *head)->indices)
            {
                if (bone >= 0)
                {
                    check.watched.push_back(bone);
                }
            }
        }
    }
    check.input.resize(check.watched.size());
    for (size_t i = 0; i < check.watched.size(); ++i)
    {
        if (static_cast<uint32_t>(check.watched[i]) >= aCount)
        {
            return;
        }
        check.input[i] = aBones[check.watched[i]];
    }
    check.active = !check.watched.empty();
}

void OnSolverOutput(uintptr_t aComponent, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    if (!aBones)
    {
        return;
    }
    std::scoped_lock lock(g_checkMutex);
    const auto it = g_checks.find(aComponent);
    if (it == g_checks.end() || !it->second.active)
    {
        return;
    }
    auto& check = it->second;
    check.output.resize(check.watched.size());
    for (size_t i = 0; i < check.watched.size(); ++i)
    {
        if (static_cast<uint32_t>(check.watched[i]) >= aCount)
        {
            check.active = false;
            return;
        }
        check.output[i] = aBones[check.watched[i]];
    }
    const auto [degrees, meters] = Difference(check.input.data(), check.output.data(), check.watched.size());
    check.moved = degrees > MovedDegrees || meters > MovedMeters;
    check.hasOutput = true;
}

void OnFinalFace(const Target& aTarget, const RED4ext::QsTransform* aBones, uint32_t aCount)
{
    {
        std::scoped_lock lock(g_checkMutex);
        const auto it = g_checks.find(aTarget.animatedComponent);
        if (it == g_checks.end() || !it->second.active || !it->second.hasOutput)
        {
            return;
        }
        auto& check = it->second;
        check.active = false;
        thread_local std::vector<RED4ext::QsTransform> final;
        final.resize(check.watched.size());
        for (size_t i = 0; i < check.watched.size(); ++i)
        {
            if (static_cast<uint32_t>(check.watched[i]) >= aCount)
            {
                return;
            }
            final[i] = aBones[check.watched[i]];
        }
        const auto [degrees, meters] = Difference(check.output.data(), final.data(), final.size());
        if (check.moved && degrees < SameDegrees && meters < SameMeters)
        {
            check.confirmed = true;
            Log::Info("Face poses: character %u: the facial solver poses the face", aTarget.photoModeSlot);
            return;
        }
        if (++check.badFrames < BadFramesToIgnore)
        {
            return;
        }
        check.ignored = true;
        Log::Info("Face poses: character %u: the face ignores the facial solver (%s, %.1f degrees / %.1f mm from its "
                  "result), posed on the bones",
                  aTarget.photoModeSlot, check.moved ? "its result is replaced later" : "it does not move the face",
                  degrees, meters * 1000.f);
    }
    std::scoped_lock solverLock(g_solverMutex);
    g_solverFaces.erase(aTarget.animatedComponent);
    if (aTarget.photoModeSlot < Characters::Count)
    {
        g_gameExpressions[aTarget.photoModeSlot].known = false;
        g_gameExpressions[aTarget.photoModeSlot].values = {};
    }
}

float GameValue(size_t aPose)
{
    const uint32_t character = Characters::Selected();
    if (aPose >= MaxFacePoses || character >= Characters::Count)
    {
        return 0.f;
    }
    std::scoped_lock lock(g_solverMutex);
    const auto& expression = g_gameExpressions[character];
    return expression.known ? expression.values[aPose] : 0.f;
}

float ShownValue(size_t aPose)
{
    const float value = PoseOverrides::GetFaceWeight(aPose);
    return value >= 0.f ? value : GameValue(aPose);
}

bool IsEdited(size_t aPose)
{
    const float value = PoseOverrides::GetFaceWeight(aPose);
    return value >= 0.f && std::abs(value - GameValue(aPose)) > 0.005f;
}

void OnExpressionChanged(uint32_t aCharacter, int32_t aExpression)
{
    if (aCharacter >= Characters::Count)
    {
        return;
    }
    FaceWeights before{}; // the expression it showed, for the undo step
    {
        std::scoped_lock lock(g_solverMutex);
        auto& expression = g_gameExpressions[aCharacter];
        if (expression.menuIndex == aExpression)
        {
            return;
        }
        expression.menuIndex = aExpression;
        if (expression.known)
        {
            before = expression.values;
            expression.captureAt = NowMs() + ExpressionSettleMs;
        }
    }
    if (PoseOverrides::ResetFaceOf(aCharacter, before))
    {
        Log::Info("Expression: character %u changed expression in the menu, face edits reset", aCharacter);
    }
}

void ResetCharacter(uint32_t aCharacter)
{
    if (aCharacter >= Characters::Count)
    {
        return;
    }
    std::scoped_lock lock(g_solverMutex);
    g_gameExpressions[aCharacter] = {};
    g_gazes[aCharacter] = {};
    g_characterMale[aCharacter] = false;
}

void ResetSession()
{
    {
        std::scoped_lock checkLock(g_checkMutex);
        g_checks.clear();
    }
    std::scoped_lock lock(g_solverMutex);
    for (auto& expression : g_gameExpressions)
    {
        expression = {};
    }
    for (auto& gaze : g_gazes)
    {
        gaze = {};
    }
}

void SetGaze(uint32_t aCharacter, bool aActive, const std::array<Math::Vec3, 2>& aTurns)
{
    if (aCharacter >= Characters::Count)
    {
        return;
    }
    std::scoped_lock lock(g_solverMutex);
    auto& gaze = g_gazes[aCharacter];
    gaze.active = aActive;
    if (aActive)
    {
        gaze.turns = aTurns;
    }
}

bool SolverTakesGaze(const Target& aTarget)
{
    if (aTarget.photoModeSlot >= Characters::Count || !IsSolverFace(aTarget.animatedComponent))
    {
        return false;
    }
    std::scoped_lock lock(g_solverMutex);
    const auto& gaze = g_gazes[aTarget.photoModeSlot];
    return gaze.active && NowMs() - gaze.takenAt < SolverFaceTimeout;
}

void SetCharacterMale(uint32_t aCharacter, bool aMale)
{
    if (aCharacter < Characters::Count)
    {
        g_characterMale[aCharacter] = aMale;
    }
}

void Apply(const Target& aTarget, RED4ext::QsTransform* aBones, uint32_t aCount, const OverrideSnapshot& aPose)
{
    if (!AnyFaceWeight(aPose.faceWeights) || !IsAvailable())
    {
        return;
    }
    // A face the solver poses gets here only the poses its rig has no track for.
    std::shared_ptr<const TrackMap> solverTracks;
    if (IsSolverFace(aTarget.animatedComponent))
    {
        solverTracks = FindTrackMap(aTarget.rig);
        if (!solverTracks)
        {
            return;
        }
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
        if (weight <= 0.f || inbetweens.empty() ||
            (solverTracks && (p >= solverTracks->tracks.size() || solverTracks->tracks[p] >= 0)))
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
    for (size_t i = 0; i < Count(); ++i)
    {
        const float value = std::round(ShownValue(i) * 1000.f) / 1000.f;
        if (value > 0.f)
        {
            weights[g_data.poses[i].name] = value;
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
    // The whole face: poses missing from the file are 0.
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
            weights[pose] = std::clamp(value.get<float>(), 0.f, MaxLoadedValue);
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

std::string Rename(const std::string& aOldName, const std::string& aNewName)
{
    return NamedFiles::Rename(GetDirectory(), aOldName, aNewName);
}
} // namespace APM::Pose::FacePoses
